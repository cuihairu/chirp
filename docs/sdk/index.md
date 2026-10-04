---
title: SDK 总览
---

# SDK 总览

chirp 的接入面是纯协议(TCP/WebSocket + Protobuf 小帧),SDK 按"协议核心 + 引擎薄壳"分层:**协议逻辑每种语言只实现一份**(C++ core / Unity C# / TS / Go 各一份参考实现),引擎接入要么是壳(Unreal),要么直接搬现成协议栈(Cocos/LayaAir/微信小游戏/Godot)。本页是全部现有 SDK 的索引与测试口径;每一行的文档页有用法与集成细节。

**边界规则(2026-10-02)**:`sdks/` 是**游戏**接入面;伴侣/聊天 app 不依赖 `sdks/`——app 侧协议层放在 `apps/` 内自持(TS 侧 `apps/shared/protocol` `@chirp/app-protocol`,Android `apps/android/.../protocol/`,iOS `apps/ios/Sources/ChirpProtocol/`),与游戏 SDK 靠同一组测试向量对拍锁语义。

## 官方 SDK(仓库内实现 + 测试)

| SDK | 位置 | 形态 | 测试与覆盖率现状 | 文档 |
| --- | --- | --- | --- | --- |
| **C++ 协议核心** | `sdks/core` | 客户端协议栈(连接/心跳/退避重连/踢线终态/钩子),Unreal 壳与桌面端共用 | `sdk_core_tests` 123 例(10 个套件,含 57 例 `ChatClientLoopback` 环回),走 ctest;覆盖率归 C++ 主门禁(`run_coverage.sh` 100% 行覆盖硬门) | [Unreal](/sdk/unreal)(消费方)<br>[sdks/core/README](https://github.com/cuihairu/chirp/tree/main/sdks/core) |
| **Unity (C#)** | `sdks/unity` | 纯 C# 协议栈(零 UnityEngine 依赖)+ `ChirpManager` 主线程派发壳 | `dotnet test` 96 例(帧编解码、序列号关联、重连/踢线、钩子接线、FileMessageStore、回环真实 WebSocket 传输);CI `unity-sdk.yml`(.NET 10 + proto/csharp 漂移检查);coverlet 运行时行覆盖 **97.8%**(剔除 proto 生成代码) | [Unity3D](/sdk/unity3d) |
| **TypeScript** | `sdks/ts`(`@chirp/protocol`) | TS 协议栈,游戏侧 JS/TS 引擎(小游戏/LayaAir/Cocos)接入面;含 `wx.connectSocket` 适配器 | vitest 110 例(8 个文件,含 wx_socket 的 fake-socket 与真实登录回路);CI `web.yml`;vitest 行覆盖 100% | [微信小游戏](/sdk/wechat-minigame)<br>[Cocos Creator](/sdk/cocos-creator)<br>[LayaAir](/sdk/layaair) |
| **Go(服务端)** | `sdks/go` | 服务器平面参考客户端(dial-out、service_id+secret、注入/事件/玩家绑定) | `go test -race` 31 例(进程内 fake hub 环回);CI `go-sdk.yml`(protoc 33.4 重生成防漂移 + vet + -race);client.go 语句覆盖 **100%** | [服务端 SDK](/sdk/server) |
| **Unreal 插件壳** | `sdks/unreal` | `UChirpClientSubsystem` 薄壳(619 行):native 回调转游戏线程 + Blueprint 化,协议全在 C++ core | 无 CI 编译位(UCLASS/Build.cs 需要 UBT 与引擎头,chirp CI 不具备);正确性契约 = C++ core 的 `sdk_core_tests` 123 例 + 极薄转发层 | [Unreal](/sdk/unreal) |

## 按引擎选择接入方式

| 引擎 / 环境 | 接入方式 | 依据 |
| --- | --- | --- |
| Unity 2021.2+ | `sdks/unity` 纯 C# 协议栈 + `ChirpManager` | 官方 SDK,覆盖聊天/社交/语音/组队等消息平面 |
| Unreal Engine | `sdks/unreal` 插件,链接 `libchirp_core_sdk` | C++ core 壳 |
| Godot 4.x (.NET) | 直接编译 `sdks/unity/Runtime/Chirp/` 源文件(零引擎依赖设计) | 复用 Unity C# 协议栈,`ClientWebSocket` 是 .NET 标准库 |
| Cocos Creator 3.x | 搬 `sdks/ts/src/` 协议栈,引擎 `WebSocket` 直连 WS 边缘 | 纯协议,与引擎无关 |
| LayaAir 3.x | 同 Cocos(同构) | 同上 |
| 微信小游戏 | `@chirp/protocol` + `adapters/wx_socket` 传输工厂 | 无 TCP,必须走 WS 边缘 |
| 游戏后端(任意语言) | Go SDK,或按 `server_gateway.proto` 直连(Node/Lua/Python 要点见服务端页) | 服务器平面 dial-out |

选型原则:**先看引擎用什么语言**(C# → Unity 协议栈;TS/JS → sdks/ts;C++ → core/Unreal),再对照各文档页的端口拓扑(chat WS 7001 / TCP 5000、server_gateway TCP 8100)。

## 每日构建(Nightly)

主分支每天自动产出一次未签名的 SDK 产物包(C++ core 库 + Go SDK + TS SDK + 桌面 App .deb,Linux x86_64/aarch64),**两路分发,均不挂 git tag、不发 GitHub Release**:

- **一键安装**(推荐):仓库根 `install.sh`(Linux/macOS)/ `install.ps1`(Windows)自动选平台产物装好——见 README「一键安装」节;
- **手动**:对应 run 页 → **Artifacts → `daily-build`**;或 [nightly-dist 分支镜像](https://github.com/cuihairu/chirp/tree/nightly-dist)(固定文件名 + `manifest.json` 可用面清单,匿名可直链,一键安装的下载源)。

产物未签名,仅供集成联调,不要直接进生产。详见各 SDK 文档页的"每日构建"小节与 [nightly workflow](https://github.com/cuihairu/chirp/blob/main/.github/workflows/nightly.yml)。

## 测试口径备注

- 各 SDK 的数字以仓库当前为准,统计命令:`dotnet test sdks/unity/dotnet/ChirpSdkTests`、`go test -race ./sdks/go/...`、`sdks/ts` 下 `npx vitest run`、`ctest -R sdk_core_tests`;
- 覆盖率口径:C++/Go/TS 走各自 CI 的原生覆盖管线;Unity 用 coverlet cobertura,**剔除 `proto/csharp` 生成代码后按文件行去重**(97.8%,1393/1425);
- Unreal 壳没有独立测试目标:它的行为契约就是 native 核心的行为契约(转发层无协议逻辑)。
