import test from 'node:test';
import assert from 'node:assert/strict';
import { ApiError, createApi } from '../src/api.js';

test('omitting playback mode delegates default selection to the backend', async () => {
  let body;
  const api = createApi({
    token: 'test',
    fetcher: async (_, options) => {
      body = JSON.parse(options.body);
      return Response.json({ code: 0, data: {} });
    },
  });
  await api.start('camera');
  assert.deepEqual(body, { resource_id: 'camera' });
});

test('all routes carry Bearer auth, preserve opaque IDs, and use backend verbs', async () => {
  const calls = [];
  const api = createApi({
    token: 'test-secret',
    fetcher: async (url, options) => {
      calls.push({ url, ...options });
      return Response.json({ code: 0, data: { ok: true } });
    },
  });
  const value = 'a/b?c';
  await api.health();
  await api.platforms();
  await api.platform(value);
  await api.savePlatform({ id: value });
  await api.savePlatform({ id: value }, true);
  await api.removePlatform(value);
  await api.operation(value);
  await api.resources({ keyword: '前门', state: '', offset: 20 });
  await api.resource(value);
  await api.info(value);
  await api.info(value, true);
  await api.status(value);
  await api.status(value, true);
  await api.start(value, 'tcp_passive');
  await api.playback(value);
  await api.forceStop(value);
  await api.renew(value);
  await api.release(value);
  assert.equal(calls.length, 18);
  for (const call of calls) assert.equal(call.headers.Authorization, 'Bearer test-secret');
  assert.equal(calls[2].url, '/api/platforms/a%2Fb%3Fc');
  assert.equal(calls[4].method, 'PATCH');
  assert.equal(calls[5].method, 'DELETE');
  assert.equal(calls[6].url, '/api/operations/a%2Fb%3Fc');
  assert.equal(calls[9].method, 'GET');
  assert.equal(calls[10].method, 'POST');
  assert.equal(calls[12].url, '/api/resources/a%2Fb%3Fc/status-refresh');
  assert.deepEqual(JSON.parse(calls[13].body), {
    resource_id: value,
    mode: 'tcp_passive',
  });
  assert.equal(new URL(calls[7].url, 'http://test').searchParams.get('keyword'), '前门');
  assert.equal(calls[17].method, 'DELETE');
});

test('business errors preserve code and trace, even on HTTP 200', async () => {
  for (const status of [200, 504]) {
    const api = createApi({
      token: 'test',
      fetcher: async () =>
        Response.json({ code: 4003, message: '媒体接收超时', trace_id: 'trace_7' }, { status }),
    });
    await assert.rejects(
      api.health(),
      (error) =>
        error instanceof ApiError &&
        error.code === 4003 &&
        error.traceId === 'trace_7' &&
        error.status === status,
    );
  }
});

test('HTML gateway errors give actionable feedback instead of a JSON exception', async () => {
  const api = createApi({
    token: 'test',
    fetcher: async () => new Response('<html>bad gateway</html>', { status: 502 }),
  });
  await assert.rejects(api.health(), /代理地址/);
});

test('disconnect cancels in-flight requests', async () => {
  const api = createApi({
    token: 'test',
    fetcher: (_, { signal }) =>
      new Promise((_, reject) => {
        signal.addEventListener('abort', () => reject(new DOMException('aborted', 'AbortError')));
      }),
  });
  const pending = api.health();
  api.abort();
  await assert.rejects(pending, /取消/);
});
