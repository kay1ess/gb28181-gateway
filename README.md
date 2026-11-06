# GB28181 Proxy Tool

基于 C++17 和 Bazel 的 GB/T 28181 代理，用于接入下级、同步目录并发起拉流。以下命令均在项目根目录执行。

接口见 [HTTP API](doc/API.md)、[SDK](doc/SDK.md)。

## 编译

已验证 Windows x64、VS 2022 / MSVC 19.42、C++17。需要 Windows SDK、Git Bash、CMake、Ninja，以及 `.bazelversion` 中的 Bazel 9.3.2rc3。首次构建需要网络下载依赖。

```powershell
bazel build --config=windows //apps/server:server
```

Linux x64：

```sh
bazel build --config=linux //apps/server:server
```

不传 `--config` 时按当前系统自动选择。编译结果为 `bazel-bin/apps/server/server.exe`（Linux 为 `server`）。运行时保留旁边的 `mk_api.dll` 或 `libmk_api.so`。

## 配置

```powershell
Copy-Item -LiteralPath configs/server.example.json -Destination configs/server.local.json
```

按实际环境修改 `configs/server.local.json`：

| 配置项               | 示例值                 | 说明                                                           |
| -------------------- | ---------------------- | -------------------------------------------------------------- |
| `server_gb_id`       | `34020000002000000001` | 本级 20 位国标编码                                             |
| `realm`              | `3402000000`           | 10 位 SIP 认证域                                               |
| `bind_address`       | `127.0.0.1`            | SIP、业务 HTTP 和 RTP 的监听地址                               |
| `advertised_address` | `127.0.0.1`            | 下级和播放器能访问的 IPv4，不要填 `0.0.0.0`                    |
| `sip_port`           | `5060`                 | SIP 端口                                                       |
| `http_port`          | `8080`                 | 业务 API 和 WebSocket                                          |
| `rtp_port`           | `10000`                | 各路共用的 RTP 端口，RTCP 使用下一端口。`0` 表示启动时自动选择 |
| `media_rtsp_port`    | `8554`                 | RTSP 播放端口。`0` 表示自动分配                                |
| `media_http_port`    | `8081`                 | HTTP-FLV 播放端口。`0` 表示自动分配                            |
| `database_path`      | `gb28181.db`           | 相对进程工作目录，父目录必须已存在且可写                       |
| `api_token`          | 自行设置               | 16–256 字符，请求头 `Authorization: Bearer <api_token>`        |
| `query_timeout_ms`   | `10000`                | 目录查询超时                                                   |
| `media_timeout_ms`   | `10000`                | 点播等待媒体就绪的超时                                         |
| `lease_seconds`      | `60`                   | 旧租约模式的有效期，取地址接口单独设置无人拉流超时             |
| `max_playbacks`      | `64`                   | 同时播放上限                                                   |

本机调试可保留 `127.0.0.1`。局域网接入时，把 `advertised_address` 改成这台机器的实际地址。端口被占用时启动失败，不会自动换端口。

## 启动

```powershell
./bazel-bin/apps/server/server.exe ./configs/server.local.json
```

在项目根目录启动，这样 `database_path` 等相对路径才指向项目内的文件。`bazel run` 的工作目录不是项目根，相对配置路径会报 `Invalid configuration file`。

进程会停在终端里，看到 `Listening on http://...` 就是已经在服务。Ctrl+C 结束。

演示页面在 [apps/web](apps/web/README.md)。服务启动后另开终端：

```powershell
cd apps/web
npm ci
npm run dev
```

打开 `http://127.0.0.1:5173`，填入配置中的 `api_token`。
