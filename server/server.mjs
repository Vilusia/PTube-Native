import http from 'node:http';
import {createReadStream, existsSync, readFileSync, writeFileSync, renameSync, mkdirSync, statSync} from 'node:fs';
import {join} from 'node:path';
import {pathToFileURL} from 'node:url';
import {Jobs} from './jobs.mjs';

export const ascii = value => String(value ?? '').replace(/&amp;/g, '&').replace(/&#39;/g, "'").replace(/&quot;/g, '"').replace(/&lt;/g, '<').replace(/&gt;/g, '>').normalize('NFKD').replace(/[^\x20-\x7e]/g, ' ').slice(0, 120);
export function createServer({key = process.env.YOUTUBE_API_KEY, fetcher = fetch,
  demo = process.env.PTUBE_DEMO === '1', jobs,
  stateDir, dailyLimit = Number(process.env.DAILY_SEARCH_LIMIT || 40)} = {}) {
  if (!Number.isInteger(dailyLimit) || dailyLimit < 1) throw Error('Invalid daily search limit');
  const clients = new Map(), searches = new Map();
  let budget = {day:'', count:0}, inflight = 0, streams = 0;
  if (stateDir) {
    mkdirSync(stateDir, {recursive:true});
    const path = join(stateDir, 'search-budget.json');
    if (existsSync(path)) {
      budget = JSON.parse(readFileSync(path, 'utf8'));
      if (typeof budget.day !== 'string' || !Number.isInteger(budget.count) || budget.count < 0) throw Error('Invalid persisted budget');
    }
  }
  const server = http.createServer(async (req, res) => {
    const reply = (status, body) => {
      if (res.destroyed || res.headersSent) return;
      res.writeHead(status, {'Content-Type':'text/plain; charset=us-ascii', 'Content-Length':Buffer.byteLength(body), 'Cache-Control':'no-store', 'Connection':'close', 'X-Content-Type-Options':'nosniff'});
      res.end(body);
    };
    try {
      if (req.url.length > 1024) return reply(414, 'ERROR\tRequest too long\n');
      const url = new URL(req.url, 'http://localhost');
      if (url.pathname === '/health' && req.method === 'GET') return reply(200, 'OK\tPTube alpha\n');
      const now = Date.now(), ip = req.socket.remoteAddress;
      for (const [address, entry] of clients) if (entry.until < now) clients.delete(address);
      const entry = clients.get(ip) || {until:now + 60000, count:0, searches:0, prepares:0};
      if (clients.size >= 10000 && !clients.has(ip)) return reply(503, 'ERROR\tServer busy\n');
      clients.set(ip, entry);
      if (++entry.count > 120) return reply(429, 'ERROR\tToo many requests; wait a minute\n');
      if (url.pathname === '/v1/prepare' || url.pathname === '/v1/status') {
        if (req.method !== (url.pathname === '/v1/prepare' ? 'POST' : 'GET')) return reply(405, 'ERROR\tWrong request method\n');
        const id = url.searchParams.get('id') || '';
        if (!/^[\w-]{11}$/.test(id)) return reply(400, 'ERROR\tInvalid video ID\n');
        if (!jobs) return reply(503, 'ERROR\tPlayback is disabled on this server\n');
        if (url.pathname === '/v1/prepare' && ++entry.prepares > 2) return reply(429, 'ERROR\tWait before preparing another video\n');
        const status = url.pathname === '/v1/prepare' ? jobs.start(id) : jobs.status(id);
        const messages = {DISABLED:'Unofficial playback is disabled', BUSY:'Another video is preparing; try again shortly', FULL:'Video cache is full; try again later', MISSING:'Prepare this video first', ERROR:jobs.error(id)};
        if (messages[status]) return reply(503, `ERROR\t${messages[status]}\n`);
        return reply(200, status === 'READY' ? `READY\t/v1/media/${id}\n` : 'WAIT\tPreparing video\n');
      }
      if (url.pathname.startsWith('/v1/media/')) {
        if (req.method !== 'GET') return reply(405, 'ERROR\tGET required\n');
        const id = url.pathname.slice('/v1/media/'.length);
        if (!/^[\w-]{11}$/.test(id) || !jobs || jobs.status(id) !== 'READY') return reply(404, 'ERROR\tVideo not ready\n');
        if (streams >= 4) return reply(503, 'ERROR\tAll playback slots are busy\n');
        const path = jobs.path(id), size = statSync(path).size;
        if (size > 256*1024*1024 || size < 36) return reply(500, 'ERROR\tInvalid prepared video\n');
        streams++;
        res.writeHead(200, {'Content-Type':'application/octet-stream','Content-Length':size,'Connection':'close','Cache-Control':'no-store'});
        const file = createReadStream(path), timer = setTimeout(() => res.destroy(), 15*60000);
        timer.unref();
        res.once('close', () => {clearTimeout(timer); file.destroy(); streams--;});
        file.on('error', () => res.destroy()); file.pipe(res); return;
      }
      if (url.pathname !== '/v1/search') return reply(404, 'ERROR\tNot found\n');
      if (req.method !== 'GET') return reply(405, 'ERROR\tGET required\n');
      const q = (url.searchParams.get('q') || '').trim();
      if (!q || q.length > 80) return reply(400, 'ERROR\tSearch must be 1-80 characters\n');
      if (++entry.searches > 6) return reply(429, 'ERROR\tPlease wait a minute before searching again\n');
      if (demo) return reply(200, 'OK\nDEMO0000001\tTest pattern with sound - 8 seconds\tPTube demo\n');
      if (!key) return reply(503, 'ERROR\tServer needs a YouTube API key\n');
      for (const [text, item] of searches) if (item.until < now) searches.delete(text);
      if (searches.has(q)) return reply(200, searches.get(q).body);
      const day = new Date().toISOString().slice(0, 10);
      if (budget.day !== day) budget = {day, count:0};
      if (budget.count >= dailyLimit) return reply(429, 'ERROR\tServer daily search budget reached\n');
      if (inflight >= 4) return reply(503, 'ERROR\tSearch service busy\n');
      budget.count++;
      if (stateDir) {
        const path = join(stateDir, 'search-budget.json');
        writeFileSync(path + '.tmp', JSON.stringify(budget), {mode:0o600}); renameSync(path + '.tmp', path);
      }
      const endpoint = new URL('https://www.googleapis.com/youtube/v3/search');
      endpoint.search = new URLSearchParams({key, part:'snippet', type:'video', maxResults:'8', q, videoDuration:'short'});
      inflight++;
      try {
        const upstream = await fetcher(endpoint, {signal:AbortSignal.timeout(10000)});
        if (!upstream.ok) return reply(502, 'ERROR\tYouTube search unavailable; check server API key and quota\n');
        const data = await upstream.json();
        const rows = (data.items || []).filter(x => /^[\w-]{11}$/.test(x.id?.videoId)).slice(0, 8);
        const body = 'OK\n' + rows.map(x => `${x.id.videoId}\t${ascii(x.snippet?.title)}\t${ascii(x.snippet?.channelTitle)}\n`).join('');
        if (searches.size >= 200) searches.delete(searches.keys().next().value);
        searches.set(q, {until:now + 5*60000, body}); return reply(200, body);
      } finally {inflight--;}
    } catch {reply(502, 'ERROR\tRequest failed; check server configuration\n');}
  });
  server.requestTimeout = 15000; server.headersTimeout = 10000; server.maxConnections = 100;
  return server;
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const directory = process.env.DATA_DIR || './data';
  const jobs = new Jobs(join(directory, 'media'), {enabled:process.env.ENABLE_UNOFFICIAL_PLAYBACK === '1', demo:process.env.PTUBE_DEMO === '1'});
  const port = Number(process.env.PORT || 8080);
  createServer({jobs, stateDir:directory}).listen(port, process.env.HOST || '127.0.0.1', () => console.log(`PTube alpha listening on port ${port}`));
}
