import {spawn} from 'node:child_process';
import {mkdirSync, existsSync, readdirSync, statSync, unlinkSync, rmSync, lstatSync} from 'node:fs';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';

// Only these fixed messages can reach the PSP. Raw extractor errors stay in
// bounded, redacted server logs and never become public API response text.
export function playbackError(detail = '') {
  const text = String(detail).toLowerCase();
  if (/confirm.*not a bot|sign in|login required|http error 429|too many requests/.test(text)) return 'YouTube rejected this server or requires sign-in. See server logs.';
  if (/requested format.*not available|no video formats|only images/.test(text)) return 'No compatible video format was available. See server logs.';
  if (/private video|video.*unavailable|not available|removed|copyright|geo.?restrict|not.*country/.test(text)) return 'This video is unavailable or restricted.';
  if (/http error 403|forbidden/.test(text)) return 'YouTube refused the video download. See server logs.';
  if (/ten minutes|duration|size limit|too large|exceeds/.test(text)) return 'Video exceeds the server duration or size limit.';
  if (/no space left|disk quota/.test(text)) return 'Server storage is full.';
  if (/timed out|timeout/.test(text)) return 'Video preparation timed out.';
  if (/ffmpeg|ffprobe|calledprocesserror|jpeg|playable frames/.test(text)) return 'Server video conversion failed. See server logs.';
  return 'Video preparation failed. See server logs.';
}
export function redactWorkerLog(detail) {
  return String(detail).slice(-8192)
    .replace(/https?:\/\/[^\s"'<>]+/gi, '[URL]')
    .replace(/AIza[\w-]+/g, '[API KEY]')
    .replace(/(authorization|cookie|token|password)\s*[:=]\s*[^\r\n]+/gi, '$1=[REDACTED]')
    .replace(/\x1b\[[0-9;]*[A-Za-z]/g, '')
    .replace(/[^\x20-\x7e\n\r\t]/g, '').trim();
}

export class Jobs {
  constructor(directory, {enabled = false, demo = false, python = process.env.PYTHON || 'python3', runner} = {}) {
    Object.assign(this, {directory, enabled, demo, python, runner});
    this.items = new Map(); this.active = 0;
    mkdirSync(directory, {recursive:true}); this.cleanup();
  }
  cleanup() {
    const now = Date.now();
    for (const name of readdirSync(this.directory)) {
      // Clean only worker-owned names after a killed or interrupted process.
      if (/^(job-[\w-]{11}-[\w-]+|[\w-]{11}\.ptv\.tmp)$/.test(name)) {
        const path = join(this.directory, name);
        if (now - lstatSync(path).mtimeMs > 45*60000) rmSync(path, {recursive:true, force:true});
        continue;
      }
      if (!/^[\w-]{11}\.ptv$/.test(name)) continue;
      const path = join(this.directory, name);
      if (now - statSync(path).mtimeMs > 30*60000) unlinkSync(path);
    }
    for (const [id, item] of this.items) if (now - item.time > 30*60000 && item.state !== 'WAIT') this.items.delete(id);
  }
  path(id) {return join(this.directory, `${id}.ptv`);}
  error(id) {return this.items.get(id)?.error || 'Video preparation failed. See server logs.';}
  status(id) {
    if (existsSync(this.path(id)) && Date.now() - statSync(this.path(id)).mtimeMs <= 30*60000) return 'READY';
    return this.items.get(id)?.state || 'MISSING';
  }
  start(id) {
    this.cleanup();
    if (['READY','WAIT'].includes(this.status(id))) return this.status(id);
    if (!this.enabled && !(this.demo && id === 'DEMO0000001')) return 'DISABLED';
    if (this.active >= 1) return 'BUSY';
    if (readdirSync(this.directory).filter(x => /^[\w-]{11}\.ptv$/.test(x)).length >= 8) return 'FULL';
    const item = {state:'WAIT', time:Date.now()};
    this.items.set(id, item); this.active++;
    const complete = (ok, detail = '') => {
      item.state = ok && existsSync(this.path(id)) ? 'READY' : 'ERROR';
      if (item.state === 'ERROR') item.error = playbackError(detail);
      item.time = Date.now(); this.active--;
    };
    if (this.runner) {
      Promise.resolve().then(() => this.runner(id, this.path(id))).then(() => complete(true), error => complete(false, String(error)));
    } else {
      const args = [fileURLToPath(new URL('prepare.py', import.meta.url)), '--video', id, '--output', this.path(id)];
      if (this.demo && id === 'DEMO0000001') args.push('--demo');
      const child = spawn(this.python, args, {stdio:['ignore', 'ignore', 'pipe'], detached:process.platform !== 'win32'});
      let stderr = '';
      child.stderr.setEncoding('utf8');
      child.stderr.on('data', chunk => {stderr = (stderr + chunk).slice(-8192);});
      let done = false;
      const finish = (ok, detail = stderr) => {if (!done) {done = true; clearTimeout(timer); complete(ok, detail);}};
      const timer = setTimeout(() => {
        try {if (process.platform !== 'win32') process.kill(-child.pid, 'SIGKILL'); else child.kill('SIGKILL');} catch {}
        console.error(`Playback worker timed out: ${id}`);
        finish(false, 'timeout');
      }, 20*60000);
      timer.unref(); child.once('error', () => {console.error('Playback worker could not start'); finish(false);});
      child.once('close', code => {
        if (code !== 0) console.error(`Playback worker failed: ${id} (exit ${code})\n${redactWorkerLog(stderr)}`);
        finish(code === 0);
      });
    }
    return 'WAIT';
  }
}
