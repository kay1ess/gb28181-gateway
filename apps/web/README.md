# GB28181 Gateway 演示页

Vue 3 + Vite 单页演示，独立于 C++ SDK 和 HTTP 服务。主界面只有下级与资源树，添加、编辑、目录同步、设备详情和视频预览使用弹窗。已接入用户指定的 smsplayer Vue 3 播放器，点播后自动通过 HTTP-FLV 播放，保留 RTSP 地址供其他客户端使用。

## 本机启动

需要 Node.js 22.12+；当前验证环境为 Node.js 22.17.0。

先在项目根目录启动网关：

```powershell
.\bazel-bin\apps\server\gateway.exe .\configs\server.example.json
```

另开终端启动前端：

```powershell
cd apps/web
npm ci
npm run dev
```

打开 **http://127.0.0.1:5173**，API 地址保留 `/api`，输入服务配置中的 `api_token`。示例配置的测试令牌为 `replace-this-test-token`，仅供本机测试。

默认代理目标为 `http://127.0.0.1:8080`。连接其他上级时，在启动前设置服务地址：

```powershell
$env:GB_API_TARGET = 'http://192.168.1.10:8080'
$env:GB_MEDIA_TARGET = 'http://192.168.1.10:8081'
npm run dev
```

也可复制 `.env.example` 为 `.env.local` 后修改 `GB_API_TARGET`，重启 Vite 生效。浏览器中继续使用 `/api`，避免现有后端没有跨域响应头导致请求失败。直接在页面填写完整 API URL 时，该服务必须自行支持相应的浏览器跨域访问。

令牌仅保存在当前标签页的 `sessionStorage` 中，刷新后可恢复连接；断开连接会清除令牌。下级密码只在保存配置时发送，编辑时不回显，留空保留原密码。不要将真实令牌写入 `VITE_*` 环境变量或源码。

## 操作流程

1. 在「连接设置」中连接上级服务。
2. 点击「添加下级」，填写名称、国标编码、认证域和密码；管理 ID 由服务端自动生成，已有记录的 ID 保持不变。
3. 让下级使用相同的国标编码和密码注册，等待下级行显示在线。
4. 每个下级一行，点击前面的「＋ / −」逐级展开或收起资源；在行上可编辑、删除或同步目录。
5. 点击「同步目录」弹出进度窗口，显示接收数量和错误；关闭窗口后任务继续，完成后点击「查看资源」展开并更新资源。
6. 添加或编辑下级时设置默认收流方式（默认 UDP，可选 TCP 被动或 TCP 主动）；点击视频通道名称，在弹窗中播放。播放地址与诊断信息默认折叠。关闭窗口或按 Escape 释放本页预览，不停止其他观看者；「继续选择设备」收起窗口并保持租约，可再选其他通道并在同一窗口预览。
7. 点击资源「详情」弹出设备信息窗口，可读取缓存或实时查询信息/状态。

新增请求 `POST /api/platforms` 不发送 `id`，服务端在公共业务层生成并持久化 ID，通过响应 `data.id` 返回；SDK `AddPlatform` 的空 ID 使用相同逻辑。原有调用显式指定 ID 仍受支持。编辑继续使用原 ID，HTTP 不允许修改身份；不需要迁移现有平台或资源记录。

多个摄像头可以同时点播。页面只管理当前标签页创建的播放租约，后端目前没有全局播放列表接口；不能据此把其他客户端的会话显示为本页拥有。刷新后从本标签页存储恢复仍未到期的租约，不重复创建点播。关闭页面后停止续租，由服务端在租约到期后清理；需要立即释放时使用「关闭预览」、关闭播放窗口或「断开并释放本页点播」。后台标签页被冻结或网络长时间中断也可能导致租约到期。

服务统计、下级状态和播放状态每 3 秒通过 HTTP 查询。页面通过 `/api/events` 提交目录同步并接收进度，首条消息使用 API 令牌鉴权。断线期间通过 HTTP 补查已知任务，重连后再补查一次。资源树按平台按需加载，分批读取完整目录后在前端组织层级。缺失父节点挂在平台根下，异常父级环会被处理，不同平台的同名通道编码彼此隔离。错误展示业务码和 `trace_id`。

## 已接入的播放器

`src/components/PlayerSlot.vue` 封装用户 `D:/likai/h265player/example/vue3` 使用的 `SmsPlayer` 组件，父组件传入：

| 属性 / 事件  | 内容                                                                   |
| ------------ | ---------------------------------------------------------------------- |
| `playback`   | 播放 ID、状态、接收模式、SSRC、错误和 `endpoints: [{ protocol, url }]` |
| `resource`   | 当前摄像头和所属下级信息                                               |
| `released`   | 本页租约是否已释放，释放后立即卸载播放器并关闭媒体请求                 |
| `error` 事件 | 播放器自身错误文本，可交给父级统一提示                                 |

播放器等待 `playback.state === 'playing'` 后加载 HTTP-FLV 地址，在组件卸载、播放结束、本页租约释放或地址变化时释放自身资源。播放和租约由 `use_console.js` 管理，播放器不重复发起点播或续租。

后端启用 ZLM RTMP 媒体封装以提供 HTTP-FLV，不额外打开 RTMP 监听。返回 `protocol: 'http-flv'` 的端点，地址形如 `http://192.168.1.16:8081/rtp/play_x.live.flv`。前端将其路径映射为同源 `/media/rtp/play_x.live.flv`，通过 `GB_MEDIA_TARGET` 访问媒体服务，业务 API 令牌不会转发给媒体服务。连接不同服务器时，API 目标和媒体目标都要更新；只修改页面 API 地址不会自动修改媒体代理目标。

播放器适配器和完整运行资源已复制到 `vendor/smsplayer`，来源和 SHA256 清单见该目录。包含核心、默认 UI、CSS/source map、匹配的 Worker、普通和 SIMD 两套 JS/WASM。没有运行时引用原工程、CDN 或额外安装第二份 Vue。`.npmrc` 的 `install-links=true` 让本地依赖以实际文件安装，避免 Windows 符号链接权限问题。

Vite 在启动和构建时使用随包复制脚本生成 `public/smsplayer`，并将其完整带入 `dist/smsplayer`；生成目录不提交，`vendor` 快照必须随项目保存。第三方预构建文件保持原样，不用 Prettier 格式化它们。升级时从同一次构建整体替换运行资源并更新哈希，避免核心、Worker 与 WASM 混用。

H.264 使用 MSE/video，H.265 使用 Worker/WASM/Canvas。除本地样例外，已用一台海康摄像头验证 GB28181 UDP → PS/H.265 → HTTP-FLV → 浏览器实播，页面显示 2560×1440、25 fps。ZLM 设置 `rtmp.enhanced=0`，输出该播放器支持的传统 HEVC FLV（Codec ID 12）；默认 Enhanced FLV 的 hvc1 头不被当前播放器解析器识别，会停留在连接中。此结果限于本次实机，不代表其他厂商和编码选项均已验证。播放器不支持 RTSP 直连；旧服务没有 HTTP-FLV 端点时会明确提示缺少浏览器播放地址。

## 构建与部署

```powershell
npm run build
npm run preview
```

产物在 `apps/web/dist`，本地预览地址为 http://127.0.0.1:4173，使用同一 API 和媒体代理配置。`vite preview` 用于本机验收；生产环境将静态产物、`/api` 和 `/media/rtp/` 反向代理放在同一个站点。Vite 的开发代理配置不会被打进静态产物。可参考 [Vite 代理配置](https://vite.dev/config/server-options#server-proxy)。

Nginx 示例（`root` 和后端地址按实际环境修改）：

```nginx
map $http_upgrade $connection_upgrade {
    default upgrade;
    '' close;
}

server {
    listen 80;
    server_name _;
    root /srv/gb28181-web;

    location / {
        try_files $uri $uri/ /index.html;
    }

    location /api/ {
        proxy_pass http://127.0.0.1:8080;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header Authorization $http_authorization;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection $connection_upgrade;
        proxy_read_timeout 75s;
    }

    location /media/rtp/ {
        proxy_pass http://127.0.0.1:8081/rtp/;
        proxy_http_version 1.1;
        proxy_set_header Authorization "";
        proxy_buffering off;
        proxy_cache off;
        proxy_read_timeout 3600s;
    }

    location ~ \.wasm$ {
        default_type application/wasm;
    }
}
```

## 验证

```powershell
npm test
npm run format:check
npm run build
npx playwright install chromium
npm run test:e2e
```

普通浏览器测试使用明确的接口替身，覆盖认证错误、平台增删改、目录任务、设备缓存/实时查询、按需展开与多级目录、跨平台同编码隔离、树搜索、双路点播、自动续租、刷新恢复、释放与强停、错误码和窄屏布局。页面实际运行时没有模拟数据或演示模式。测试前端使用独立的 5174 端口（可用 `GB_WEB_TEST_PORT` 覆盖），不会占用用户正在使用的 5173 页面。

真实联调测试需要先准备项目服务、独立 Go 下级和 H.264 样例，执行：

```powershell
$env:GB_LIVE_E2E = '1'
$env:GB_API_TARGET = 'http://127.0.0.1:32180'
$env:GB_MEDIA_TARGET = 'http://127.0.0.1:32181'
npm run test:e2e
```

该测试自动创建独立数据库并启动 C++ 服务和 Go cascade 下级，通过页面添加平台、注册、同步目录、双路点播；两个浏览器视频元素都必须出现真实解码帧并推进播放时间，外部 FFmpeg 另对两个 RTSP 地址各解码一帧。逐路释放后检查另一路仍在播放，最后检查播放、媒体、租约和 SIP 计数归零。测试使用 HTTP 32180、媒体 HTTP 32181、SIP 32160、RTP 32100/32101，需保持空闲，不改动现有服务和数据库。

默认查找根目录的 `bazel-bin/apps/server/gateway.exe`、`.bazel-output/tools/gb28181-go-device.exe`、`.bazel-output/tools/sample.h264` 和 `C:/Program Files/ffmpeg/ffmpeg.exe`。可用 `GB_SERVER`、`GB_SIMULATOR`、`GB_SAMPLE`、`GB_FFMPEG` 覆盖路径。测试日志、截图和失败追踪位于 `test-results`。在受限 Windows 环境中，测试需要能管理自身创建的服务子进程，以正常关闭测试环境。

## 本次验证结果

最初的前端验收为 4 项 API 测试和 5 项浏览器测试。播放器接入后，新增协议映射测试；本轮 7 项浏览器测试全部通过，包含真实 C++/Go 双路 HTTP-FLV 浏览器播放及本地 H.264、H.265 实播。H.265 用实际 Canvas 像素和 Worker/WASM 成功加载验证。可选播放器样例位于根目录 `.bazel-output/player-fixtures/h264.flv` 和 `h265.mp4`，没有样例时只跳过这两项，不将跳过描述为通过。生产构建保留播放器全部静态资源。

资源播放入口先检查平台启用和注册状态、目录节点及子节点、通道在线状态和国标编码类型。131/132 视频通道可点播；已知主机、目录、显示与语音终端不可直接播放；未知类型或未知在线状态保留「尝试播放」。这些仅为目录预判，最终以 SIP 协商、媒体到达及播放器解码为准。点播不传 mode，自动使用所属下级的 default_receive_mode；修改默认值只影响后续点播，不打断当前视频。同一设备重复点击复用本页预览；关闭预览释放本页租约，不强制停止其他观看者。
