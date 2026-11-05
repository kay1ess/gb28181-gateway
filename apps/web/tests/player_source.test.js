import test from 'node:test';
import assert from 'node:assert/strict';
import { playerSource } from '../src/player_source.js';

test('browser consumes HTTP-FLV through a same-origin media proxy', () => {
  const playback = {
    endpoints: [
      { protocol: 'rtsp', url: 'rtsp://192.168.1.16:8554/rtp/play_1' },
      { protocol: 'http-flv', url: 'http://192.168.1.16:8081/rtp/play_1.live.flv' },
    ],
  };
  assert.equal(playerSource(playback), '/media/rtp/play_1.live.flv');
  assert.equal(playerSource(playback, '/console/'), '/console/media/rtp/play_1.live.flv');
});

test('missing or malformed FLV endpoints never fall back to RTSP or arbitrary paths', () => {
  for (const url of [
    'rtsp://host/rtp/play_1.live.flv',
    'http://host/api/health',
    'http://host/rtp/a/b.live.flv',
    'bad',
  ]) {
    assert.equal(playerSource({ endpoints: [{ protocol: 'http-flv', url }] }), '');
  }
  assert.equal(playerSource({ endpoints: [{ protocol: 'rtsp', url: 'rtsp://host/rtp/a' }] }), '');
});
