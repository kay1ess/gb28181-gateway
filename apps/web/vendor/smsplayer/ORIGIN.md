# 本地播放器快照

来自用户指定的 `D:/likai/h265player`，Vue 3 示例位于 `example/vue3`。2026-11-05 同步播放器 1.3.0 的预构建运行资源。

`vue/` 为原 Vue 适配器和资源复制脚本，`types/` 包含适配器使用的运行时加载器和类型定义。`dist/` 包含核心、UI、CSS、Worker 分块及普通和 SIMD 解码物料，附带 CSS source map。`upstream-files.json` 记录同步文件的 SHA-256 和字节数。本地 package.json 声明 Vue peer dependency，原 package.json 保存在 upstream-package.json。

上游播放器声明 ISC 许可。WASM 内含 FFmpeg 5.1.6，相关许可文本保存在 licenses/。本项目的播放协议和生命周期适配在 src/components/PlayerSlot.vue 中实现。
