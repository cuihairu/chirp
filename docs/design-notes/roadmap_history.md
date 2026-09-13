# Chirp Roadmap History (Archived)

> Archived from TODO.md on 2026-09. This file is a historical record of the early roadmap and update logs; completion marks reflect repository state at archive time, cross-checked against [../CAPABILITY_MATRIX.md](../CAPABILITY_MATRIX.md). For current work see [TODO.md](../../TODO.md).

## 1. Infrastructure (Monorepo & C++)

> 注：以下第 1–6 节为历史 roadmap，勾选状态已于 2026-09 按 [能力矩阵](../CAPABILITY_MATRIX.md) 对齐。`[x]` 仅表示代码存在；未标注的为核心 Supported 路径，标注 **Experimental** / **Demo** / **Stub** 的不应视为稳定能力。

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
