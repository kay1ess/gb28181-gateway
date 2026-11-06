# GB28181 C SDK

头文件为 [gb28181/c_api.h](../include/gb28181/c_api.h)。接口使用 C ABI、固定宽度整数、UTF-8 字符串和不透明句柄。异步回调返回 JSON，方便 C、Rust、Go、Python 和 .NET 通过同一套接口接入。调用方可用 `gb28181_abi_version()` 检查 ABI 版本，当前为 1。

## 构建

```text
bazel build //:sdk_c_shared
```

Windows 产物为 `bazel-bin/gb28181_c_sdk.dll`，Linux 为 `bazel-bin/libgb28181_c_sdk.so`。部署时让系统加载器能找到同次构建的 ZLMediaKit 动态库。使用 Bazel 编写原生调用方时，可直接依赖 `//:sdk_c`。

## 调用方式

```c
#include <stdio.h>
#include "gb28181/c_api.h"

static void OnStatistics(void* context, gb28181_status status, const char* json) {
  (void)context;
  if (json) puts(json);
  else fprintf(stderr, "%s\n", gb28181_error_message(status));
}

gb28181_status QueryStatistics(gb28181_client* client) {
  return gb28181_get_statistics(client, NULL, OnStatistics, NULL);
}
```

1. 调用 `gb28181_client_create`，再调用 `gb28181_config_init` 初始化配置。按环境设置数据库路径、对端可达地址和端口，然后调用 `gb28181_client_start`。默认值与 [C++ SDK](SDK.md#配置) 一致，SIP 端口必须非零。
2. 异步方法返回 `GB28181_OK` 表示已受理。结果由 SDK 回调线程返回，JSON 形如 `{"code":0,"message":"Success","data":{},"trace_id":""}`。提交失败时直接返回错误码，回调不会执行。JSON 指针只在回调期间有效。
3. 在调用线程执行 `gb28181_client_stop` 或 `gb28181_client_destroy`。销毁会先停止服务并等待回调。外语绑定要在回调结束前保留回调函数和用户数据的引用。
4. 动态库调用方在销毁全部客户端后，调用 `gb28181_library_shutdown` 结束库的全局工作线程，再卸载动态库或退出进程。该调用只用于最后一次关闭，之后不能再启动客户端。

## 接口分组

| 分组 | 函数名，省略 `gb28181_` |
| --- | --- |
| 下级管理 | `add_platform`、`update_platform`、`remove_platform`、`get_platform`、`list_platforms` |
| 目录与资源 | `sync_catalog`、`get_operation`、`get_resource`、`list_resources` |
| 设备信息 | `get_device_info`、`get_device_status`、`refresh_device_info`、`refresh_device_status` |
| 点播 | `start_live_by_channel`、`get_playback_address`、`get_playback`、`wait_playback_ready`、`force_stop_playback` |
| 兼容租约 | `renew_playback`、`release_playback` |
| 状态与事件 | `get_statistics`、`subscribe_events`、`unsubscribe_events` |

目录同步完成回调给出任务 ID，进度通过 `operation_changed` 事件或 `get_operation` 获取。目录事件有可选的 `catalog_batch`，用于接收 SIP 分批结果。

调用 `gb28181_start_live_by_channel` 时，在 `gb28181_channel_live_request` 中填下级 ID 和通道国标编码。收流方式填 `GB28181_MODE_DEFAULT` 时继承下级配置。

```c
const gb28181_channel_live_request live = {
    "lower_1", "34020000001320000001", GB28181_MODE_DEFAULT};
gb28181_start_live_by_channel(client, &live, NULL, on_complete, user_data);
```

点播返回 `playback_id`。调用 `gb28181_get_playback_address` 为这路播放启用无人拉流回收，空闲秒数传 0 使用默认 60 秒，显式值范围为 1–86400 秒。只要有 RTSP 或 HTTP-FLV 观看连接，就无需续租。结果的 `endpoints` 为空时，可调用 `gb28181_wait_playback_ready` 或 `gb28181_get_playback`。C++ 对应入口为 `GetPlaybackAddress`，用 `std::nullopt` 选择默认值。

已有 C ABI 调用方可以继续用 `gb28181_start_live` 和原请求结构。旧模式用租约 `id` 续租和释放。C++ 的 `StartLiveRequest` 可填写 `platform_id` 与 `channel_gb_id`，此时把 `resource_id` 留空。C++ 调用方需用新头文件重新编译。

请求选项可设置超时、追踪 ID 和取消句柄。取消句柄可跨请求共享，一旦取消即保持取消状态。已经提交的数据库变更可能在取消前完成，重试前应先查状态。
