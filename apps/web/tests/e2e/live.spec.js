import { expandPlatforms } from './tree_helpers.js';
import { test, expect } from '@playwright/test';
import { spawn, execFile } from 'node:child_process';
import { mkdir, writeFile } from 'node:fs/promises';
import { openSync, closeSync } from 'node:fs';
import { resolve } from 'node:path';
import { promisify } from 'node:util';

test('real C++ server + independent Go cascade: register, catalog, two leases and decode', async ({
  page,
}) => {
  test.skip(
    process.env.GB_LIVE_E2E !== '1',
    'Set GB_LIVE_E2E=1 with built server and prepared Go fixture',
  );
  test.setTimeout(120000);
  const root = resolve(process.cwd(), '../..');
  const directory = resolve('test-results', `live-${Date.now()}`);
  await mkdir(directory, { recursive: true });
  const token = 'frontend-live-test-token';
  const configuration = {
    database_path: resolve(directory, 'server.db'),
    bind_address: '127.0.0.1',
    advertised_address: '127.0.0.1',
    sip_port: 32160,
    http_port: 32180,
    rtp_port: 32100,
    media_http_port: 32181,
    media_rtsp_port: 0,
    api_token: token,
  };
  const configPath = resolve(directory, 'server.json');
  await writeFile(configPath, JSON.stringify(configuration));
  const child = (file, args, name) => {
    const fd = openSync(resolve(directory, `${name}.log`), 'w');
    const process = spawn(file, args, {
      cwd: root,
      windowsHide: true,
      stdio: ['ignore', fd, fd],
    });
    closeSync(fd);
    process.on('error', () => {});
    return process;
  };
  const server = child(
    process.env.GB_SERVER || resolve(root, 'bazel-bin/apps/server/server.exe'),
    [configPath],
    'server',
  );
  let peer;
  const api = async (path) => {
    const response = await fetch(`http://127.0.0.1:32180/api${path}`, {
      headers: { Authorization: `Bearer ${token}` },
    });
    if (!response.ok) throw new Error(`Live backend ${path}: ${response.status}`);
    return (await response.json()).data;
  };
  try {
    await expect
      .poll(
        async () => {
          try {
            await api('/health');
            return true;
          } catch {
            return false;
          }
        },
        { timeout: 20000 },
      )
      .toBe(true);
    await page.goto('/');
    await page.getByLabel('API 令牌', { exact: true }).fill(token);
    await page.getByRole('button', { name: '连接服务', exact: true }).click();
    await expect(page.getByRole('dialog')).toHaveCount(0);
    await page.getByRole('button', { name: '添加下级', exact: false }).click();
    const form = page.getByRole('dialog');
    await form.getByLabel('平台名称', { exact: true }).fill('独立 Go 下级');
    await form.getByLabel('国标编码', { exact: true }).fill('34020000002000000002');
    await form.getByLabel('认证密码', { exact: true }).fill('test-only');
    await form.getByRole('button', { name: '保存平台', exact: true }).click();
    await expect(page.getByRole('dialog')).toHaveCount(0);
    peer = child(
      process.env.GB_SIMULATOR || resolve(root, '.bazel-output/tools/gb28181-go-device.exe'),
      [
        '-role',
        'cascade',
        '-server',
        '127.0.0.1:32160',
        '-media',
        process.env.GB_SAMPLE || resolve(root, '.bazel-output/tools/sample.h264'),
      ],
      'peer',
    );
    const platform = page.locator('.platform-tree-row').filter({ hasText: '独立 Go 下级' });
    await expect(platform.getByText('在线', { exact: true })).toBeVisible({
      timeout: 20000,
    });
    await page.screenshot({
      path: 'test-results/live-platforms-desktop.png',
      fullPage: true,
    });
    await platform.getByRole('button', { name: '同步目录', exact: true }).click();
    await expect(page.getByText('已完成', { exact: true })).toBeVisible({
      timeout: 20000,
    });
    await page.getByRole('button', { name: '关闭', exact: true }).click();
    await expandPlatforms(page);
    await expect(page.locator('.resource-tree-row')).toHaveCount(2);
    await page.screenshot({
      path: 'test-results/live-resources-desktop.png',
      fullPage: true,
    });
    await page
      .locator('.resource-tree-row')
      .first()
      .getByRole('button', { name: '点播', exact: true })
      .click();
    await expect(page.locator('.session-card .badge')).toHaveText('媒体就绪', {
      timeout: 20000,
    });
    await page.getByRole('button', { name: '继续选择设备' }).click();
    await expandPlatforms(page);
    await page
      .locator('.resource-tree-row')
      .nth(1)
      .getByRole('button', { name: '点播', exact: true })
      .click();
    await expect(page.locator('.session-card')).toHaveCount(2);
    await expect(page.locator('.session-card .badge').first()).toHaveText('媒体就绪', {
      timeout: 20000,
    });
    await expect(page.locator('video')).toHaveCount(2);
    await expect
      .poll(
        async () =>
          page
            .locator('video')
            .evaluateAll((videos) =>
              videos.every(
                (video) =>
                  video.videoWidth > 0 &&
                  video.currentTime > 0.3 &&
                  video.readyState >= 2 &&
                  video.getVideoPlaybackQuality().totalVideoFrames > 3,
              ),
            ),
        { timeout: 20000 },
      )
      .toBe(true);
    for (const address of (await page.locator('.endpoint code').allTextContents()).filter(
      (address) => address.startsWith('rtsp://'),
    )) {
      await promisify(execFile)(
        process.env.GB_FFMPEG || 'C:/Program Files/ffmpeg/ffmpeg.exe',
        [
          '-nostdin',
          '-hide_banner',
          '-loglevel',
          'error',
          '-rtsp_transport',
          'tcp',
          '-i',
          address,
          '-frames:v',
          '1',
          '-f',
          'null',
          '-',
        ],
        { timeout: 15000, windowsHide: true },
      );
    }
    await page.screenshot({
      path: 'test-results/live-playbacks-desktop.png',
      fullPage: true,
    });
    await page.locator('.session-card').first().getByRole('button', { name: '关闭预览' }).click();
    await expect.poll(async () => (await api('/health')).active_receivers).toBe(1);
    await expect(page.locator('video')).toHaveCount(1);
    const previousTime = await page.locator('video').evaluate((video) => video.currentTime);
    await expect
      .poll(() => page.locator('video').evaluate((video) => video.currentTime), { timeout: 8000 })
      .toBeGreaterThan(previousTime + 0.5);
    await page.locator('.session-card').first().getByRole('button', { name: '关闭预览' }).click();
    await expect
      .poll(
        async () => {
          const state = await api('/health');
          return [
            state.active_playbacks,
            state.active_receivers,
            state.playback_leases,
            state.sip_sessions,
            state.sip_requests,
          ];
        },
        { timeout: 10000 },
      )
      .toEqual([0, 0, 0, 0, 0]);
    await expect(page.locator('video, canvas')).toHaveCount(0);
  } finally {
    peer?.kill();
    server.kill();
  }
});
