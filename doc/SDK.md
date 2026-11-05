# GB28181 C++ SDK

头文件 [`include/gb28181/client.h`](../include/gb28181/client.h)。`CreateClient()` 创建实例。`Start` / `Stop` 在调用线程同步执行，其余方法返回是否受理，结果在回调里给一次。先看 `ok()`。回调里不要再等回调，也不要 `Stop()` 或销毁 `Client`。`RequestOptions` 默认超时 10 秒。

可运行示例：[`list_resources.cc`](../examples/sdk/list_resources.cc)、[`live_preview.cc`](../examples/sdk/live_preview.cc)。

## 接口

| 方法 | 作用 |
| --- | --- |
| `Start` / `Stop` | 启动或关闭。`Stop` 后可以再 `Start` |
| `AddPlatform` | 添加下级。`id` 留空则自动生成 |
| `UpdatePlatform` | 修改下级，必须带原来的 `id` |
| `RemovePlatform` | 删除下级及其目录和播放 |
| `GetPlatform` / `ListPlatforms` | 查询下级。`kOnline` 后再同步目录 |
| `SyncCatalog` | 发起目录同步。回调只表示已发起，此时多为 `kRunning` |
| `GetOperation` | 查同步进度，直到 `kSucceeded`、`kFailed` 或 `kCancelled` |
| `ListResources` / `GetResource` | 查目录。`limit` 默认 100，最大 1000 |
| `GetDeviceInfo` / `GetDeviceStatus` | 读缓存。无缓存为 `kNotFound` |
| `RefreshDeviceInfo` / `RefreshDeviceStatus` | 向下级重新查询 |
| `StartLive` | 点播并返回租约。相同资源和收流模式会复用播放 |
| `WaitPlaybackReady` | 等到 `kPlaying`，`endpoints` 里有 RTSP 和 HTTP-FLV |
| `GetPlayback` | 查播放状态 |
| `RenewPlayback` | 按 `lease_seconds` 续租 |
| `ReleasePlayback` | 释放一条租约。最后一条释放后停止该路播放 |
| `ForceStopPlayback` | 停掉整路播放 |
| `GetStatistics` | 在线下级、查询、播放和租约计数 |
| `SubscribeEvents` | `Start` 之后订阅变化。目录进度在 `kOperationChanged` 的 `operation` 里 |

收流模式：`kUdp`、`kTcpPassive`（上级监听）、`kTcpActive`（上级去连下级）。`StartLiveRequest.mode` 不填则用下级的 `default_receive_mode`。

## 配置

`Start` 使用 `Config`。`advertised_address` 填下级和播放器能访问的 IPv4。端口填 `0` 表示启动时自动分配。`rtp_port` 各路共用，RTCP 用下一个端口。

| 字段 | 默认值 |
| --- | --- |
| `database_path` | `gb28181.db` |
| `server_gb_id` | `34020000002000000001` |
| `realm` | `3402000000` |
| `bind_address` | `0.0.0.0` |
| `advertised_address` | `127.0.0.1` |
| `sip_port` | `5060` |
| `rtp_port` | `10000` |
| `media_rtsp_port` | `8554` |
| `media_http_port` | `8081` |
| `query_timeout_ms` | `10000` |
| `media_timeout_ms` | `10000` |
| `lease_seconds` | `60` |
| `max_playbacks` | `64` |

`AddPlatform` 使用 `PlatformConfig`。`gb_id` 为下级 20 位编码，`password` 为注册口令。`id`、`name`、`realm`、`username` 可留空：`id` 自动生成，`realm` 用上面的认证域，`username` 用 `gb_id`。`enabled` 默认 `true`，`heartbeat_timeout_seconds` 默认 180，`default_receive_mode` 默认 `kUdp`。
