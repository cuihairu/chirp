# Project Chirp Roadmap

## 1. Infrastructure (Monorepo & C++)

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

- [x] **Auth Service**
  - [x] Login/Logout Logic with password hashing (Argon2)
  - [x] Token Validation (JWT)
  - [x] Multi-device Conflict Handling (Kick logic)
  - [x] Refresh token support
  - [x] Rate limiting (Redis-based)
  - [x] Brute force protection
  - [x] User registration with MySQL persistence

- [x] **Chat Service** (Core)
  - [x] 1v1 Message Routing.
  - [x] Offline Message Storage (Redis → MySQL migration pipeline)
  - [x] History Message Retrieval with pagination
  - [x] Message delivery tracking (ACK/NACK)
  - [x] Hybrid message store (Redis + MySQL)

- [x] **Social & Presence Service**
  - [x] Friend List Management.
  - [x] Real-time Status Sync (Online/In-Game) via Redis Pub/Sub.

## 3. Real-time Voice (WebRTC)

> Target: Low latency team voice for Gaming & Mobile App.

- [x] **Signaling Service**
  - [x] Room Management (Create/Join/Leave).
  - [x] SDP & ICE Candidate Exchange.
- [x] **Voice Client Module**
  - [x] WebRTC Native Integration (C++).
  - [x] Audio Device Management (Mic/Speaker).
  - [x] Network Adaptability (Jitter Buffer, FEC configuration).

## 4. SDKs (in `sdks/`)

- [x] **Core SDK (C++)** (`sdks/core`)
  - [x] Unified interface for Chat & Voice.
  - [x] Cross-platform compilation (Windows/Mac/iOS/Android).
- [x] **Unity SDK** (`sdks/unity` - C# Adapter)
  - [x] C ABI bridge for native plugin
  - [x] C# wrapper with P/Invoke
  - [x] MonoBehaviour component
  - [x] Blueprint functions for all features
- [x] **Unreal SDK** (`sdks/unreal` - UPlugin)
  - [x] UPlugin descriptor
  - [x] Blueprint function library
  - [x] C++ wrapper implementation
  - [x] Event dispatchers

## 5. Client Apps (in `apps/`)

- [x] **Mobile Companion App** (`apps/mobile_companion` - Flutter/RN)
  - [x] **UI Framework**: Contact List, Chat Window, Voice Room.
  - [x] **Background Services**: Push Notifications (APNs/FCM) for keeping users "reachable" when app is killed.
  - [x] **Voice Interaction**: CallKit/ConnectionService integration (System Phone UI).
- [x] **CLI Client** (`apps/cli_client` - C++)
  - [x] Simple terminal based chat client for testing.
- [x] **Load Tester** (`apps/load_tester` - C++)
  - [x] Connection flood testing
  - [x] Message storm testing
  - [x] Statistics reporting

## 6. Operations & Testing

- [x] **Deployment**: Dockerfiles for all services.
- [x] **Orchestration**: Docker Compose / K8s manifests.
- [x] **Load Testing**: Benchmark 10k+ concurrent connections (Gateway).

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
