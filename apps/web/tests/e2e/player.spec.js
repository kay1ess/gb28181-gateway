import { expandPlatforms } from './tree_helpers.js';
import { test, expect } from '@playwright/test';
import { readFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { resolve } from 'node:path';

for (const codec of ['h264', 'h265']) {
  test(`copied player renders ${codec} with local runtime and releases media`, async ({ page }) => {
    const fixture = resolve(
      process.cwd(),
      '../../.bazel-output/player-fixtures',
      `${codec}.${codec === 'h264' ? 'flv' : 'mp4'}`,
    );
    test.skip(
      !existsSync(fixture),
      'Prepare the optional local player fixture in .bazel-output/player-fixtures',
    );
    const bytes = await readFile(fixture);
    const loaded = [];
    page.on('response', (response) => {
      if (response.url().includes('/smsplayer/'))
        loaded.push({ url: response.url(), status: response.status() });
    });
    await page.route('**/media/rtp/sample.live.flv', async (route) => {
      const request = route.request();
      const range = request.headers().range?.match(/bytes=(\d+)-(\d*)/);
      const start = range ? Number(range[1]) : 0;
      const end =
        range && range[2] ? Math.min(Number(range[2]), bytes.length - 1) : bytes.length - 1;
      await route.fulfill({
        status: range ? 206 : 200,
        headers: {
          'Content-Type': codec === 'h264' ? 'video/x-flv' : 'video/mp4',
          'Content-Length': String(end - start + 1),
          'Accept-Ranges': 'bytes',
          ...(range ? { 'Content-Range': `bytes ${start}-${end}/${bytes.length}` } : {}),
        },
        body: request.method() === 'HEAD' ? undefined : bytes.subarray(start, end + 1),
      });
    });
    const resource = {
      id: 'sample',
      platform_id: 'fixture',
      name: `${codec} 实播样例`,
      gb_id: '34020000001320000001',
      state: 'online',
    };
    await page.route('**/api/**', async (route) => {
      const path = new URL(route.request().url()).pathname;
      const method = route.request().method();
      let data;
      if (path === '/api/health')
        data = {
          online_platforms: 1,
          active_receivers: 0,
          active_playbacks: 0,
          playback_leases: 0,
        };
      else if (path === '/api/platforms')
        data = [{ id: 'fixture', name: '播放器样例', state: 'online', enabled: true }];
      else if (path === '/api/resources') data = [resource];
      else if (path === '/api/playbacks' && method === 'POST')
        data = { id: 'lease', playback_id: 'play', expires_ms: Date.now() + 60000 };
      else if (path === '/api/playbacks/play' || path === '/api/playbacks/play/address')
        data = {
          id: 'play',
          state: 'playing',
          mode: 'udp',
          idle_timeout_seconds: 60,
          endpoints: [{ protocol: 'http-flv', url: 'http://127.0.0.1:8081/rtp/sample.live.flv' }],
        };
      else if (path.startsWith('/api/playback-leases/')) data = null;
      else throw new Error(`Unexpected player fixture API ${path}`);
      await route.fulfill({ json: { code: 0, data } });
    });
    await page.goto('/');
    await page.getByLabel('API 令牌', { exact: true }).fill('player-fixture-token');
    await page.getByRole('button', { name: '连接服务', exact: true }).click();
    await expandPlatforms(page);
    await page.getByRole('button', { name: '点播', exact: true }).click();
    if (codec === 'h264') {
      await expect(page.locator('video')).toHaveCount(1);
      await expect
        .poll(
          () =>
            page
              .locator('video')
              .evaluate(
                (video) =>
                  video.videoWidth > 0 &&
                  video.currentTime > 0.3 &&
                  video.getVideoPlaybackQuality().totalVideoFrames > 3,
              ),
          { timeout: 20000 },
        )
        .toBe(true);
    } else {
      const canvas = page.locator('.sms-vue-player__media canvas');
      await expect(canvas).toHaveCount(1, { timeout: 20000 });
      await expect
        .poll(
          () =>
            canvas.evaluate((canvas) => {
              if (!canvas.width || !canvas.height) return false;
              const pixels = canvas
                .getContext('2d')
                .getImageData(0, 0, canvas.width, canvas.height).data;
              let colored = 0;
              for (let index = 0; index < pixels.length; index += 4)
                if (pixels[index] > 25 || pixels[index + 1] > 25 || pixels[index + 2] > 25)
                  colored++;
              return colored > (canvas.width * canvas.height) / 10;
            }),
          { timeout: 20000 },
        )
        .toBe(true);
      expect(loaded.some((item) => item.url.endsWith('.wasm') && item.status === 200)).toBe(true);
      expect(
        loaded.some((item) => item.url.includes('333.smsplayer.min.js') && item.status === 200),
      ).toBe(true);
    }
    await page.screenshot({ path: `test-results/player-${codec}.png`, fullPage: true });
    if (codec === 'h265') await page.keyboard.press('Escape');
    else await page.getByRole('button', { name: '关闭播放窗口' }).click();
    await expect(page.getByRole('dialog', { name: '视频预览' })).toHaveCount(0);
    await expect(page.locator('video, canvas')).toHaveCount(0);
    expect(loaded.every((item) => item.status < 400)).toBe(true);
  });
}
