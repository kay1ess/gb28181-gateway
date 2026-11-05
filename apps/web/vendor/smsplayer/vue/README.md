# Vue 3 组件

`SmsPlayer` 在已有播放核心外封装 Vue 生命周期、响应式属性和事件。默认使用现有玻璃 UI，也支持关闭默认 UI、替换 UI 工厂或使用 Vue 插槽编写控件；核心、UI、Vue 适配层保持独立。

支持 Vue 3.3+，提供 TypeScript 声明。组件在 mounted 后加载播放器，不在模块导入时读取 DOM，支持 SSR。Vue 2 不在本组件支持范围内。

## 安装与静态资源

目前使用本地包集成，不需要发布到 npm。先在本仓库根目录构建并打包：

```powershell
npm ci
npm run build:debug
npm run build
New-Item -ItemType Directory -Force .tools/releases | Out-Null
npm pack --pack-destination .tools/releases
```

在 Vue 项目中安装生成的 tarball，例如 Vue 项目与本仓库同级：

```powershell
npm install ../h265player/.tools/releases/smsplayer-1.3.0.tgz
node node_modules/smsplayer/vue/copy-assets.cjs public/smsplayer
```

Vue 由前端应用提供，组件不内置另一份 Vue。复制脚本会根据当前核心入口寻找 Worker 分块，同时复制核心、默认 UI、CSS、普通和 SIMD 两套解码物料。升级播放器后重新复制，并同步更新服务器缓存。

组件默认从 `/smsplayer/` 加载核心，开启 controls 时加载默认 UI 和 CSS；同页面多个实例共享下载。Worker/WASM 与核心保持同目录，避免让 Vite/Webpack 再次解析 FFmpeg 生成代码。无需额外手工导入播放器 CSS。

## 最小使用示例

```vue
<script setup lang="ts">
import { ref, shallowRef } from 'vue';
import SmsPlayer, { type SmsPlayerExpose, type SmsPlayerError } from 'smsplayer/vue';

const player = shallowRef<SmsPlayerExpose | null>(null);
const src = ref('/videos/demo.mp4');
const volume = ref(0.8);
const playbackRate = ref(1);
const assetsBase = import.meta.env.BASE_URL + 'smsplayer/';
const options = { cacheduration: 5, wasmSIMD: true };
const onError = (error: SmsPlayerError) => { console.error(error.message); };
</script>

<template>
  <SmsPlayer
    ref="player"
    :src="src"
    :assets-base="assetsBase"
    :options="options"
    :height="420"
    autoplay
    v-model:volume="volume"
    v-model:playback-rate="playbackRate"
    @error="onError"
  />
  <button @click="player?.pause()">暂停</button>
  <button @click="player?.play()">播放</button>
  <button @click="player?.seek(30)">跳到 30 秒</button>
</template>
```

`src` 支持绝对 HTTP(S) URL，也支持相对于页面 base URI 的地址。远端媒体仍须满足核心的 CORS / Range 要求。浏览器可能阻止自动播放，可使用 `muted` 或让用户点击播放；`autoplayblocked` 会正常转发。H.265 软件解码暂不支持音频。

子目录部署时，Vite 使用上例中的 `BASE_URL`；其他工程将 `assets-base` 设为实际静态目录，例如 `/my-app/smsplayer/`。服务器为 `.wasm` 设置 `application/wasm`，建议启用 gzip / Brotli。

也可以全局注册：

```js
import SmsPlayer from 'smsplayer/vue';
app.use(SmsPlayer);
```

## 属性

| 属性 | 默认值 | 行为 |
| --- | --- | --- |
| `src` | `''` | 动态切源；清空时 stop。可使用 `v-model:src` 接收 ref.load() 的来源更新 |
| `autoplay` | `false` | 就绪后尝试播放；动态设为 false 时暂停 |
| `controls` | `true` | 挂载默认 / 自定义 UI；切换时不重建核心、不重新加载视频 |
| `loop` | `false` | 播放结束后重新加载并播放；直播没有自然结束时不触发 |
| `volume` | `1` | 0–1，支持 `v-model:volume` |
| `muted` | `false` | 临时将音量设为 0，取消静音恢复偏好；支持 `v-model:muted` |
| `playbackRate` | `1` | 正数，支持 `v-model:playback-rate` |
| `width` | `'100%'` | 数字为 px，也接受 CSS 长度 |
| `height` | `360` | 媒体区域高度，数字为 px，也接受 CSS 长度 |
| `assetsBase` | `'/smsplayer/'` | 播放器静态资源目录 |
| `options` | `{}` | 扁平的核心播放配置，例如 cacheduration、maxDecodedBytes、wasmSIMD、hevcRenderer、loglevel |
| `uiFactory` | 默认 UI 工厂 | 实现已有 UI 协议的工厂；更换时只替换 UI |
| `progressInterval` | `100` | 进度状态发布间隔，毫秒；0 关闭合并，状态切换 / 错误等立即发布 |
| `nonce` | 未设置 | 自动插入 script/link 时使用的 CSP nonce |
| `runtime` | 自动加载 | 可注入已加载的 `{ core, ui }`，便于自托管或测试 |

`src`、音量、倍速、静音、尺寸及 UI 开关变化不会重建核心。`options` 的实际值、assetsBase、runtime 或 nonce 变化会释放旧实例并重新加载来源；相同值的内联 options 对象不会触发重建。options 中的 container / autoPlay 由组件管理，不作为组件配置入口。

## 事件与 ref 方法

- `created(player)`：实例创建后返回原始核心实例，未被 Vue 深层代理。
- `statechange(state)`：冻结的核心状态快照；进度默认最多约 10 次/秒，重要状态立即发布。
- `timeupdate(seconds)`：随已发布状态的当前时间变化通知，单位为秒。
- `error({ message, error, phase })`：phase 为 runtime、load、ui、playback 或 command。
- `ready / loadstart / play / pause / playing / waiting / autoplayblocked / ended / stop / destroy`：生命周期事件。
- `mediainfo / durationchange / cacheduration / volumechange / ratechange`：与核心相同的数据，时长类数据为秒。

ref 暴露 `play()`、`pause()`、`seek(seconds)`、`load(url)`、`reload()`、`stop()`、`setVolume(value)`、`setPlaybackRate(value)`、`snapshot()`、`debug()`、`getPlayer()`、`getState()`，以及只读 `state` / `error`。加载器尚未就绪时的 play() 意图会保留到 ready；seek() 应在点播就绪后调用。

将组件通过 v-if 卸载时会销毁核心、UI、监听和待发布计时器，不需要额外调用原始 player.destroy()。KeepAlive 停用时暂停，激活时恢复之前的播放意图；缓冲遵守核心预算，真正卸载时才销毁实例。已共享加载的 JS / CSS 保留供其他实例复用。

## 使用 Vue 控件替换默认 UI

```vue
<SmsPlayer :src="src" :controls="false" :assets-base="assetsBase">
  <template #default="{ state, play, pause, seek }">
    <div style="position:absolute;bottom:16px;left:16px;pointer-events:auto">
      <button @click="state?.status === 'playing' ? pause() : play()">
        {{ state?.status === 'playing' ? '暂停' : '播放' }}
      </button>
      <button :disabled="!state?.capabilities.seek" @click="seek(30)">跳到 30 秒</button>
    </div>
  </template>
</SmsPlayer>
```

默认插槽与 loading、error 插槽均接收 `state / error / player` 及上述控制方法；初始化期间 state / player 可以为 null。覆盖层默认不拦截画面操作，自定义按钮区域请设置 `pointer-events: auto`。框架原生控件的更新不应重建播放器实例。

如果使用 EventTarget 风格的现有自定义 UI，直接传 `:ui-factory="createMyUI"`；协议见仓库 AGENTS.md 的 UI 依赖边界说明。

## 已有运行时 / SSR

如果页面已经加载了核心和 UI，可以传入：

```js
const runtime = { core: window.playerjsCore, ui: window.playerjsUI };
// <SmsPlayer :runtime="runtime" :src="src" />
```

禁用 controls 时 runtime 只需要 core。自动加载方式会等待 script / CSS 完成，失败后可重试；多实例共用资源，卸载中的异步结果不会创建迟到的实例。也可调用导出的 `loadPlayerRuntime({ assetsBase, controls })` 提前加载。

SSR 可以直接导入 `smsplayer/vue`，组件只在客户端 mounted 后接触播放器；不要在服务端直接导入依赖浏览器的 UMD 根入口或读取 window 全局。

本地目录软链接调试时，如果前端和本仓库各自安装了 Vue，Vite 建议配置 `resolve: { dedupe: ['vue'] }`；安装 tarball 并使用前端已有 Vue 可避免重复运行时。

## 仓库示例与验证

在仓库根目录：

```powershell
npm run build
npm run dev:vue
```

访问 `http://localhost:9091/`。独立于原有 9090 演示服务；如果 vod 中存在 H.264 / H.265 测试文件，示例会自动复制并提供切换按钮，否则输入自己的媒体地址。

```powershell
npm test
npm run typecheck:vue
npm run build:vue-demo
node scripts/vue/verify-example.cjs
```

测试使用真实 Vue 与 jsdom，覆盖私有字段实例、动态属性、UI 替换、异步取消、多实例缓存、失败重试、循环、KeepAlive、卸载清理和 SSR；示例验证实际 Vite 子目录产物及 Worker / WASM 拷贝。浏览器媒体解码实播仍需单独验证。
