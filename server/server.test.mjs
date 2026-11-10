import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createServer} from './server.mjs';
import {Jobs, playbackError, redactWorkerLog} from './jobs.mjs';
import {mkdtempSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

async function withServer(options, run) {
  const server = createServer(options);
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try { await run(`http://127.0.0.1:${server.address().port}`); }
  finally { await new Promise(resolve => server.close(resolve)); }
}
test('search calls official API and bounds PSP protocol fields', async () => {
  await withServer({key:'test-secret', fetcher:async url => {
    assert.equal(url.hostname, 'www.googleapis.com');
    assert.equal(url.searchParams.get('q'), 'music');
    return {ok:true, json:async () => ({items:[{id:{videoId:'abcdefghijk'},snippet:{title:'Hello\tworld\n!',channelTitle:'Channel'}}]})};
  }}, async base => {
    const response = await fetch(`${base}/v1/search?q=music`);
    assert.equal(response.status, 200);
    assert.equal(await response.text(), 'OK\nabcdefghijk\tHello world !\tChannel\n');
  });
});
test('validation, missing credentials and upstream errors stay explicit', async () => {
  await withServer({key:''}, async base => {
    assert.equal((await fetch(`${base}/v1/search?q=`)).status, 400);
    assert.equal((await fetch(`${base}/v1/search?q=test`)).status, 503);
    assert.equal((await fetch(`${base}/unknown`)).status, 404);
  });
  await withServer({key:'secret',fetcher:async () => {throw Error('secret');}}, async base => {
    const response = await fetch(`${base}/v1/search?q=test`);
    assert.equal(response.status, 502);
    assert.ok(!(await response.text()).includes('secret'));
  });
});
test('per-client throttling prevents repeated upstream requests', async () => {
  await withServer({demo:true}, async base => {
    for (let i=0;i<6;i++) assert.equal((await fetch(`${base}/v1/search?q=test`)).status, 200);
    assert.equal((await fetch(`${base}/v1/search?q=test`)).status, 429);
  });
});

test('global search budget survives restart and cached searches do not consume it', async () => {
  const directory = mkdtempSync(join(tmpdir(), 'ptube-budget-'));
  let calls = 0;
  const options = {key:'test', dailyLimit:1, stateDir:directory, fetcher:async () => {calls++; return {ok:true, json:async () => ({items:[]})};}};
  try {
    await withServer(options, async base => {
      assert.equal((await fetch(`${base}/v1/search?q=one`)).status, 200);
      assert.equal((await fetch(`${base}/v1/search?q=one`)).status, 200);
      assert.equal((await fetch(`${base}/v1/search?q=two`)).status, 429);
      assert.equal(calls, 1);
    });
    await withServer(options, async base => assert.equal((await fetch(`${base}/v1/search?q=three`)).status, 429));
  } finally {rmSync(directory, {recursive:true, force:true});}
});

test('prepare is bounded, validates IDs and serves exactly the ready file', async () => {
  const directory = mkdtempSync(join(tmpdir(), 'ptube-jobs-'));
  let release;
  const gate = new Promise(resolve => {release = resolve;});
  const fixture = Buffer.alloc(40); fixture.write('PTV1');
  const jobs = new Jobs(directory, {enabled:true, runner:async (id, path) => {await gate; writeFileSync(path, fixture);}});
  try {
    await withServer({jobs}, async base => {
      assert.equal((await fetch(`${base}/v1/prepare?id=bad`, {method:'POST'})).status, 400);
      assert.equal((await fetch(`${base}/v1/prepare?id=abcdefghijk`)).status, 405);
      assert.equal(await (await fetch(`${base}/v1/prepare?id=abcdefghijk`, {method:'POST'})).text(), 'WAIT\tPreparing video\n');
      assert.equal(jobs.start('12345678901'), 'BUSY');
      assert.equal((await fetch(`${base}/v1/media/abcdefghijk`)).status, 404);
      release();
      for (let i=0;i<50 && jobs.status('abcdefghijk') !== 'READY';i++) await new Promise(resolve => setTimeout(resolve, 5));
      const response = await fetch(`${base}/v1/media/abcdefghijk`);
      assert.equal(response.status, 200);
      assert.equal(Number(response.headers.get('content-length')), fixture.length);
      assert.deepEqual(Buffer.from(await response.arrayBuffer()), fixture);
      assert.equal((await fetch(`${base}/v1/prepare?id=abcdefghijk`, {method:'POST'})).status, 200);
      assert.equal((await fetch(`${base}/v1/prepare?id=12345678901`, {method:'POST'})).status, 429);
    });
  } finally {release(); rmSync(directory, {recursive:true, force:true});}
});

test('unofficial playback is disabled unless configured', async () => {
  const directory = mkdtempSync(join(tmpdir(), 'ptube-disabled-'));
  try {
    await withServer({jobs:new Jobs(directory)}, async base => {
      const response = await fetch(`${base}/v1/prepare?id=abcdefghijk`, {method:'POST'});
      assert.equal(response.status, 503);
      assert.match(await response.text(), /disabled/);
    });
  } finally {rmSync(directory, {recursive:true, force:true});}
});

test('worker failure categories are actionable and do not expose raw errors', async () => {
  assert.match(playbackError("Sign in to confirm you're not a bot"), /YouTube rejected/);
  assert.match(playbackError('Requested format is not available'), /compatible video format/);
  assert.match(playbackError('subprocess.CalledProcessError: ffmpeg failed'), /conversion failed/);
  assert.match(playbackError('No space left on device'), /storage is full/);
  const raw = 'HTTP error 403 https://example.com/video?token=private\nCookie: SECRET\nAIzaSECRET123';
  const clean = redactWorkerLog(raw);
  assert.ok(!clean.includes('SECRET') && !clean.includes('private') && !clean.includes('example.com'));
  const directory = mkdtempSync(join(tmpdir(), 'ptube-errors-'));
  const jobs = new Jobs(directory, {enabled:true, runner:async () => {throw Error(raw);}});
  try {
    await withServer({jobs}, async base => {
      await fetch(`${base}/v1/prepare?id=abcdefghijk`, {method:'POST'});
      for (let i=0;i<50 && jobs.status('abcdefghijk') === 'WAIT';i++) await new Promise(resolve => setTimeout(resolve, 5));
      const response = await fetch(`${base}/v1/status?id=abcdefghijk`);
      assert.equal(response.status, 503);
      assert.equal(await response.text(), 'ERROR\tYouTube refused the video download. See server logs.\n');
    });
  } finally {rmSync(directory, {recursive:true, force:true});}
});
