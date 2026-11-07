# GB28181 Gateway HTTP API

默认 `http://127.0.0.1:8080`。业务接口带 `Authorization: Bearer <api_token>`。JSON 请求体的接口使用 `Content-Type: application/json`。可选请求头 `X-Request-ID` 最多 64 个字母、数字、下划线或连字符；省略时由服务端生成，并在响应头和 `trace_id` 中返回。

成功响应：

```json
{"code": 0, "message": "Success", "data": {}, "trace_id": "demo-health"}
```

无数据时 `data` 为 `null`，空列表为 `[]`。路径中的 `{id}` 按原样做 URL 编码。

## 下级平台

| 方法 | URL | 说明 |
| --- | --- | --- |
| GET | `/api/health` | 在线下级、进行中的查询、活动播放、接收器、租约、SIP 会话和待处理请求 |
| GET | `/api/platforms` | 下级列表 |
| POST | `/api/platforms` | 新建，201。`id` 可省略，由服务端生成 |
| GET | `/api/platforms/{id}` | 查询一个下级 |
| PATCH | `/api/platforms/{id}` | 只更新请求体里出现的字段。带 `id` 时必须与路径相同。省略 `password` 表示保留原密码 |
| DELETE | `/api/platforms/{id}` | 删除下级、其资源和关联播放 |

新建字段：`gb_id` 必填，20 位数字。`name`、`password` 可空。`realm` 留空则用服务端认证域，填写时必须与服务端一致。`username` 留空则用 `gb_id`。`enabled` 默认 `true`，`heartbeat_timeout_seconds` 默认 180，`default_receive_mode` 默认 `udp`，也可为 `tcp_passive`、`tcp_active`。显式 `id` 最多 64 字符，只能含字母、数字、下划线和连字符。

```json
{
  "id": "lower_1",
  "gb_id": "34020000002000000002",
  "name": "园区下级",
  "password": "example-password",
  "default_receive_mode": "udp"
}
```

返回对象不含 `password`。`state` 为 `unknown`、`online`、`offline`。注册后带有 `remote_address`、`remote_port`、`sip_transport`。修改默认收流方式只影响之后的点播。

## 目录与资源

目录同步通过 [WebSocket](#websocket) 发送 `catalog_sync`，再用任务 ID 查询：

| 方法 | URL | 说明 |
| --- | --- | --- |
| GET | `/api/operations/{id}` | 任务。`state` 为 `running`、`succeeded`、`failed`、`cancelled` |
| GET | `/api/resources` | 分页查询目录 |
| GET | `/api/resources/{id}` | 一条资源 |
| GET | `/api/resources/{id}/info` | 设备信息缓存。无缓存 404 |
| POST | `/api/resources/{id}/info-refresh` | 向下级查询并更新缓存 |
| GET | `/api/resources/{id}/status` | 设备状态缓存。无缓存 404 |
| POST | `/api/resources/{id}/status-refresh` | 向下级查询并更新缓存 |

资源查询参数：`platform_id`、`keyword`（匹配名称或国标编码）、`offset`（默认 0）、`limit`（默认 100，范围 1–1000）、`state`（`online`、`offline`、`unknown`）。

任务的 `received`、`expected` 是已收到和预期条数。`state` 为 `succeeded` 后再读资源。失败时任务接口仍返回 200，原因在 `error`。资源 `parental` 为 `true` 时不能点播。`queried_ms` 是缓存采集时间。

## 点播

| 方法 | URL | 说明 |
| --- | --- | --- |
| POST | `/api/playbacks` | 点播，202，返回租约。同一资源、同一收流方式复用播放，每次一条新租约 |
| POST | `/api/playbacks/{id}/address` | 获取播放地址并启用按观看连接保活 |
| GET | `/api/playbacks/{id}` | 用 `playback_id` 查询状态与地址 |
| POST | `/api/playbacks/{id}/stop` | 结束该播放和全部租约 |
| POST | `/api/playback-leases/{id}/renew` | 兼容旧租约。租约不存在或已过期返回 404 |
| DELETE | `/api/playback-leases/{id}` | 释放一条租约。重复释放仍返回 200。旧模式下最后一条释放或到期后播放结束 |

```json
{"platform_id": "lower_1", "channel_gb_id": "34020000001320000001", "mode": "udp"}
```

`platform_id` 是上级保存的下级 ID，`channel_gb_id` 对应资源列表里的 `gb_id`。服务端在指定下级的目录里精确匹配通道。旧调用仍可只传 `resource_id`。两种定位方式混用或缺少必填字段时返回 400，指定下级的目录里找不到通道时返回 404。

`mode` 可省略，省略时用下级的 `default_receive_mode`。`udp` 由上级收 RTP，`tcp_passive` 由上级监听，`tcp_active` 由上级去连下级。

点播返回租约 `id`、`playback_id` 和 `expires_ms`。新调用方用 `playback_id` 请求 `POST /api/playbacks/{id}/address`，请求体可省略，也可指定空闲秒数。

```json
{"idle_timeout_seconds": 60}
```

范围是 1–86400 秒，默认 60 秒。设置作用于整路共享播放，后续取地址时使用最新指定的值。只要还有 RTSP 或 HTTP-FLV 观看连接，服务端就保持摄像头连接。媒体就绪后一直无人拉流，或最后一个观看者断开后持续无人拉流达到超时，服务端停止向摄像头拉流。此模式无需续租。查询状态和重复取地址都不会重置已开始的空闲计时。

地址请求返回当前播放对象。媒体未就绪时 `endpoints` 为空，可继续用 GET 查询。用 `playback_id` 强制停止播放，或用租约 `id` 释放旧租约。

`state` 为 `playing` 后使用 `endpoints`。`rtsp` 与 `http-flv` 地址由媒体服务提供，不走 Bearer 鉴权。播放失败时查看播放对象的 `error`，查询本身仍可返回 200。

## WebSocket

地址 `ws://127.0.0.1:8080/api/events`。握手不带 Token。连接后 5 秒内发送一条文本帧：

```json
{"type": "auth", "token": "<api_token>"}
```

通过后服务端先返回 `{"type":"auth_ok"}`，之后才推送事件。格式不对、令牌错误或超时，连接以关闭码 `1008` 断开。收到 `auth_ok` 之前不要发其他消息。

鉴权之后，文本 `ping` 会收到文本 `pong`。其他文本按 JSON 命令处理，目前只有目录同步。消息须是不超过 4096 字节的 JSON 对象。

```json
{"type": "catalog_sync", "request_id": "catalog-1", "platform_id": "lower_1"}
```

`request_id` 和 `platform_id` 都是 1–64 字符。受理时返回：

```json
{
  "type": "catalog_accepted",
  "request_id": "catalog-1",
  "operation": {
    "id": "catalog_demo_1",
    "platform_id": "lower_1",
    "state": "running",
    "received": 0,
    "expected": 0,
    "error": {"code": 0, "message": "Success", "trace_id": ""}
  }
}
```

拒绝时返回 `catalog_error`，`error` 只有 `code` 和 `message`：

```json
{"type": "catalog_error", "request_id": "catalog-1", "error": {"code": 2001, "message": "Platform offline"}}
```

用 `operation.id` 对应后面的 `operation_changed.entity_id`。断线后用 `GET /api/operations/{id}` 补查。

服务端推送的事件：

```json
{
  "type": "operation_changed",
  "entity_id": "catalog_demo_1",
  "trace_id": "catalog-1",
  "error": {"code": 0, "message": "Success", "trace_id": ""},
  "operation": {
    "id": "catalog_demo_1",
    "platform_id": "lower_1",
    "state": "running",
    "received": 17,
    "expected": 1000,
    "error": {"code": 0, "message": "Success", "trace_id": ""}
  }
}
```

| `type` | `entity_id` | 附加内容 |
| --- | --- | --- |
| `platform_changed` | 平台 ID | 无，用平台接口再查 |
| `catalog_changed` | 平台 ID | 无。目录写入成功后发送，条目用 `/api/resources` 读取 |
| `operation_changed` | 任务 ID | `operation`，结构与 `GET /api/operations/{id}` 相同 |
| `playback_changed` | 播放 ID | 无，用播放接口再查 |

`operation_changed` 在进度、成功、失败和取消时都会带上当前任务。`received` 已经等于 `expected` 时，`state` 仍可能是 `running`，要等 `succeeded` 再读新目录。失败或取消看 `operation.error`。事件顶层的 `trace_id` 是提交时的 `request_id`；任务结束时，`operation.error.trace_id` 也会写成这个值。

## 错误码

受理之后的任务或播放失败，错误在对象的 `error` 里，再查询仍可能返回 HTTP 200。

| HTTP 状态 | 业务码 | 含义 |
| --- | --- | --- |
| 400 | 1001 | 参数不合法 |
| 401 | 1002 | 鉴权失败，响应带 `WWW-Authenticate: Bearer` |
| 403 | 1003 | 权限不足 |
| 404 | 1004 | 对象或缓存不存在 |
| 409 | 1005 / 1006 / 1007 | 已存在 / 状态不允许 / 已取消 |
| 422 | 4004 | 媒体不支持 |
| 500 | 1099 / 5002 / 5003 / 5004 | 内部错误 / 存储读失败 / 存储写失败 / 迁移失败 |
| 502 | 2003 / 3001 / 3002 / 3003 / 4002 | 目录同步失败 / SIP 传输失败 / SIP 被拒 / SDP 协商失败 / 媒体连接失败 |
| 503 | 1009 / 2001 / 2002 / 4001 / 5001 | 达到上限 / 下级离线 / 资源离线 / 媒体分配失败 / 存储繁忙 |
| 504 | 1008 / 4003 | 请求超时 / 媒体接收超时 |

未知路由返回 404 和业务码 1004。方法不允许返回 405 和业务码 1001。
