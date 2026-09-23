---
title: Unreal 接入
---

# Unreal Engine

Unreal 侧的官方支持形态是**插件壳 + native 核心**:`sdks/unreal` 提供 `UChirpClientSubsystem`(GameInstance Subsystem)与 Blueprint 化的事件,协议逻辑(帧、心跳、指数退避重连、KICK 终态、请求超时)全部在 native 核心 `chirp::sdk::ChatClient`(`sdks/core`,与桌面 C++ SDK 同一份代码)里,由 chirp CI 的 `sdk_core_tests`(37 例 loopback 单测)覆盖。

插件只做两件事:

1. **游戏线程派发**:native 回调在 SDK 内部 io 线程触发,Subsystem 用 `AsyncTask(ENamedThreads::GameThread, ...)` 转发——回调里可以直接碰 `UWorld`/`UWidget`;
2. **Blueprint 化**:连接状态枚举、可绑定事件、BlueprintCallable 方法。

完整源码见 [sdks/unreal/README.md](https://github.com/cuihairu/chirp/tree/main/sdks/unreal)。

## 接入方式

1. **编译 native 核心**(宿主机上,一次性):

   ```bash
   cd <chirp 仓库>
   cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON \
         -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake -G Ninja
   cmake --build build --target chirp_core_sdk_static
   ```

2. **拷贝插件**:把 `sdks/unreal/` 整目录拷进工程的 `Plugins/ChirpSDK/`(或作为引擎插件)。
3. **指向 native 产物**:设置环境变量 `CHIRP_SDK_NATIVE_DIR=<chirp 仓库路径>` 后再启动 UBT(`ChirpSDK.Build.cs` 检查该目录并链接 `libchirp_core_sdk.a`;Windows 链接 `chirp_core_sdk.lib`,并把 protobuf/abseil 导入库一并加进 `PublicAdditionalLibraries`)。
4. 编辑器里启用 ChirpSDK 插件,Blueprint 里 `Get Chirp Client`(GameInstance Subsystem)。

> Unreal 侧代码无法在 chirp 的 CI 里编译(需要 UBT/引擎头);它的正确性契约 = native 核心的单测 + 极薄转发层。UE 集成后首次编译如有 API 版本差异,按报错调整即可。

## 生命周期

```text
Connect("127.0.0.1", 5000) → Login("user-42") → 事件回调(游戏线程)
        ↓ 断线(核心自动指数退避重连)
OnDisconnected → (重连成功) → 重新 Login
        ↓ 收到 KICK
OnKicked:终态,核心不再自动重连,需重新 Connect + Login
```

要点:

- **没有 OnConnected 事件**(故意的):用 `GetState()` 轮询或等 `OnLoginResult`。
- **KICK 是终态**:核心不再自动重连,`GetState() == Kicked`,需要重新 `Connect` + `Login`。
- **端口**:开发拓扑 chat TCP **5000**(WS 5001 是 web/unity 走的入口,Unreal 用 TCP 帧——帧协议与 WS 消息体一致)。
- 事件都在游戏线程。

## Blueprint 用法

```text
Get Chirp Client
├─ OnChatMessage   → (Sender, Content)  实时私聊推送
├─ OnKicked        → (Reason)           顶号,终态
├─ OnDisconnected  → (Reason)           断线(核心会自动重连)
├─ OnLoginResult   → (UserId)           登录往返结果,失败为空
└─ WatchNotify(6001) → OnRawNotify(MsgId, Body)  通用 notify 订阅

Connect("127.0.0.1", 5000)
Login("user-42")          // scaffold 模式 token 即 user id
SendChatMessage("peer-7", "hello")
```

`WatchNotify` 是通用出口:任意 `MsgID` 的推送(聊天 2005、设备 6xxx 等)都能订阅,拿原始 body 自行解析。

## 协议 / API 参考

- 协议总览:[API 概述](/api/overview)(Packet 帧、消息 ID 分段、核心流程)
- C++ 核心头文件:`sdks/core/include/chirp/sdk_client.h`(`ChatClient` 公开 API 面)
- 钩子接口(拦截器/命令/存储/鉴权/监听):[SDK 钩子接口](/design-notes/sdk_hooks)
- 引擎兼容性 checklist:[SDK 引擎兼容性](/design-notes/sdk_compatibility)
- 服务端接入(游戏后端):[服务端 SDK](/sdk/server) 与[服务器平面](/server_plane)

## 现状与边界

- Unreal 与桌面 C++ 共享 native 核心;web/mobile/unity 各自有独立协议实现(Dart/C#),四端语义(sequence 关联、心跳、退避、踢线)逐一对齐,测试矩阵见 [sdks/core/README.md](https://github.com/cuihairu/chirp/tree/main/sdks/core)。
- 语音/组队/社交平面在 Unreal 侧暂无现成封装;可经 native 核心的通用请求接口或 `WatchNotify` 自行扩展。
