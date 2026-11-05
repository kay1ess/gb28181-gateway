import test from 'node:test';
import assert from 'node:assert/strict';
import { buildTree, visibleRows } from '../src/resource_tree.js';

const platform = { id: 'a', gb_id: 'server' };
const resource = (id, parent = '', extra = {}) => ({
  id: `a:${id}`,
  gb_id: id,
  parent_gb_id: parent,
  platform_id: 'a',
  name: id,
  state: 'online',
  ...extra,
});

test('platform grouping, nested directories and collapsed branches retain opaque identities', () => {
  const tree = buildTree(platform, [
    resource('group', 'server', { parental: true }),
    resource('nested', 'group', { parental: true }),
    resource('camera', 'nested'),
    resource('orphan', 'missing'),
    resource('self', 'self'),
    resource('foreign', '', { platform_id: 'b' }),
  ]);
  assert.equal(tree.nodes.size, 5);
  assert.deepEqual(
    visibleRows(tree, new Set()).map((row) => row.resource.gb_id),
    ['group', 'orphan', 'self'],
  );
  assert.deepEqual(
    visibleRows(tree, new Set(['a:group', 'a:nested'])).map((row) => [
      row.resource.gb_id,
      row.depth,
    ]),
    [
      ['group', 0],
      ['nested', 1],
      ['camera', 2],
      ['orphan', 0],
      ['self', 0],
    ],
  );
});

test('search and status filters preserve ancestry even through collapsed nodes', () => {
  const tree = buildTree(platform, [
    resource('group', '', { parental: true, state: 'unknown' }),
    resource('front', 'group', { name: '前门摄像头' }),
    resource('back', 'group', { state: 'offline' }),
  ]);
  assert.deepEqual(
    visibleRows(tree, new Set(), { keyword: '前门', state: 'online' }).map(
      (row) => row.resource.gb_id,
    ),
    ['group', 'front'],
  );
  assert.deepEqual(
    visibleRows(tree, new Set(), { state: 'offline' }).map((row) => row.resource.gb_id),
    ['group', 'back'],
  );
});

test('cyclic parent data cannot hang or hide the entire resource tree', () => {
  const tree = buildTree(platform, [
    resource('a', 'b', { parental: true }),
    resource('b', 'c', { parental: true }),
    resource('c', 'a', { parental: true }),
  ]);
  assert.equal(visibleRows(tree, new Set(['a:a', 'a:b', 'a:c'])).length, 3);
});

test('different platforms may share the same GB identifier', () => {
  const rows = [resource('camera'), { ...resource('camera'), id: 'b:camera', platform_id: 'b' }];
  assert.equal(buildTree(platform, rows).roots[0].id, 'a:camera');
  assert.equal(buildTree({ id: 'b' }, rows).roots[0].id, 'b:camera');
});
