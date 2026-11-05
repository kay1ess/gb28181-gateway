# 更新记录

## 1.3.0 — 2026-11-05

- H.265 + AAC 可通过 WebCodecs 与 AudioContext 输出声音。视频按音频时钟同步，并支持暂停、倍速、音量及可用的定位路径。

- 支持 HTTP(S) Enhanced FLV 的单轨 HEVC（`hvc1`），沿用现有 Worker/WASM 解码和渲染链路。

- 为项目主入口 `smsplayer` 和独立核心 `smsplayer/core` 增加 TypeScript 声明，并提供适合前端打包器的 `smsplayer/ts` 异步入口。Vue 组件复用项目级播放器类型。

- H.265 默认渲染模式改为 WebGL，初始化失败自动回退 Canvas2D；仍支持显式选择 Canvas2D 或 Offscreen。Vue 示例跟随核心默认配置。

## 1.2.0 — 2026-11-04

### 新增与改进

- 提供 Vue 3.3+ 组件 `smsplayer/vue`，包含 TypeScript 类型、响应式属性、事件、组件 ref 方法、SSR 导入和 KeepAlive 生命周期支持。
- 提供独立核心、独立默认 UI 和组合入口；通过公开 API、状态事件与外层绑定替换控件，保持播放进度与核心实例。
- H.265 增加普通 / SIMD 两套 WASM，按能力选择并支持初始化回退；Canvas2D 为默认模式，WebGL、Offscreen 为可选模式。
- 减少像素缓冲分配与复制，限制解码队列及网络缓冲；MSE 合批追加，UI 合并进度更新。定位时保留参考帧解码并省略目标前的像素输出。
- 以实际视频颜色空间、范围配置转换；8 位 YUV420 可走 GPU，10 位及 HDR 输入回退 RGBA。
- 补充 WASM 工具链、Vue 集成、浏览器性能对比及升级说明。正式包版本统一为 `1.2.0`，同时校验 API、状态和播放信息面板。

### 修复

- 修复 H.264 / AAC 共享时间原点、首段缓冲及任意位置定位时的 fMP4 时间戳问题。
- 修复 WASM send / receive、EAGAIN 重试、尾帧排空、关闭重开及毫秒时间戳传递；使用 ABI 2。
- 修复 Worker 在子目录部署时的 WASM 加载路径，并提供静态资源复制工具。
- 增强暂停恢复、资源回收、图形上下文初始化失败及丢失后的错误处理。

### 升级与验证

升级时需同时替换播放器、UI / CSS、当前 Worker 分块与普通 / SIMD 解码物料，避免混用旧 ABI；操作步骤见 [升级指南](UPGRADING.md)。

本机 Chrome 156 / RTX 3080 Ti 的三轮合成样本对照中，1080p WebGL CPU 时间减少约 17%，4K Offscreen 减少约 38%，4K 私有内存峰值增加约 9%。这是固定条件下的结果，详细口径见 [浏览器测试说明](https://gitee.com/wolflikai/h265player/blob/v1.2.0/scripts/performance/browser/README.md)。GPU 路径与 Canvas2D 在色块边缘存在采样差异。

H.265 软件解码路径仍不播放音频；本次未增加 PCM / G.711 音频播放或 HDR 色调映射。实际浏览器支持取决于所选播放链路。
