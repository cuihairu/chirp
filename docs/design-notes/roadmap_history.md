> 🌐 Language: [中文](../zh/design-notes/roadmap_history.md)) | **English** (this page)

# Chirp Roadmap History (Archive)

> Archived from TODO.md in 2026-09. This file is a historical record of the early roadmap and change log; the checked states reflect the repository as of archiving time and have been cross-checked against [../CAPABILITY_MATRIX.md](../CAPABILITY_MATRIX.md)). For current work see [TODO.md](https://github.com/cuihairu/chirp/blob/main/TODO.md)).

## 1. Infrastructure (Monorepo and C++)

> Note: sections 1–6 below are the historical roadmap; the checked states were aligned with the [capability matrix](../CAPABILITY_MATRIX.md)) in 2026-09. `[x]` only means the code exists; items without an annotation are core Supported paths, and items annotated **Experimental** / **Demo** / **Stub** must not be treated as stable capabilities.

- [x] **Project scaffolding**: set up the `libs`, `services`, `proto` directory structure.
- [x] **Build system**: root `CMakeLists.txt` and per-module configuration.
- [x] **Protobuf definitions**: `common`, `auth`, `gateway` schemas.
- [x] **Network library**: `chirp_network` (ASIO wrapper, Packet parsing).
- [x] **Common libraries**: config loader, logger (spdlog wrapper).

## 2. Backend Services (Microservices)

- [x] **Gateway service** (edge)
  - [x] TCP server (for game clients).
  - [x] WebSocket server (for web/mobile apps).
  - [x] Session management (local memory).
  - [x] Session management (distributed, Redis).

- [x] **Auth service** (basic token flow = Supported; the items below are the enhanced implementation, **Experimental**, conditionally built on MySQL + libsodium, and currently zero unit tests — see P2)
  - [x] Login/logout logic, password hashing (Argon2)
  - [x] Token validation (JWT)
  - [x] Multi-device conflict handling (kick logic)
  - [x] Refresh token support
  - [x] Rate limiting (Redis-based)
  - [x] Brute-force protection
  - [x] User registration, MySQL persistence

- [x] **Chat service** (core) (basic messaging = Supported; storage enhancements and the distributed form are **Experimental**)
  - [x] 1v1 message routing.
  - [x] Offline message storage (Redis → MySQL migration pipeline) **(Experimental)**
  - [x] History retrieval, with pagination
  - [x] Message delivery tracking (ACK/NACK) **(Experimental)**
  - [x] Hybrid message storage (Redis + MySQL) **(Experimental — and the enhanced build is missing server plane/push bridge files, see P1)**
    - [x] Export MySQL-compatible SQL from Redis history/offline queues
    - [x] Archival scripts and background worker
  - [x] Distributed routing (`chirp_chat_distributed` target) **(Experimental)**

- [x] **Social & presence service** **(Experimental — not verified as a core path, zero unit tests)**
  - [x] Friend list management.
  - [x] Real-time status sync (online/in-game), via Redis Pub/Sub.

## 3. Real-time Voice (WebRTC) **(Experimental — heavy environment dependencies, not on the minimal verification path, zero unit tests)**

> Goal: low-latency party voice for games and mobile apps.

- [x] **Signaling service**
  - [x] Room management (create/join/leave).
  - [x] SDP and ICE candidate exchange.
- [x] **Voice client module**
  - [x] WebRTC native integration (C++).
  - [x] Audio device management (microphone/speaker).
  - [x] Network adaptivity (jitter buffer, FEC configuration).

## 4. SDKs (`sdks/`) **(Experimental — must not be claimed stable)**

- [x] **Core SDK (C++)** (`sdks/core`) **(Experimental)**
  - [x] Unified interface for chat and voice.
  - [x] Cross-platform compilation (Windows/Mac/iOS/Android).
- [x] **Unity SDK** (`sdks/unity` - C# adapter layer) **(Experimental — social bindings contain TODOs)**
  - [x] C ABI bridge (native plugin)
  - [x] C# wrapper (P/Invoke)
  - [x] MonoBehaviour components
  - [ ] Full-featured Blueprint functions **(the social part is a TODO placeholder)**
- [x] **Unreal SDK** (`sdks/unreal` - UPlugin) **(Experimental — contains unimplemented return paths)**
  - [x] UPlugin descriptor
  - [x] Blueprint function library
  - [ ] C++ wrapper implementation **(some functions are unimplemented return paths)**
  - [x] Event dispatcher

## 5. Client Applications (`apps/`)

- [x] **Mobile companion app** (`apps/mobile_companion` - Flutter/RN) **(Demo — must not be presented as production-ready)** (removed on 2026-09-29 along with the native migration: the protocol core was ported into `apps/android`/`apps/ios` with same-shape test vectors cross-checked; the CI leg `mobile-build.yml` and `proto/dart` were deleted in the same batch)
  - [x] **UI framework**: contacts list, chat window, voice rooms.
  - [ ] **Background services**: push notifications (APNs/FCM), keeping users "reachable" after the app is killed. **(default log transport; `--push_transport http` turns on real HTTP/1.1+TLS provider delivery, APNs needs HTTP/2 via a protocol-converting front deployed ahead of it, see P2)**
  - [x] **Voice interaction**: CallKit/ConnectionService integration (system phone UI).
- [x] **CLI client** (`apps/cli_client` - C++) **(Demo — used for smoke/manual verification)**
  - [x] A simple terminal chat client, used for testing.
- [x] **Load testing tool** (`apps/load_tester` - C++) **(Demo)**
  - [x] Connection flood testing
  - [x] Message storm testing
  - [x] Statistics reporting
- [ ] **Admin dashboard** (`apps/admin_dashboard`) **(Stub — pages first attempt real HTTP calls to `/api/{stats,users,channels,messages}`, falling back to built-in mock demo data on failure; there is no `/api/*` backend service in the repo, still not wired to a real backend)**

## 6. Operations & Testing

- [x] **Deployment**: Dockerfiles for all services. **(Supported)**
- [x] **Orchestration**: Docker Compose / K8s manifests. **(Docker Compose = Supported; K8s is a draft)**
- [x] **Load testing**: 10k+ concurrent connection benchmark for the gateway. Measured evidence in [capacity_benchmark](/design-notes/capacity_benchmark); the capacity numbers have been publicly claimed.

## Recent Updates (2026-03-18)

### Mobile Companion App (Flutter)
- Added a voice room screen with WebRTC integration
- Added a participant list with speaking indicators
- Added mute/unmute controls
- Added speaker switching
- Integrated ChirpClient for voice signaling
- Added a voice room entry on the home page

### Android Native Integration
- Created MainActivity.kt with native method channels
- Implemented VoiceForegroundService for background calls
- Implemented VoiceBroadcastReceiver responding to notification actions
- Implemented VoiceCallNotification for the incoming-call UI
- Implemented ChirpFirebaseMessagingService for push notifications
- Created AndroidManifest.xml with all required permissions
- Added build.gradle with Firebase and Kotlin dependencies

### iOS Native Integration
- Created AppDelegate.swift with CallKit integration
- Implemented PKPushRegistryDelegate for VoIP push
- Implemented CXProviderDelegate for call management
- Added audio focus and speakerphone controls
- Created Info.plist with microphone/camera permissions
- Created a Podfile with Firebase and CallKit dependencies

### Cross-platform Build Scripts
- Created build_all.sh for macOS/Linux builds
- Created build_all.bat for Windows builds
- Created the iOS.cmake toolchain file
- Created the linux-gcc.cmake toolchain file
- Created an all-platform Unity plugin build script
- Added iOS XCFramework packaging

### WebRTC Native Client Module
- Created the WebRTC abstraction layer webrtc_client.h
- Created webrtc_client.cc with audio device management
- Wired the WebRTC client into the voice module
- Added volume monitoring
- Added a device enumeration API
- Added input/output enable/disable controls

### Auth Service Enhancements
- Added the `refresh_tokens` table, refresh token persistence
- Added the `failed_login_attempts` table, brute-force tracking
- Added the `password_reset_tokens` table, password recovery
- Implemented the Argon2id `PasswordHasher`
- Implemented the `TokenGenerator` for secure token generation
- Implemented the `UserStore` for MySQL user CRUD
- Implemented the `SessionStore` for multi-device session tracking
- Implemented the `RedisAuthStore` for distributed sessions
- Implemented `RateLimiter` and `BruteForceProtector`
- Complete auth service: registration, password login, refresh token

### Chat Service Enhancements
- Created `MessageStoreConfig` for centralized configuration
- protobuf extensions for ACK/NACK and paginated messages
- `MySQLMessageStore` gained batch operations
- Created the `HybridMessageStore` interface for Redis + MySQL dual writes
- Created `MessageDeliveryTracker` for ACK/NACK tracking
- Created `MessageMigrationWorker` for background Redis→MySQL migration
- Created `PaginatedHistoryRetriever` for cross-source pagination

### Unity SDK
- Created the C ABI bridge (`chirp_unity_bridge.h/cc`)
- Created the C# wrapper for P/Invoke (`ChirpSDK.cs`)
- Created the MonoBehaviour component (`ChirpManager.cs`)
- Native plugin build configuration (`CMakeLists.txt`)
- Supports Windows, macOS, Linux, iOS, Android

### Unreal SDK
- Created the UPlugin descriptor (`ChirpSDK.uplugin`)
- Created a Blueprint function library covering all chat, social and voice features
- Created the C++ wrapper implementation (`ChirpClient.h/cpp`)
- Created the Build.cs for module configuration
- Blueprint event dispatcher

### Load Testing Tool
- Created the `LoadTester` framework in `apps/load_tester/`
- Connection flood scenario (ramping up to N connections)
- Message storm scenario (high throughput)
- Mixed workload scenario (close to real usage)
- Statistics reporting with latency metrics

## Recent Updates (2026-03-18 cont.)

### Discord-style Channel System
- Created `channel_manager.h/cc` for hierarchical channel management
- Channel categories for organization
- Text/voice/announcement/stage/forum channel types
- Permission system with role/user overrides
- Channel slow-mode support
- Voice channel participant management

### Message Reactions
- Created `reaction_manager.h/cc` for emoji reactions
- Add/remove reactions with aggregation
- Batch reaction fetching for multiple messages
- Popular reactions for UI display

### Message Edit/Delete
- Created `message_edit_manager.h/cc` for message editing
- Configurable edit time window (default 15 minutes)
- Edit history tracking with a cap
- Soft delete with a retention period
- Hard delete for admins
- Batch delete operations

### Typing Indicators
- Created `typing_manager.h/cc` for real-time typing feedback
- Broadcast typing state to channel participants
- Configurable timeout (default 10 seconds)
- Cooldown to prevent excessive broadcasting

### Mention System
- Created `mention_manager.h/cc` for @mention parsing
- Support for @user, @role, @channel, @everyone, @here
- Cooldown for mass mentions
- Notification recipient construction
- Autocomplete support hooks

### File Sharing
- Created `file_storage_manager.h/cc` for file upload/download
- Upload preparation with pre-signed URLs
- File type validation (images, video, audio, documents)
- Configurable size cap (default 100MB)
- Download URL generation
- Virus scanning hook

### Prometheus Metrics
- Created `metrics.h/cc` for metric collection
- Counter, Gauge, Histogram metric types
- HTTP server for the /metrics endpoint
- Health check endpoint
- Simple implementation (no external dependencies)

### Mobile CI/CD
- Created `.github/workflows/mobile-build.yml`
- Android APK/AppBundle builds
- iOS build workflow
- Flutter code analysis
- Native SDK cross-platform builds

## Recent Updates (2026-03-18 cont. 2)

### Message Edit/Delete
- Created `message_edit_manager.h/cc` for message editing
- Configurable edit time window (default 15 minutes)
- Edit history tracking with a cap
- Soft delete with a retention period
- Hard delete for admins
- Batch delete operations

### Typing Indicators
- Created `typing_manager.h/cc` for real-time typing feedback
- Broadcast typing state to channel participants
- Configurable timeout (default 10 seconds)
- Cooldown to prevent excessive broadcasting

### Mention System
- Created `mention_manager.h/cc` for @mention parsing
- Support for @user, @role, @channel, @everyone, @here
- Cooldown for mass mentions
- Notification recipient construction
- Autocomplete support hooks

### File Sharing
- Created `file_storage_manager.h/cc` for file upload/download
- Upload preparation with pre-signed URLs
- File type validation (images, video, audio, documents)
- Configurable size cap (default 100MB)
- Download URL generation
- Virus scanning hook

### Push Notification Service
- Created `services/notification` supporting FCM/APNs
- Device registration and token management
- Message, mention and incoming-call notifications
- Silent notification support
- Badge count management
- Notification cooldown against spam
- Inactive device cleanup

### Presence Enhancements
- Created `presence_manager_v2.h/cc` for multi-device presence
- Status types: online, idle, do-not-disturb, invisible, in-game, in-voice, in-call
- Custom status messages with emoji
- Activity detection and idle/offline timeouts
- Per-device session management
- Batch presence queries
- Online friend detection

### Message Search Service
- Created `services/search` with a full-text index
- Inverted index for fast text search
- Filtering by channel, sender, time range, type
- Phrase matching and relevance scoring
- Highlighted snippets with context
- Query suggestions/autocomplete
- Search statistics tracking

### Web Admin Dashboard
- Created the React app `apps/admin_dashboard`
- Real-time metrics panels with charts
- User management with search and actions
- Channel management and monitoring
- Message search UI
- System settings panel
- Material-UI dark theme
- Fast development with Vite
