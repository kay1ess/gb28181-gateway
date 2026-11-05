import test from 'node:test';
import assert from 'node:assert/strict';
import { playability } from '../src/playability.js';

const platform = { enabled: true, state: 'online' };
const camera = { gb_id: '34020000001320000001', state: 'online', parental: false };
test('video candidates require a registered platform and non-directory online channel', () => {
  assert.equal(playability(camera, platform).label, '点播');
  for (const resource of [
    { ...camera, parental: true },
    { ...camera, state: 'offline' },
  ])
    assert.equal(playability(resource, platform).allowed, false);
  assert.equal(playability(camera, platform, true).allowed, false);
  for (const parent of [
    undefined,
    { ...platform, enabled: false },
    { ...platform, state: 'offline' },
    { ...platform, state: 'unknown' },
  ])
    assert.equal(playability(camera, parent).allowed, false);
});
test('host and non-video types are blocked while unknown capabilities remain explicitly testable', () => {
  for (const type of ['118', '200', '136', '137', '133'])
    assert.equal(
      playability({ ...camera, gb_id: `3402000000${type}0000001` }, platform).allowed,
      false,
    );
  for (const resource of [
    { ...camera, gb_id: 'custom-camera' },
    { ...camera, state: 'unknown' },
  ]) {
    assert.equal(playability(resource, platform).allowed, true);
    assert.equal(playability(resource, platform).label, '尝试播放');
  }
});
