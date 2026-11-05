# smsplayer.js

## 版本

### v1.0.0

- createPlayerInstance，创建实例
- load，加载并播放
- play，播放
- pause，暂停
- reload，重新加载
- debug，下载日志文件
- snapshot，截图并保存为png
- volume，设置音量，n为0-1的小数
- stop，停止
- destroy，销毁实例，移除播放器自身创建的内容
- version，获取版本号

### v1.1.0

- playbackRate，设置或获取播放倍数
- currentTime，获取当前播放进度（秒）
- duration，获取视频总时长（秒）
- seek，仅点播，跳转到指定时间（秒）

### 路线

- [ ] 优化加载：记录已加载数据，seek时按需加载
- [ ] 优化H265花屏问题：对于含B帧的视频，解析帧的依赖关系，不渲染缺失依赖的帧（但要提交给解码器）
- [ ] 支持PCM解析和播放

## 编译

```sh
npm run build
```

### 输出物料

| 文件 | 用途 |
| --- | --- |
| `smsplayer.min.js` + `player.css` | 默认组合版，保留 `playerjs.createPlayerInstance()` 用法 |
| `smsplayer.core.min.js` | 独立播放核心，暴露 `playerjsCore`，不含 UI、图标或 CSS |
| `smsplayer.ui.min.js` + `player.ui.css` | 独立默认 UI 与绑定层，暴露 `playerjsUI`，不含播放引擎 |
| `265decoder.js/.wasm`、`265decoder.simd.js/.wasm`、`[id].smsplayer.min.js` | HEVC 解码及 Worker 物料，按构建输出一起部署 |

`npm run build:core` 仅构建核心，`npm run build:ui` 仅构建默认 UI。
`npm run build:debug` 输出对应的不带 `.min` 的开发版本。
`npm run verify:bundles` 构建生产版本并检查核心/UI 的实际模块依赖边界。

### WASM 解码器

Windows 首次运行 `npm run setup:wasm` 安装项目内 Emscripten 3.1.64（需要
PowerShell 7 和 Git for Windows），再运行 `npm run build:wasm:deps` 重建
FFmpeg 5.1.6 配套库。只修改 C 时执行 `npm run build:wasm`，然后重新构建 JS。
普通 `npm run build` 不会编译 C；普通 / SIMD 两套 `265decoder*.js` 与 `.wasm` 必须同步部署。
完整流程见 [解码器说明](src/decoder/wasm/readme)。

`npm run test:wasm -- vod/ui-check-h265.mp4` 验证真实解码器的完整帧数、时间戳与
20 次重开；`npm run test:wasm:player` 验证真实 Worker 的暂停恢复和尾帧结束流程
（Canvas 为模型）。测试媒体为本地 HEVC MP4，不随仓库分发。

## 运行

```sh
npm run dev
```

[player.html](http://localhost:9090/player.html)

[ui-switch.html](http://localhost:9090/ui-switch.html) 演示同一播放实例运行中切换玻璃控件、简洁控件或隐藏控件，不重新加载视频，也支持 `playurl`。

可通过 `playurl` 查询参数预填视频地址并自动加载播放。视频地址需使用
`encodeURIComponent` 编码，避免地址中的 `&`、`+` 等字符被当作页面参数解析：

```js
const url = 'https://example.com/live.flv?token=abc&quality=high';
const demoUrl = 'http://localhost:9090/player.html?playurl=' + encodeURIComponent(url);
```

未提供 `playurl` 或参数为空时，保持原有行为，手动输入地址播放。

测试页支持点击“加载播放”或回车加载。播放区域内可使用空格播放/暂停、
`M` 静音、`F` 全屏；按钮支持键盘焦点及操作提示。浏览器阻止自动播放时，
点击中央播放按钮即可继续。H.265 的 AAC 音轨由 WebCodecs 解码，再由 AudioContext 输出。视频以音频输出时间为主时钟。浏览器需要支持对应的 AAC 解码配置，否则继续播放画面并禁用音量控件。

## 测试

```sh
npm test
```

使用 Node.js 内置测试运行器，覆盖自动播放恢复、下载完成后的暂停/恢复、取消请求、
资源销毁、音频配置先到的 FLV 初始化、直播时间戳封装、共同缓冲起点和日志导出。
另外覆盖无 UI 核心、状态快照、旧入口兼容，以及替换/移除 UI 时的事件清理。
画面、解码与响应式布局仍需在浏览器中验证。

版本更新见 [更新记录](CHANGELOG.md)，从旧版迁移、完整资源替换及 tag 发布流程见 [升级指南](UPGRADING.md)。

## 示例

性能测量脚本保留在 `scripts/performance/`，结果默认输出到被忽略的 `.tools/performance/`。测量方式、复跑命令与限制见 [AGENTS.md](AGENTS.md#性能改造与对比记录)。

### Enhanced FLV

HTTP(S) Enhanced FLV 支持单轨 HEVC（`hvc1`），直接将流地址传给 `player.load(url)`。视频复用现有 Worker/WASM 解码及 WebGL / Canvas2D 渲染。当前支持 HEVC 扩展视频头，扩展音频、其他 FourCC 和多轨封装需另行适配。

### 普通 TypeScript 项目

项目主入口 `smsplayer` 和独立核心入口 `smsplayer/core` 都提供类型声明，可继续使用原有的同步创建 API。

```ts
import smsplayer, { type PlayerConfig } from 'smsplayer';

const config: PlayerConfig = { divID: 'player' };
const player = smsplayer.createPlayerInstance(config);
player.addEventListener('statechange', ({ detail }) => {
    console.log(detail.status, detail.currentTime);
});
```

直接使用同步入口时，按原有部署方式提供构建产物、Worker 与 WASM。前端打包器项目也可使用下方的 `smsplayer/ts` 异步入口，从静态目录加载预构建资源。

安装包含 TypeScript 入口的本项目包后，将运行资源复制到前端静态目录。

```sh
npx smsplayer-assets public/smsplayer
```

```ts
import { createPlayer } from 'smsplayer/ts';

const container = document.querySelector<HTMLElement>('#player');
if (!container) throw new Error('找不到播放器容器');

const player = await createPlayer({ container, assetsBase: '/smsplayer/' });
player.addEventListener('statechange', event => {
    console.log(event.detail.status, event.detail.currentTime);
});
player.load(new URL('/media/demo.mp4', document.baseURI).href);

// 容器卸载时调用 player.destroy()
```

请为容器设置尺寸。创建函数会异步加载预构建的浏览器资源，支持 `controls: false` 仅加载核心，或通过 `uiFactory` 替换默认控件。子目录部署时将 `assetsBase` 指向复制后的资源目录。模块可在服务端导入，创建播放器须在浏览器中执行。

可从同一入口导入 `PlayerInstance`、`PlayerState`、`PlayerEventMap` 和 `CreatePlayerOptions`，用于业务代码的类型标注。

### Vue 3 组件

已提供独立的 `smsplayer/vue` 入口，支持动态切源、音量 / 倍速模型绑定、组件 ref、默认 UI / 自定义插槽、KeepAlive 与卸载清理，并附 TypeScript 声明。

```vue
<script setup>
import SmsPlayer from 'smsplayer/vue';
const assetsBase = import.meta.env.BASE_URL + 'smsplayer/';
</script>

<template>
  <SmsPlayer src="/videos/demo.mp4" :assets-base="assetsBase" :height="420" autoplay />
</template>
```

先构建播放器并安装本地 tarball，再将运行时复制到 Vue 项目的 public 目录：

```powershell
# 本仓库
npm run build:debug
npm run build
New-Item -ItemType Directory -Force .tools/releases | Out-Null
npm pack --pack-destination .tools/releases
# Vue 项目中，先安装生成的 smsplayer-1.3.0.tgz，再执行：
node node_modules/smsplayer/vue/copy-assets.cjs public/smsplayer
```

完整安装、属性、事件与自定义 UI 用法见 [Vue 组件文档](vue/README.md)。仓库内执行 `npm run dev:vue` 打开 9091 端口示例；`npm run typecheck:vue` 验证公开类型，`npm run build:vue-demo` 构建实际 Vite 示例。

### H.265 性能选项

默认使用 SIMD 能力检测和 WebGL；WebGL 初始化失败时自动回退 Canvas2D；定位时保留参考帧解码，省略目标前的颜色转换。
可通过 `wasmSIMD: false` 强制普通版，`hevcRenderer: 'canvas2d'` 或 `'offscreen'`
选择 Canvas2D 或 Worker 内绘制。已完成本机 Chrome 156 / RTX 3080 Ti 的真实无头浏览器对照：1080p WebGL 的 CPU 时间降低约 17%，4K Offscreen 降低约 38%，但 4K 峰值私有内存增加约 9%。Offscreen 模式保持可选，具体条件、画质差异及复现命令见 [浏览器测试说明](scripts/performance/browser/README.md)。
这些选项通过核心配置传入，UI 可继续独立替换。

`npm run test:wasm:pixels` 验证真实 WASM 的像素、PTS、定位跳过和颜色回退；
编译、测量及使用说明见 [wasm/readme](src/decoder/wasm/readme)。

### 仅使用播放核心

核心不创建控件、不设置容器外观，也不清空宿主已有内容。宿主应给容器设置尺寸；核心仅创建并管理自身的 video/canvas 媒体输出。

```html
<div id="media" style="width: 800px; height: 450px"></div>
<button id="play">播放</button>
<span id="status"></span>
<script src="./smsplayer.core.min.js"></script>
<script>
    const player = playerjsCore.createPlayerInstance({ divID: 'media' });
    document.getElementById('play').onclick = () => player.play();
    player.addEventListener('statechange', ({ detail }) => {
        document.getElementById('status').textContent = detail.status;
    });
    player.load('https://example.com/live.flv');
</script>
```

### 默认 UI 独立挂载与替换

在核心之外加载 `smsplayer.ui.min.js` 和 `player.ui.css`，再挂载界面：

```js
let binding = playerjsUI.mountUI(player, playerjsUI.createUI);

// 自动清理之前的 UI 及绑定；播放器、连接、缓冲与进度保持原样。
binding = playerjsUI.mountUI(player, context => new MyControls(context));

binding.destroy(); // 只移除 UI，播放继续
player.destroy(); // 销毁核心，也会通知绑定层清理 UI
```

默认组合版仍可使用 `controller: false` 关闭默认 UI，或通过 `ui: context => new MyControls(context)` 提供自定义 UI。组合版的替换入口为 `playerjs.mountUI()`。纯自定义工程可直接引用 `src/ui/bindings.js`，该模块不导入任何默认皮肤、样式或核心代码。

自定义 UI 约定：

- 工厂接收 `{ container, state }`，返回实现 `EventTarget` 的对象。
- 实现 `render(state)` 展示状态，不能在此反向派发播放操作。
- 实现 `destroy()`，仅清理自己创建的节点、监听和计时器，不销毁核心或清空整个媒体容器。
- 用户操作派发 `play`、`pause`、`reload`、`stop`、`snapshot`、`debug` 事件。
- `seek` 的 `event.detail` 为秒，`volume` 为 0–1，`playbackrate` 为正数；进度条百分比由 UI 自行换算成秒。

### 状态快照

`player.state` 和 `statechange` 的 `event.detail` 提供同一套只读结构：

```js
{
    status, source, generation,
    currentTime, duration, bufferedDuration, // 秒
    volume, playbackRate, version,
    capabilities: { audio, seek, snapshot },
    mediaInfo, // 只读媒体信息，尚未解析时为 null
    error      // null 或 { message }
}
```

`status` 可为 `idle/loading/ready/playing/paused/waiting/blocked/error/ended/destroyed`。
`generation` 在 load/stop 重置时递增，方便 UI 清理上一个来源的展示。
UI 只订阅公开状态与调用播放 API，不访问渲染器、轨道或解码器内部状态。

## 接口

| 名称 | 参数 | 描述 |
| :--- | :--- | :--- |
| createPlayerInstance | config: object | 创建实例 |
| load | url:string | 加载并播放 |
| play | -- | 播放 |
| pause | -- | 暂停 |
| seek | n | [仅点播] 跳转到指定时间（秒） |
| reload | -- | 重新加载 |
| playbackRate | n | [setter/getter] 设置或获取播放倍数，默认为1 |
| currentTime |  | [getter] 获取当前播放进度（秒） |
| duration |  | [getter] 获取视频总时长（秒） |
| debug | -- | 下载日志文件 |
| snapshot | -- | 截图并保存为png |
| stop | -- | 停止 |
| destroy | -- | 销毁实例及绑定的 UI，保留宿主已有内容 |
| version | -- | [getter] 获取版本号 |
| state | -- | [getter] 只读状态快照 |
| status | -- | [getter] 当前播放状态 |
| container | -- | [getter] 媒体输出容器 |

### 配置列表

| 字段 | 是否必须 | 类型 | 默认值 | 备注 |
| :--- | :--- | :--- | :--- | :--- |
| divID | 二选一 | string | null | 媒体输出容器 ID |
| container | 二选一 | HTMLElement | -- | 直接传入媒体输出容器，与 divID 二选一 |
| cacheduration | 否 | number | 10 | [仅点播] 最小缓冲区(s)，低于该值时加载下一个片 |
| loglevel | 否 | string | "info" | 日志级别，可选项：debug、info、warn、error |
| controller | 否 | boolean | true | 仅组合版：是否挂载默认或自定义 UI |
| ui | 否 | function | 默认 UI 工厂 | 仅组合版：接收 context 的自定义 UI 工厂 |
| autoPlay | 否 | boolean | false | 初始化完成后是否尝试自动播放，仍受浏览器策略限制 |

```js
{
    divID: "playerID", 
    cacheduration: 3,
    loglevel: "info",
    controller: true,
    autoPlay: true
}
```

## 事件

| 类型 | 属性 | 描述 |
| :--- | :--- | :--- |
| ready | -- | 解码器初始化完成 |
| loadstart | -- | 开始加载来源 |
| autoplayblocked | -- | 浏览器阻止自动播放，等待手动操作 |
| play | -- | 播放开始 |
| pause | -- | 播放暂停 |
| waiting | -- | 缓存中 |
| playing | -- | 播放中 |
| error | error | 播放异常 |
| ended | -- | 播放结束 |
| statechange | detail | 完整只读状态快照 |
| mediainfo | detail | 媒体信息更新 |
| timeupdate | detail | 当前时间，秒 |
| durationchange | detail | 时长，秒 |
| cacheduration | detail | 缓冲时长，秒 |
| volumechange | detail | 音量，0–1 |
| ratechange | detail | 倍速 |
| stop | -- | 重置播放 |
| destroy | -- | 核心已销毁 |

## 部署须知

- 后端nginx需要允许跨域请求，并且需要允许HEAD、OPTIONS、GET请求，需要支持Bytes分片请求，参考配置如下:

```yaml
location /videos/ {
    .........

    add_header 'Access-Control-Allow-Origin' '*';
    add_header 'Access-Control-Allow-Headers' '*';
    add_header 'Access-Control-Expose-Headers' '*';
    if ($request_method = 'OPTIONS') {
        return 204;
    }

    ......
}
```

- 后端nginx需要开启gzip压缩，以保证加载wasm速度

在nginx的mime.types配置文件当中增加以下类型配置：

```yaml
application/wasm wasm;
```

在nginx.conf中server配置块的gzip相关配置中，追加application/wasm类型：

```yaml
gzip on;
gzip_types application/wasm;
```
