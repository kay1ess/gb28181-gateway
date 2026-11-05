# 升级与发布指南

## 从 1.2.0 升级到 1.3.0

- H.265 默认使用 WebGL，初始化失败时回退 Canvas2D。可通过 `hevcRenderer: 'canvas2d'` 显式选择 Canvas2D。
- HTTP(S) Enhanced FLV 会自动识别单轨 HEVC（`hvc1`）。
- H.265 + AAC 使用 WebCodecs 解码和 AudioContext 输出。浏览器需要支持对应的 AAC 配置，必要时点击播放按钮解锁声音。
- `smsplayer` 和 `smsplayer/core` 提供 TypeScript 声明。`smsplayer/ts` 可异步加载静态资源。

部署时同步替换播放器、UI、Worker 分块及普通和 SIMD 两套 WASM。通过安装 `.tools/releases/smsplayer-1.3.0.tgz` 升级本地依赖，使用 Vue 或异步入口的项目需重新运行 `npx smsplayer-assets public/smsplayer`。刷新后检查 `player.version` 与播放信息面板均显示 `1.3.0`。

## 从 1.1.3 升级到 1.2.0

### 替换完整播放物料

保留原有组合入口 `playerjs.createPlayerInstance(config)` 的工程，可继续使用 `smsplayer.min.js` 与 `player.css`。自定义界面可使用 `smsplayer.core.min.js`；默认 UI 通过 `smsplayer.ui.min.js`、`player.ui.css` 和 `playerjsUI.mountUI()` 单独挂载。

部署时替换整个本次构建的物料集合，并更新 CDN / 浏览器缓存：

- 组合版或独立核心 / UI 的 JS 与对应 CSS。
- 当前入口引用的 `*.smsplayer.min.js` Worker 分块；分块名以实际构建为准。
- `265decoder.js`、`265decoder.wasm`、`265decoder.simd.js`、`265decoder.simd.wasm`。

1.2.0 的 C / WASM 接口为 ABI 2。旧 Worker 与新解码器、旧解码器与新播放器均不应混用。Worker / WASM 放在核心文件同目录，服务器保留跨域、Range 与 `application/wasm` 配置。

### Vue 3 工程

Vue 需为 3.3 或更高版本。当前按本地 tarball 集成；以下命令不假定 1.2.0 已发布到 npm：

```powershell
# 前端工程与播放器仓库同级时
npm install ../h265player/.tools/releases/smsplayer-1.2.0.tgz
node node_modules/smsplayer/vue/copy-assets.cjs public/smsplayer
```

组件用法见 [Vue 组件文档](vue/README.md)。升级包后重新执行静态资源复制；部署在子目录时同步设置 `assetsBase`。不要仅替换 Vue 适配器而继续使用服务器缓存的旧核心或 WASM。

### 默认行为与可选模式

- H.264 继续使用 MSE；H.265 使用 Worker / WASM 软件解码。
- `wasmSIMD` 默认开启，失败回退普通版；`wasmSIMD: false` 可强制普通版。
- `hevcRenderer` 默认 `canvas2d`；按目标设备需求选择 `webgl` 或 `offscreen`。GPU 负责颜色转换和绘制，HEVC 解码仍在 WASM 内执行。
- 进度、时长、缓冲与定位 API 使用秒。UI 通过只读状态和公开播放 API 绑定；运行中替换 UI 无需重建播放器。
- H.265 路径仍不支持音频；10 位 / HDR 回退 RGBA，未提供 HDR 色调映射。GPU 纹理采样与默认颜色转换在部分边缘像素存在差异。

升级后检查：播放 / 暂停、定位、重新加载、流结束、截图、控件替换、子目录资源地址及控制台错误。确认 `player.version`、`player.state.version` 和“播放信息”均为 `1.2.0`。

## 构建、打包与版本来源

`package.json` 的 `version` 是唯一的构建版本来源。Webpack 的 `DefinePlugin` 将它写入核心与 UI 的 `__VERSION__`；tag 本身不会修改代码或生成版本号。

在仓库根目录执行：

```powershell
npm ci
npm test
npm run typecheck:vue
npm run build:debug
npm run verify:bundles
npm run verify:release -- --tag=v1.3.0
npm run test:wasm:bundles
New-Item -ItemType Directory -Force .tools/releases | Out-Null
npm pack --pack-destination .tools/releases
```

`verify:bundles` 构建生产包并检查核心 / UI / Vue 的依赖边界；`verify:release` 实例化开发、生产入口，检查公开 API、状态与实际信息面板的版本，并检查两套 WASM。`npm pack` 的 `prepack` 也会执行版本校验，防止打包旧版本产物。只改 JS 版本无需重编 C；修改 C 或 FFmpeg 后另按 `src/decoder/wasm/readme` 重编 WASM。
