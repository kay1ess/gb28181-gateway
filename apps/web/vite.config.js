import { defineConfig, loadEnv } from 'vite';
import vue from '@vitejs/plugin-vue';
import { fileURLToPath } from 'node:url';
import { copyFileSync } from 'node:fs';
import { createRequire } from 'node:module';

const assets = createRequire(import.meta.url)('./vendor/smsplayer/vue/copy-assets.cjs');

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), 'GB_');
  const proxy = {
    '/api': {
      target: process.env.GB_API_TARGET || env.GB_API_TARGET || 'http://127.0.0.1:8080',
      changeOrigin: true,
      ws: true,
    },
    '/media/rtp/': {
      target: process.env.GB_MEDIA_TARGET || env.GB_MEDIA_TARGET || 'http://127.0.0.1:8081',
      changeOrigin: true,
      rewrite: (path) => path.replace(/^\/media/, ''),
      configure(proxy) {
        // Media is independent of the business API. Never forward its token.
        proxy.on('proxyReq', (request) => request.removeHeader('authorization'));
      },
    },
  };
  return {
    plugins: [
      vue(),
      {
        name: 'local-smsplayer-assets',
        configResolved(config) {
          assets.copyPlayerAssets(`${config.publicDir}/smsplayer`);
          copyFileSync(
            fileURLToPath(new URL('./vendor/smsplayer/dist/player.ui.css.map', import.meta.url)),
            `${config.publicDir}/smsplayer/player.ui.css.map`,
          );
        },
      },
    ],
    server: { port: 5173, strictPort: true, proxy },
    preview: { port: 4173, strictPort: true, proxy },
  };
});
