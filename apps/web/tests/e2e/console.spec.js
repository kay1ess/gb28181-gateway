import { expandPlatforms } from './tree_helpers.js';
import { test, expect } from '@playwright/test';

const camera = {
  id: 'camera_a',
  platform_id: 'lower_1',
  gb_id: '34020000001320000001',
  name: '园区东门',
  state: 'online',
  manufacturer: 'Fixture',
  model: 'IPC',
  parental: false,
};

async function mockApi(page) {
  const state = {
    platforms: [
      {
        id: 'lower_1',
        name: '园区平台',
        gb_id: '34020000002000000002',
        realm: '3402000000',
        username: '',
        enabled: true,
        default_receive_mode: 'udp',
        heartbeat_timeout_seconds: 180,
        state: 'online',
        remote_address: '192.168.1.20',
        remote_port: 5060,
        sip_transport: 'udp',
      },
    ],
    resources: [
      camera,
      {
        ...camera,
        id: 'camera_b',
        name: '园区西门',
        gb_id: '34020000001320000002',
      },
      { ...camera, id: 'group', name: '园区目录', parental: true },
    ],
    sessions: new Map(),
    calls: [],
    info: null,
    deviceStatus: null,
    job: null,
    catalogCount: 0,
  };
  await page.routeWebSocket('**/api/events', (socket) => {
    socket.onMessage((message) => {
      const command = JSON.parse(message);
      if (command.type === 'auth') {
        expect(command.token).toBe('valid-test-token');
        socket.send(JSON.stringify({ type: 'auth_ok' }));
        state.eventSocket = socket;
      } else {
        expect(command.type).toBe('catalog_sync');
        state.job = {
          id: `job_${++state.catalogCount}`,
          platform_id: command.platform_id,
          state: 'running',
          received: 0,
          expected: 3,
          error: { code: 0 },
        };
        socket.send(
          JSON.stringify({
            type: 'catalog_accepted',
            request_id: command.request_id,
            operation: state.job,
          }),
        );
      }
    });
  });
  await page.route('**/api/**', async (route) => {
    const request = route.request();
    const url = new URL(request.url());
    const path = url.pathname.replace('/api', '');
    const method = request.method();
    const body = request.postDataJSON();
    state.calls.push({ path, method, body, search: url.search });
    const fail = (code, message, status = 400) =>
      route.fulfill({
        status,
        json: { code, message, trace_id: 'ui_trace_1', data: null },
      });
    const ok = (data) =>
      route.fulfill({
        json: { code: 0, message: 'Success', data, trace_id: 'ui_ok' },
      });
    if (request.headers().authorization !== 'Bearer valid-test-token')
      return fail(1002, '认证失败', 401);
    if (path === '/health')
      return ok({
        online_platforms: state.platforms.filter((p) => p.state === 'online').length,
        active_playbacks: [...state.sessions.values()].filter((s) => s.state === 'playing').length,
        active_receivers: state.sessions.size,
        playback_leases: state.sessions.size,
      });
    if (path === '/platforms') {
      if (method === 'POST') {
        const created = { ...body, id: body.id || 'server_generated_2', state: 'unknown' };
        state.platforms.push(created);
        return ok(created);
      }
      return ok(state.platforms);
    }
    if (state.job && path === '/operations/' + state.job.id) {
      if (state.pushCatalog) return ok(state.job);
      state.job.state = state.jobFails ? 'failed' : 'succeeded';
      state.job.error = state.jobFails
        ? { code: 2003, message: '目录接收不完整', trace_id: 'catalog_trace' }
        : { code: 0 };
      state.job.received = 3;
      return ok(state.job);
    }
    if (path.startsWith('/platforms/')) {
      const id = path.split('/')[2];
      const platform = state.platforms.find((p) => p.id === id);
      if (method === 'PATCH') {
        Object.assign(platform, body);
        return ok(platform);
      }
      if (method === 'DELETE') {
        state.platforms = state.platforms.filter((p) => p.id !== id);
        return ok(null);
      }
      return ok(platform);
    }
    if (path === '/resources')
      return ok(
        state.resources
          .filter(
            (r) =>
              (!url.searchParams.get('platform_id') ||
                r.platform_id === url.searchParams.get('platform_id')) &&
              (!url.searchParams.get('keyword') ||
                r.name.includes(url.searchParams.get('keyword'))) &&
              (!url.searchParams.get('state') || r.state === url.searchParams.get('state')),
          )
          .slice(
            Number(url.searchParams.get('offset') || 0),
            Number(url.searchParams.get('offset') || 0) +
              Number(url.searchParams.get('limit') || 20),
          ),
      );
    if (path.startsWith('/resources/')) {
      if (path.endsWith('/info-refresh')) {
        state.info = {
          name: '东门摄像头',
          manufacturer: 'Fixture',
          model: 'IPC-X',
          firmware: 'v1.2',
          queried_ms: Date.now(),
        };
        return ok(state.info);
      }
      if (path.endsWith('/status-refresh')) {
        state.deviceStatus = {
          state: 'online',
          status: 'OK',
          queried_ms: Date.now(),
        };
        return ok(state.deviceStatus);
      }
      if (path.endsWith('/info'))
        return state.info ? ok(state.info) : fail(1004, '尚无设备信息缓存', 404);
      if (path.endsWith('/status'))
        return state.deviceStatus ? ok(state.deviceStatus) : fail(1004, '尚无设备状态缓存', 404);
      return ok(state.resources.find((r) => r.id === path.split('/')[2]));
    }
    if (path === '/playbacks') {
      const number = state.calls.filter(
        (c) => c.path === '/playbacks' && c.method === 'POST',
      ).length;
      const id = `play_${number}`;
      state.sessions.set(id, {
        id,
        state: 'playing',
        mode:
          body.mode ||
          state.platforms.find(
            (p) => p.id === state.resources.find((r) => r.id === body.resource_id)?.platform_id,
          )?.default_receive_mode ||
          'udp',
        resource_id: body.resource_id,
        ssrc: number,
        consumers: 1,
        endpoints: [{ protocol: 'rtsp', url: `rtsp://127.0.0.1:8554/rtp/${id}` }],
        error: { code: 0 },
      });
      return ok({
        id: `lease_${number}`,
        playback_id: id,
        expires_ms: Date.now() + 5000,
      });
    }
    if (path.startsWith('/playback-leases/')) {
      const number = path.split('/')[2].split('_')[1];
      if (method === 'DELETE') {
        state.sessions.get(`play_${number}`).state = 'ended';
        return ok(null);
      }
      return ok({
        id: `lease_${number}`,
        playback_id: `play_${number}`,
        expires_ms: Date.now() + 60000,
      });
    }
    if (path.startsWith('/playbacks/')) {
      const playback = state.sessions.get(path.split('/')[2]);
      if (state.failMedia) {
        playback.state = 'failed';
        playback.error = { code: 4003, message: '媒体接收超时', trace_id: 'media_trace' };
      }
      if (path.endsWith('/stop')) playback.state = 'ended';
      return ok(playback);
    }
    throw new Error(`Unimplemented mock route: ${method} ${path}`);
  });
  return state;
}

async function connect(page) {
  await page.goto('/');
  await page.getByLabel('API 令牌', { exact: true }).fill('valid-test-token');
  await page.getByRole('button', { name: '连接服务', exact: true }).click();
  await expect(page.getByRole('dialog')).toHaveCount(0);
}

test('authentication errors, platform create/edit/delete, blank edit password is omitted', async ({
  page,
}) => {
  const state = await mockApi(page);
  await page.goto('/');
  await page.getByLabel('API 令牌', { exact: true }).fill('incorrect');
  await page.getByRole('button', { name: '连接服务', exact: true }).click();
  await expect(page.getByRole('dialog')).toContainText('认证失败');
  await expect(page.getByRole('dialog')).toContainText('1002');
  await page.getByLabel('API 令牌', { exact: true }).fill('valid-test-token');
  await page.getByRole('button', { name: '连接服务', exact: true }).click();
  await page.getByRole('button', { name: '添加下级', exact: false }).click();
  const dialog = page.getByRole('dialog');
  await expect(dialog.getByLabel('管理 ID', { exact: true })).toHaveCount(0);
  await dialog.getByLabel('平台名称', { exact: true }).fill('新园区');
  await dialog.getByLabel('国标编码', { exact: true }).fill('34020000002000000003');
  await dialog.getByLabel('认证密码', { exact: true }).fill('test-password');
  await dialog.getByRole('button', { name: '保存平台', exact: true }).click();
  expect(
    state.calls.find((call) => call.method === 'POST' && call.path === '/platforms').body,
  ).not.toHaveProperty('id');
  const row = page.locator('.platform-tree-row').filter({ hasText: '新园区' });
  await expect(row).toBeVisible();
  await expect(row.getByRole('button', { name: '同步目录' })).toBeDisabled();
  await row.getByRole('button', { name: '编辑', exact: true }).click();
  await expect(dialog.getByLabel('认证密码', { exact: true })).toHaveValue('');
  await dialog.getByLabel('平台名称', { exact: true }).fill('新园区更新');
  await dialog.getByRole('button', { name: '保存平台', exact: true }).click();
  await expect(page.locator('.platform-tree-row').filter({ hasText: '新园区更新' })).toBeVisible();
  const patch = state.calls.find((c) => c.method === 'PATCH');
  expect(patch.path).toBe('/platforms/server_generated_2');
  expect(patch.body.id).toBe('server_generated_2');
  expect(patch.body).not.toHaveProperty('password');
  page.once('dialog', (d) => d.accept());
  await page
    .locator('.platform-tree-row')
    .filter({ hasText: '新园区更新' })
    .getByRole('button', { name: '删除', exact: true })
    .click();
  await expect(page.locator('.platform-tree-row').filter({ hasText: '新园区更新' })).toHaveCount(0);
});

test('catalog progress and completion arrive over the authenticated WebSocket', async ({
  page,
}) => {
  const state = await mockApi(page);
  state.pushCatalog = true;
  await connect(page);
  await expect.poll(() => Boolean(state.eventSocket)).toBe(true);
  await page.getByRole('button', { name: '同步目录', exact: true }).click();
  await expect(page.getByText('0 / 3 项')).toBeVisible();
  const push = (operation) =>
    state.eventSocket.send(
      JSON.stringify({
        type: 'operation_changed',
        entity_id: operation.id,
        operation,
      }),
    );
  push({ ...state.job, received: 1 });
  await expect(page.getByRole('progressbar', { name: '目录同步进度' })).toHaveAttribute(
    'value',
    '1',
  );
  await expect(page.getByText('1 / 3 项')).toBeVisible();
  push({ ...state.job, state: 'succeeded', received: 3 });
  await expect(page.getByText('已完成', { exact: true })).toBeVisible();
  await expect(page.getByText('3 / 3 项')).toBeVisible();
  await expect(page.getByRole('button', { name: '查看资源' })).toHaveCount(0);
  await page.getByRole('button', { name: '关闭', exact: true }).click();
  await page.getByRole('button', { name: '同步目录', exact: true }).click();
  await expect(page.getByText('0 / 3 项')).toBeVisible();
  await expect(page.getByText('3 / 3 项')).toHaveCount(0);
});

test('catalog, cached/live device queries, concurrent playback, auto renewal, reload and stop', async ({
  page,
}) => {
  const state = await mockApi(page);
  await connect(page);
  await page.getByRole('button', { name: '同步目录', exact: true }).click();
  await expect(page.getByText('已完成', { exact: true })).toBeVisible({
    timeout: 12000,
  });
  await page.getByRole('button', { name: '关闭', exact: true }).click();
  await expandPlatforms(page);
  await expect(
    page
      .locator('.resource-tree-row')
      .filter({ hasText: '园区目录' })
      .getByRole('button', { name: '不可播放', exact: true }),
  ).toBeDisabled();
  const first = page.locator('.resource-tree-row').filter({ hasText: '园区东门' });
  await first.getByRole('button', { name: '详情', exact: true }).click();
  const info = page.locator('.detail-block').filter({
    has: page.getByRole('heading', { name: '设备信息', exact: true }),
  });
  await info.getByRole('button', { name: '读取缓存' }).click();
  await expect(page.getByRole('alert')).toContainText('1004');
  await expect(page.getByRole('alert')).toContainText('ui_trace_1');
  await info.getByRole('button', { name: '查询下级' }).click();
  await expect(info).toContainText('IPC-X');
  await info.getByRole('button', { name: '读取缓存' }).click();
  const status = page.locator('.detail-block').filter({
    has: page.getByRole('heading', { name: '设备状态', exact: true }),
  });
  await status.getByRole('button', { name: '查询下级' }).click();
  await expect(status).toContainText('OK');
  await status.getByRole('button', { name: '读取缓存' }).click();
  await page.getByRole('button', { name: '关闭设备详情' }).click();
  await page
    .locator('.platform-tree-row')
    .first()
    .getByRole('button', { name: '编辑', exact: true })
    .click();
  await page.getByLabel('默认收流方式', { exact: true }).selectOption('tcp_passive');
  await page.getByRole('button', { name: '保存平台', exact: true }).click();
  await expect(page.getByLabel('收流方式', { exact: true })).toHaveCount(0);
  await first.locator('.tree-title').click();
  await page.getByRole('button', { name: '继续选择设备' }).click();
  await first.locator('.tree-title').click();
  await expect(page.getByRole('button', { name: /点播会话/ })).toHaveCount(0);
  await expect(page.locator('.session-card')).toHaveCount(1);
  await expect(page.locator('.session-card')).toContainText('媒体就绪');
  expect(
    state.calls.find((c) => c.path === '/playbacks' && c.method === 'POST').body,
  ).not.toHaveProperty('mode');
  await expect(page.locator('.session-card')).toContainText('TCP 被动');
  await expect
    .poll(() => state.calls.filter((c) => c.path.endsWith('/renew')).length)
    .toBeGreaterThan(0);
  await page.getByRole('button', { name: '继续选择设备' }).click();
  await expandPlatforms(page);
  await page
    .locator('.resource-tree-row')
    .filter({ hasText: '园区西门' })
    .getByRole('button', { name: '点播', exact: true })
    .click();
  await expect(page.locator('.session-card')).toHaveCount(2);
  await expect(page.locator('video, iframe')).toHaveCount(0);
  await page.reload();
  await expect(page.getByRole('dialog', { name: '视频预览' })).toBeVisible();
  await expect(page.locator('.session-card')).toHaveCount(2);
  expect(state.calls.filter((c) => c.path === '/playbacks' && c.method === 'POST')).toHaveLength(2);
  await page
    .locator('.session-card')
    .filter({ hasText: '园区东门' })
    .getByRole('button', { name: '关闭预览' })
    .click();
  await expect(page.locator('.session-card').filter({ hasText: '园区东门' })).toHaveCount(0);
  await expect(page.locator('.session-card').filter({ hasText: '园区西门' })).toContainText(
    '媒体就绪',
  );
  await page
    .locator('.session-card')
    .filter({ hasText: '园区西门' })
    .getByRole('button', { name: '关闭预览' })
    .click();
  await expect(page.getByRole('dialog', { name: '视频预览' })).toHaveCount(0);
  await page.evaluate(() => window.scrollTo(0, 0));
  await page.screenshot({
    path: 'test-results/playbacks-desktop.png',
    fullPage: true,
  });
});

test('lazy resource expansion, cached collapse, filtering and small screen layout', async ({
  page,
}) => {
  const state = await mockApi(page);
  state.resources = Array.from({ length: 25 }, (_, i) => ({
    ...camera,
    id: `camera_${i}`,
    name: `摄像头 ${i}`,
  }));
  await connect(page);
  await expandPlatforms(page);
  await expect(page.locator('.resource-tree-row')).toHaveCount(25);
  await page.getByRole('button', { name: '收起平台 园区平台' }).click();
  await expect(page.locator('.resource-tree-row')).toHaveCount(0);
  await expandPlatforms(page);
  await expect(page.locator('.resource-tree-row')).toHaveCount(25);
  await page.getByLabel('搜索资源').fill('摄像头 0');
  await page.getByRole('button', { name: '搜索', exact: true }).click();
  await expect(page.locator('.resource-tree-row')).toHaveCount(1);
  expect(state.calls.at(-1).search).toContain('offset=0');
  await page.setViewportSize({ width: 390, height: 844 });
  await expect(page.getByRole('heading', { name: '下级与资源', exact: true })).toBeVisible();
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(
    true,
  );
  await page.screenshot({
    path: 'test-results/resources-mobile.png',
    fullPage: true,
  });
});

test('failed catalog and playback show backend errors, disconnect clears token', async ({
  page,
}) => {
  const state = await mockApi(page);
  state.jobFails = true;
  state.failMedia = true;
  await connect(page);
  await page.getByRole('button', { name: '同步目录', exact: true }).click();
  await expect(page.locator('.job-row')).toContainText('目录接收不完整', { timeout: 12000 });
  await expect(page.locator('.job-row')).toContainText('catalog_trace');
  await expect(page.locator('.job-row')).toHaveCount(1);
  await page.getByRole('button', { name: '关闭同步窗口' }).click();
  await expandPlatforms(page);
  await page
    .locator('.resource-tree-row')
    .filter({ hasText: '园区东门' })
    .getByRole('button', { name: '点播', exact: true })
    .click();
  await expect(page.locator('.session-error')).toContainText('4003');
  await expect(page.locator('.session-error')).toContainText('media_trace');
  await page.getByRole('button', { name: '继续选择设备' }).click();
  await page.getByRole('button', { name: '连接设置', exact: false }).click();
  await page.getByRole('button', { name: '断开并释放本页点播' }).click();
  await expect(page.getByLabel('API 令牌', { exact: true })).toHaveValue('');
  expect(await page.evaluate(() => JSON.parse(sessionStorage.getItem('gb.connection')).token)).toBe(
    '',
  );
});

test('nested platform tree isolates duplicate GB codes and preserves matching ancestors', async ({
  page,
}) => {
  const state = await mockApi(page);
  state.platforms.push({
    ...state.platforms[0],
    id: 'lower_2',
    name: '第二平台',
    gb_id: '34020000002000000003',
  });
  state.resources = [
    { ...camera, id: 'group', gb_id: 'group_gb', name: '园区分组', parental: true },
    {
      ...camera,
      id: 'nested',
      gb_id: 'nested_gb',
      parent_gb_id: 'group_gb',
      name: '一号楼',
      parental: true,
    },
    { ...camera, id: 'first_cam', parent_gb_id: 'nested_gb', name: '一楼摄像头' },
    { ...camera, id: 'second_cam', platform_id: 'lower_2', name: '第二平台同编码摄像头' },
  ];
  await connect(page);
  await expect(page.getByRole('heading', { name: '操作记录' })).toHaveCount(0);
  await expect(page.locator('.resource-tree-row')).toHaveCount(0);
  expect(state.calls.filter((call) => call.path === '/resources')).toHaveLength(0);
  await page.getByRole('button', { name: '展开平台 园区平台' }).click();
  await expect(page.locator('.resource-tree-row')).toHaveCount(1);
  await page.getByRole('button', { name: '展开目录 园区分组' }).click();
  await expect(page.locator('.resource-tree-row')).toHaveCount(2);
  await page.getByRole('button', { name: '展开目录 一号楼' }).click();
  await expect(page.getByRole('treeitem', { name: '一楼摄像头', exact: true })).toHaveAttribute(
    'aria-level',
    '4',
  );
  await page.getByRole('button', { name: '展开平台 第二平台' }).click();
  await expect(
    page.getByRole('treeitem', { name: '第二平台同编码摄像头', exact: true }),
  ).toBeVisible();
  await page.getByRole('button', { name: '收起目录 园区分组' }).click();
  await page.getByLabel('搜索资源').fill('一楼');
  await page.getByRole('button', { name: '搜索', exact: true }).click();
  await expect(page.getByRole('treeitem', { name: '一楼摄像头', exact: true })).toBeVisible();
  await expect(page.getByRole('treeitem', { name: '园区分组', exact: true })).toBeVisible();
  await expect(
    page.getByRole('treeitem', { name: '第二平台同编码摄像头', exact: true }),
  ).toHaveCount(0);
  await page.evaluate(() => window.scrollTo(0, 0));
  await page.screenshot({ path: 'test-results/resource-tree-desktop.png', fullPage: true });
});
