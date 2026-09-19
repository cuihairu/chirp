# Chirp Core SDK(C++)

C++ 客户端协议核心:`chirp::sdk::ChatClient` 直连 chat 网关的 TCP 长连接(`[u32_be len][Packet protobuf]` 帧)。桌面游戏客户端与 Unreal SDK(`sdks/unreal`)共用这一份实现;连接语义与 web/mobile/unity 三端的 `ChirpClient` 逐一对齐。

## 能力面

- **连接状态机**:`Disconnected → Connecting → Connected → LoggedIn`,外加 `WaitingReconnect`(运行中断线的退避重连中)与 `Kicked`(顶号终态)。
- **请求-响应**:通用 `Request(req_msg_id, resp_msg_id, body, cb)`,sequence 关联 + 响应 msg_id 校验,默认 10s 超时(`ChatConfig::request_timeout_ms`),迟到的响应无害丢弃。
- **notify 订阅**:`OnNotify(msg_id, cb)` 返回退订句柄,`OffNotify` 退订;便捷回调(`SetMessageCallback` 等)与之平行。
- **心跳**:默认 25s,pong 必须回显 ping 的非零 sequence 才记为已答;连续 `max_missed_pongs`(默认 2)次未答判定连接死亡,进入重连。
- **自动重连**:指数退避 500ms→15s ±20% 抖动;`max_reconnect_attempts`(-1 无限);KICK 不重连;显式 `Disconnect()`/`Logout()` 取消重连。
- 所有回调在内部 io 线程触发;引擎适配层负责派发到游戏线程(见 unreal 壳的 `AsyncTask` 用法)。

## 构建

in-tree(随 chirp 仓库构建,`sdks/core/CMakeLists.txt` 被根 CMake 引入):

```bash
cmake --build build --target chirp_core_sdk_static sdk_example
```

standalone(无 chirp 树也能编,自动落回 checked-in gencode):

```bash
cd sdks/core && cmake -B build && cmake --build build
```

产物:`chirp_core_sdk_static`(静态)/ `chirp_core_sdk`(共享;依赖的 chirp_network/chirp_common 会被自动以 PIC 重编)。

## 测试

- 单测:`tests/unit/sdk_core_tests.cc`(37 例:状态机、loopback 登录/收发、请求关联与超时、notify 订阅退订、心跳死亡判定、踢线终态、断线重连),CI 自动跑。
- 进程级 E2E:`./test_services.sh --smoke-sdk`(两个 SDK 实例对真 `chirp_chat` 双向收发 + 离线队列)。

## 使用

```cpp
#include "chirp/sdk_client.h"

chirp::sdk::ChatConfig config;
config.gateway_host = "127.0.0.1";
config.gateway_port = 5000;
chirp::sdk::ChatClient client(config);

client.SetMessageCallback([](const std::string& sender, const std::string& content) {
  // io 线程:投递回你的主线程再碰 UI
});
client.Connect();
client.Login("user-42", [](const std::error_code& ec, const std::string& user_id) {
  // ...
});
client.SendMessage("peer-7", "hello");
```

完整示例见 `examples/sdk_example.cc`(`--smoke-sdk` 的被测程序)。

## 历史

2026-09 架构评审删除了从未编译过的 `chirp::core` 模块层(include/chirp/core、src/client_impl、src/modules,约 2000 行,头文件自带编译错误且被 Unity/Unreal 旧桥各自错误引用);现存的只有 smoke 验证过的 `sdk.cc`/`sdk_client.cc` 链,并在此之上补齐了通用 Request/notify 订阅/心跳回声/退避重连/KICK 终态。
