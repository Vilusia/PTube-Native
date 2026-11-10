// Runs inside the server container: real FFmpeg + worker + HTTP, no YouTube calls.
import assert from 'node:assert/strict';
import {mkdtempSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {spawnSync} from 'node:child_process';
import {createServer} from './server.mjs';
import {Jobs} from './jobs.mjs';

const directory = mkdtempSync(join(tmpdir(), 'ptube-integration-'));
const jobs = new Jobs(directory, {demo:true});
const server = createServer({demo:true, jobs});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const base = `http://127.0.0.1:${server.address().port}`;
try {
  const search = await (await fetch(base + '/v1/search?q=demo')).text();
  assert.match(search, /DEMO0000001/);
  assert.equal((await fetch(base + '/v1/prepare?id=DEMO0000001', {method:'POST'})).status, 200);
  const deadline = Date.now() + 60000;
  while (jobs.status('DEMO0000001') === 'WAIT' && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 200));
  assert.equal(jobs.status('DEMO0000001'), 'READY');
  const response = await fetch(base + '/v1/media/DEMO0000001');
  assert.equal(response.status, 200);
  const body = Buffer.from(await response.arrayBuffer());
  assert.equal(body.length, Number(response.headers.get('content-length')));
  assert.equal(body.toString('ascii', 0, 4), 'PTV1');
  assert.equal(body.readUInt32LE(4), 640);
  assert.equal(body.readUInt32LE(8), 360);
  assert.equal(body.readUInt32LE(12), 44100);
  assert.equal(body.readUInt32LE(16), 2944);
  let position = 32, frames = 0, sound = false;
  const images = [];
  for (;;) {
    const length = body.readUInt32LE(position); position += 4;
    if (!length) break;
    assert.ok(length <= 524288);
    const jpeg = body.subarray(position, position + length); position += length;
    assert.equal(jpeg.readUInt16BE(0), 0xffd8);
    assert.equal(jpeg.readUInt16BE(jpeg.length-2), 0xffd9);
    images.push(jpeg);
    const pcm = body.subarray(position, position+2944*4); position += 2944*4;
    assert.equal(pcm.length, 2944*4);
    if (pcm.some(byte => byte !== 0)) sound = true;
    frames++;
  }
  assert.equal(position, body.length);
  assert.ok(frames >= 118 && frames <= 121, `Unexpected frame count ${frames}`);
  assert.ok(sound, 'Missing audio');
  const decode = spawnSync('ffmpeg', ['-v','error','-threads','1','-f','mjpeg','-i','pipe:0','-f','null','-'], {input:Buffer.concat(images)});
  assert.equal(decode.status, 0, decode.stderr?.toString());
  console.log(JSON.stringify({result:'PASS', frames, resolution:'640x360', samplesPerFrame:2944, durationSeconds:frames*2944/44100, bytes:body.length, jpegDecode:'PASS', audio:'non-silent PCM'}, null, 2));
} finally {
  await new Promise(resolve => server.close(resolve));
  rmSync(directory, {recursive:true, force:true});
}
