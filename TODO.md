# Project Chirp Roadmap

## Current Focus（2026-09，最高优先级）

> 决策：**优先把"游戏客户端 SDK 接入 + 游戏服务端接入"端到端跑通，其余（含下方架构债 P0–P3）等基础功能完成后再做。**

- [x] **游戏客户端 SDK 进程级 E2E**：`sdks/core`（`chirp::sdk::ChatClient`）直连 chat 的登录/双向收发/离线队列链路，已纳入 `test_services.sh --smoke-sdk`（sdk_example 已参数化，dev/ci preset 默认构建）
- [x] **游戏服务端接入 E2E**：`test_services.sh --smoke-npc` 已覆盖（server_gateway 注入 + 事件回环 + 离线队列 refill）
- [ ] 两条 smoke 纳入 CI(`.github/workflows` 当前未跑 test_services.sh 的 smoke)

## 0. Architecture Debt（架构欠债，2026-09 架构评审）

> 来源：架构评审（见 `docs/architecture.md` "Is The Architecture Reasonable?"）。按优先级排列，P0 为最优先。完成一项勾掉一项。**注意：本节整体让位于 Current Focus，基础功能跑通后再启动。**

### P0 — 公共代码沉淀（消除跨服务私有耦合）

- [ ] **抽取公共 session registry**：`services/gateway/src/gateway_session_registry.{h,cc}` 与 `services/chat/src/chat_session_registry.{h,cc}` 几乎逐行重复（仅改名）。沉淀为 `libs/` 下的公共组件，gateway / chat / app_gateway 共用。
- [ ] **消除跨服务直接编译对方源码**：`services/app_gateway/CMakeLists.txt:13-16` 直接编译 gateway 的 3 个 .cc 和 notification 的 `notification_client.cc`；`services/chat/CMakeLists.txt:39,74` 直接 include/编译 notification 的私有源码。将 `auth_client`、`redis_session_manager`、`notification_client` 沉淀为公共库，服务间只通过协议或公共库交互。

### P1 — 登录语义统一（对应 migration path 第 4 步）

- [ ] **统一登录/会话语义**：gateway 与 chat 各自处理 `LOGIN_REQ`，且 chat 版是 "token 即 user_id" 脚手架（`services/chat/src/main.cc:265`）。目标拓扑中 chat 应成为内部服务，由 `game_gateway` 吸收直连入口，客户端只认边缘。
- [ ] **设备级会话核心**：Redis session registry 从 "user → instance" 升级为 "user → device → edge instance"，支撑 app 边缘与跨端语义（跨设备投递、kick 策略、统一未读数）。

### P1 — 构建一致性

- [ ] **修复 chat 增强构建功能缺失**：`services/chat/CMakeLists.txt:44-64` 的 MySQL 增强分支遗漏 `inject_consumer.cc`、`server_gateway_peer.cc`、`push_bridge.cc`、`channel_manager.cc` 等，导致增强构建丢失服务器平面集成与推送桥能力。三个 main（`main.cc` / `main_enhanced.cc` / `main_distributed.cc`）功能应对等或在文档中明确差异。
- [ ] **推送桥覆盖全部 chat 构建**：PushBridge 目前只接入默认 `chirp_chat`，`main_enhanced` / `main_distributed` 未接。
- [ ] **proto 改为链接 `chirp_protos` 静态库**：9 个服务各自 `file(GLOB ...)` 重复编译全部 .pb.cc，应统一链接 `proto/CMakeLists.txt` 已构建的 `chirp_protos`。

### P2 — 测试补齐

- [ ] **auth 单测**：Supported 服务但零单测（enhanced 路径的 user_store / session_store / rate_limiter / brute_force 等）。
- [ ] **app_gateway / social / voice 单测**：三个服务目前没有任何单测。
- [ ] **server plane 进程级 E2E smoke**：注入链路仅回环级验证，需补进程级端到端 smoke（`test_services.sh` 增加 `--smoke-server-plane`）。
- [ ] **真实推送传输**：notification 的 APNs HTTP/2 / FCM HTTP 投递仍是日志 stub（`PushTransport` 接缝已留好，需真实实现 + TLS）。

### P2 — 协议演进

- [ ] **MsgID 去中心化**：单一全局枚举（`proto/gateway.proto`，~180 值）意味着任何服务加消息都要改 gateway.proto。评估按平面拆分枚举或改用分段常量文件，保持号段约定不变。

### P3 — 仓库卫生

- [ ] **清理根目录覆盖率产物**：上百个 `*.gcov` / `*.gcov.json.gz` 和 `build-cov/`、`vcpkg_installed/` 污染源码树，加入 `.gitignore` 并从仓库移除。
- [ ] **移除死代码**：`services/router/` 空目录及顶层 `CMakeLists.txt:165` 被注释的 `add_subdirectory(services/router)`。
- [ ] **清理命名冗余/历史包袱**：`websocket_util.cc` 与 `websocket_utils.h` 并存；`presence_manager_v2` 只有 v2 没有 v1；libs/common 自研 sha256/base64 与 auth 的 libsodium 两套实现并存（评估统一或文档说明边界）。

## 1. Infrastructure (Monorepo & C++)

> 注：以下第 1–6 节为历史 roadmap，勾选状态已于 2026-09 按 [能力矩阵](docs/CAPABILITY_MATRIX.md) 对齐。`[x]` 仅表示代码存在；未标注的为核心 Supported 路径，标注 **Experimental** / **Demo** / **Stub** 的不应视为稳定能力。

- [x] **Project Scaffolding**: Setup `libs`, `services`, `proto` directory structure.
- [x] **Build System**: Root `CMakeLists.txt` and module configurations.
- [x] **Protobuf Definitions**: `common`, `auth`, `gateway` schemas.
- [x] **Library - Network**: `chirp_network` (ASIO wrapper, Packet Parser).
- [x] **Library - Common**: Config loader, Logger (spdlog wrapper).

## 2. Backend Services (Microservices)

- [x] **Gateway Service** (Edge)
  - [x] TCP Server (for Game Clients).
  - [x] WebSocket Server (for Web/Mobile App).
  - [x] Session Management (Local In-Memory).
  - [x] Session Management (Distributed, Redis).

- [x] **Auth Service**（basic token flow = Supported；以下为增强实现，**Experimental**，依赖 MySQL + libsodium 条件构建，且目前零单测 — 见 P2）
  - [x] Login/Logout Logic with password hashing (Argon2)
  - [x] Token Validation (JWT)
  - [x] Multi-device Conflict Handling (Kick logic)
  - [x] Refresh token support
  - [x] Rate limiting (Redis-based)
  - [x] Brute force protection
  - [x] User registration with MySQL persistence

- [x] **Chat Service** (Core)（basic messaging = Supported；存储增强与分布式为 **Experimental**）
  - [x] 1v1 Message Routing.
  - [x] Offline Message Storage (Redis → MySQL migration pipeline) **(Experimental)**
  - [x] History Message Retrieval with pagination
  - [x] Message delivery tracking (ACK/NACK) **(Experimental)**
  - [x] Hybrid message store (Redis + MySQL) **(Experimental — 且增强构建缺失 server plane/push bridge 文件，见 P1)**
    - [x] MySQL-compatible SQL export from Redis history/offline queues
    - [x] Archive scripts and loop worker
  - [x] Distributed routing (`chirp_chat_distributed` target) **(Experimental)**

- [x] **Social & Presence Service** **(Experimental — 未验证为核心路径，零单测)**
  - [x] Friend List Management.
  - [x] Real-time Status Sync (Online/In-Game) via Redis Pub/Sub.

## 3. Real-time Voice (WebRTC) **(Experimental — 环境依赖重，不在最小验证路径，零单测)**

> Target: Low latency team voice for Gaming & Mobile App.

- [x] **Signaling Service**
  - [x] Room Management (Create/Join/Leave).
  - [x] SDP & ICE Candidate Exchange.
- [x] **Voice Client Module**
  - [x] WebRTC Native Integration (C++).
  - [x] Audio Device Management (Mic/Speaker).
  - [x] Network Adaptability (Jitter Buffer, FEC configuration).

## 4. SDKs (in `sdks/`) **(Experimental — 不应宣称稳定)**

- [x] **Core SDK (C++)** (`sdks/core`) **(Experimental)**
  - [x] Unified interface for Chat & Voice.
  - [x] Cross-platform compilation (Windows/Mac/iOS/Android).
- [x] **Unity SDK** (`sdks/unity` - C# Adapter) **(Experimental — social 绑定含 TODO)**
  - [x] C ABI bridge for native plugin
  - [x] C# wrapper with P/Invoke
  - [x] MonoBehaviour component
  - [ ] Blueprint functions for all features **(social 部分为 TODO 占位)**
- [x] **Unreal SDK** (`sdks/unreal` - UPlugin) **(Experimental — 存在未实现的返回路径)**
  - [x] UPlugin descriptor
  - [x] Blueprint function library
  - [ ] C++ wrapper implementation **(部分函数为未实现的返回路径)**
  - [x] Event dispatchers

## 5. Client Apps (in `apps/`)

- [x] **Mobile Companion App** (`apps/mobile_companion` - Flutter/RN) **(Demo — 不应呈现为生产就绪)**
  - [x] **UI Framework**: Contact List, Chat Window, Voice Room.
  - [ ] **Background Services**: Push Notifications (APNs/FCM) for keeping users "reachable" when app is killed. **(provider HTTP 投递仍为日志 stub，见 P2)**
  - [x] **Voice Interaction**: CallKit/ConnectionService integration (System Phone UI).
- [x] **CLI Client** (`apps/cli_client` - C++) **(Demo — 用于 smoke/手工验证)**
  - [x] Simple terminal based chat client for testing.
- [x] **Load Tester** (`apps/load_tester` - C++) **(Demo)**
  - [x] Connection flood testing
  - [x] Message storm testing
  - [x] Statistics reporting
- [ ] **Admin Dashboard** (`apps/admin_dashboard`) **(Stub — 使用 mock 数据与演示页面，未接真实后端)**

## 6. Operations & Testing

- [x] **Deployment**: Dockerfiles for all services. **(Supported)**
- [x] **Orchestration**: Docker Compose / K8s manifests. **(Docker Compose = Supported；K8s 为草案)**
- [ ] **Load Testing**: Benchmark 10k+ concurrent connections (Gateway). **(容量数字需实测证据后方可对外宣称 — 见 architecture.md)**

## Recent Updates (2026-03-18)

### Mobile Companion App (Flutter)
- Added voice room screen with WebRTC integration
- Added participant list with speaking indicators
- Added mute/unmute controls
- Added speaker toggle
- Integrated with ChirpClient for voice signaling
- Enhanced home screen with voice room navigation

### Native Android Integration
- Created MainActivity.kt with native method channel
- Implemented VoiceForegroundService for background calls
- Implemented VoiceBroadcastReceiver for notification actions
- Implemented VoiceCallNotification for incoming call UI
- Implemented ChirpFirebaseMessagingService for push notifications
- Created AndroidManifest.xml with all required permissions
- Added build.gradle with Firebase and Kotlin dependencies

### Native iOS Integration
- Created AppDelegate.swift with CallKit integration
- Implemented PKPushRegistryDelegate for VoIP pushes
- Implemented CXProviderDelegate for call management
- Added audio focus and speakerphone control
- Created Info.plist with microphone/camera permissions
- Created Podfile with Firebase and CallKit dependencies

### Cross-Platform Build Scripts
- Created build_all.sh for macOS/Linux builds
- Created build_all.bat for Windows builds
- Created iOS.cmake toolchain file
- Created linux-gcc.cmake toolchain file
- Created Unity plugin build scripts for all platforms
- Added XCFramework creation for iOS

### WebRTC Native Client Module
- Created webrtc_client.h with WebRTC abstraction
- Created webrtc_client.cc with audio device management
- Integrated WebRTC client with voice module
- Added audio level monitoring
- Added device enumeration APIs
- Added input/output enable/disable controls

### Auth Service Enhancement
- Added `refresh_tokens` table for refresh token persistence
- Added `failed_login_attempts` table for brute force tracking
- Added `password_reset_tokens` table for password recovery
- Implemented `PasswordHasher` with Argon2id
- Implemented `TokenGenerator` for secure token generation
- Implemented `UserStore` for MySQL user CRUD operations
- Implemented `SessionStore` for multi-device session tracking
- Implemented `RedisAuthStore` for distributed sessions
- Implemented `RateLimiter` and `BruteForceProtector`
- Full auth service with registration, password login, refresh tokens

### Chat Service Enhancement
- Created `MessageStoreConfig` for centralized configuration
- Extended protobuf with ACK/NACK and pagination messages
- Added batch operations to `MySQLMessageStore`
- Created `HybridMessageStore` dual-write interface (Redis + MySQL)
- Created `MessageDeliveryTracker` for ACK/NACK tracking
- Created `MessageMigrationWorker` for background Redis→MySQL migration
- Created `PaginatedHistoryRetriever` for cross-source pagination

### Unity SDK
- Created C ABI bridge (`chirp_unity_bridge.h/cc`)
- Created C# wrapper with P/Invoke (`ChirpSDK.cs`)
- Created MonoBehaviour component (`ChirpManager.cs`)
- Native plugin build configuration (`CMakeLists.txt`)
- Support for Windows, macOS, Linux, iOS, Android

### Unreal SDK
- Created UPlugin descriptor (`ChirpSDK.uplugin`)
- Created Blueprint function library with all chat, social, voice functions
- Created C++ wrapper implementation (`ChirpClient.h/cpp`)
- Created Build.cs for module configuration
- Event dispatchers for Blueprint events

### Load Testing Tool
- Created `LoadTester` framework in `apps/load_tester/`
- Connection flood scenario (ramp to N connections)
- Message storm scenario (high throughput)
- Mixed load scenario (realistic usage)
- Statistics reporting with latency metrics

## Recent Updates (2026-03-18 Continued)

### Discord-like Channel System
- Created `channel_manager.h/cc` for hierarchical channel management
- Channel categories for organization
- Text/Voice/Announcement/Stage/Forum channel types
- Permission system with role/user overrides
- Slow mode support for channels
- Voice channel participant management

### Message Reactions
- Created `reaction_manager.h/cc` for emoji reactions on messages
- Add/remove reactions with aggregation
- Bulk reaction retrieval for multiple messages
- Top reactions for UI display

### Message Edit/Delete
- Created `message_edit_manager.h/cc` for message editing
- Configurable edit time window (default 15 minutes)
- Edit history tracking with limit
- Soft delete with retention period
- Hard delete for moderators
- Bulk delete operations

### Typing Indicators
- Created `typing_manager.h/cc` for real-time typing feedback
- Typing broadcast to channel participants
- Configurable timeout (default 10 seconds)
- Cooldown to prevent excessive broadcasts

### Mention System
- Created `mention_manager.h/cc` for @mentions parsing
- @user, @role, @channel, @everyone, @here support
- Cooldown for mass mentions
- Notification recipient building
- Autocomplete support hooks

### File Sharing
- Created `file_storage_manager.h/cc` for file uploads/downloads
- Prepare upload with presigned URLs
- File type validation (images, videos, audio, documents)
- Configurable size limits (default 100MB)
- Download URL generation
- Virus scan hooks

### Prometheus Metrics
- Created `metrics.h/cc` for metrics collection
- Counter, Gauge, Histogram metric types
- HTTP server for /metrics endpoint
- Health check endpoint
- Simple implementation (no external dependency)

### Mobile CI/CD
- Created `.github/workflows/mobile-build.yml`
- Android APK/AppBundle builds
- iOS build workflow
- Flutter code analysis
- Native SDK cross-platform builds

## Recent Updates (2026-03-18 Continued)

### Message Edit/Delete
- Created `message_edit_manager.h/cc` for message editing
- Configurable edit time window (default 15 minutes)
- Edit history tracking with limit
- Soft delete with retention period
- Hard delete for moderators
- Bulk delete operations

### Typing Indicators
- Created `typing_manager.h/cc` for real-time typing feedback
- Typing broadcast to channel participants
- Configurable timeout (default 10 seconds)
- Cooldown to prevent excessive broadcasts

### Mention System
- Created `mention_manager.h/cc` for @mentions parsing
- @user, @role, @channel, @everyone, @here support
- Cooldown for mass mentions
- Notification recipient building
- Autocomplete support hooks

### File Sharing
- Created `file_storage_manager.h/cc` for file uploads/downloads
- Prepare upload with presigned URLs
- File type validation (images, videos, audio, documents)
- Configurable size limits (default 100MB)
- Download URL generation
- Virus scan hooks

### Push Notification Service
- Created `services/notification` with FCM/APNs support
- Device registration and token management
- Message, mention, and call notifications
- Silent notification support
- Badge count management
- Notification cooldown to prevent spam
- Inactive device cleanup

### Enhanced User Presence
- Created `presence_manager_v2.h/cc` for multi-device presence
- Status types: Online, Idle, DND, Invisible, In-Game, In-Voice, In-Call
- Custom status messages with emoji support
- Activity detection and idle/offline timeout
- Session management per device
- Bulk presence queries
- Online friend detection

### Message Search Service
- Created `services/search` with full-text indexing
- Inverted index for fast text search
- Filter by channel, sender, time range, type
- Phrase matching and relevance scoring
- Highlighted snippets with context
- Query suggestions/autocomplete
- Search statistics tracking

### Web Admin Dashboard
- Created `apps/admin_dashboard` React application
- Real-time metrics dashboard with charts
- User management with search and actions
- Channel management and monitoring
- Message search interface
- System settings panel
- Material-UI dark theme
- Vite for fast development
