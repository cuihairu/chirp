# Chirp Unreal 插件 SDK

Unreal Engine 插件壳,包装 native 协议核心 `chirp::sdk::ChatClient`(`sdks/core`,与桌面 C++ SDK 同一份代码)。协议逻辑(帧、心跳 pong 回声校验、指数退避重连、KICK 终态、请求超时)全部在 native 核心,由 chirp 仓库 CI 的 `sdk_core_tests`(37 例 loopback 单测)覆盖;本目录只做两件事:

1. **游戏线程派发**:native 回调在 SDK 内部 io 线程触发,`UChirpClientSubsystem` 用 `AsyncTask(ENamedThreads::GameThread, ...)` 转发——这是旧版桥完全缺失的关键一环;
2. **Blueprint 化**:连接状态枚举、可绑定事件、BlueprintCallable 方法。

> Unreal 侧代码(UCLASS/Build.cs)无法在 chirp 的 CI 里编译(需要 UBT/引擎头);它的正确性契约 = native 核心的单测 + 本目录的极薄转发层。UE 集成后首次编译如有 API 版本差异,按报错调整即可(转发层没有复杂逻辑)。

## 接入步骤

1. **编译 native 核心**(宿主机上,一次性):

   ```bash
   cd <chirp 仓库>
   cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON \
         -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake -G Ninja
   cmake --build build --target chirp_core_sdk_static
   ```

2. **拷贝插件**:把 `sdks/unreal/` 整目录拷进工程的 `Plugins/ChirpSDK/`(或作为引擎插件)。
3. **指向 native 产物**:设置环境变量 `CHIRP_SDK_NATIVE_DIR=<chirp 仓库路径>` 后再启动 UBT(`ChirpSDK.Build.cs` 检查该目录并链接 `libchirp_core_sdk.a`)。Windows 下链接的是 `chirp_core_sdk.lib`,并把 protobuf/abseil 的导入库一并加进 `PublicAdditionalLibraries`(见 Build.cs 注释)。
4. 编辑器里启用 ChirpSDK 插件,Blueprint 里 `Get Chirp Client`(GameInstance Subsystem)。

## Blueprint 用法

```
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

要点:

- **没有 OnConnected 事件**(故意的):用 `GetState()` 轮询或等 `OnLoginResult`。
- **KICK 是终态**:核心不再自动重连,`GetState() == Kicked`,需要重新 `Connect`+`Login`。
- 端口:开发拓扑 chat TCP **5000**(WS 5001 是 web/unity 走的入口,Unreal 用 TCP 帧——帧协议与 WS 消息体一致)。
- 事件都在游戏线程,回调里可直接碰 UWorld/UWidget。

## 与其他端的关系

web/mobile/unity 各自有独立的协议实现(Dart/C#),Unreal 与桌面 C++ 共享 native 核心——四端语义(sequence 关联、心跳、退避、踢线)逐一对齐,测试矩阵见 `sdks/core/README.md` 与 `tests/unit/sdk_core_test.cc`。
