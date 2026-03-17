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

- [ ] **Signaling Service**
  - [ ] Room Management (Create/Join/Leave).
  - [ ] SDP & ICE Candidate Exchange.
- [ ] **Voice Client Module**
  - [ ] WebRTC Native Integration (C++).
  - [ ] Audio Device Management (Mic/Speaker).
  - [ ] Network Adaptability (Jitter Buffer, FEC configuration).

## 4. SDKs (in `sdks/`)

- [x] **Core SDK (C++)** (`sdks/core`)
  - [x] Unified interface for Chat & Voice.
  - [ ] Cross-platform compilation (Windows/Mac/iOS/Android).
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

- [ ] **Mobile Companion App** (`apps/mobile_companion` - Flutter/RN)
  - [ ] **UI Framework**: Contact List, Chat Window, Voice Room.
  - [ ] **Background Services**: Push Notifications (APNs/FCM) for keeping users "reachable" when app is killed.
  - [ ] **Voice Interaction**: CallKit/ConnectionService integration (System Phone UI).
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

## Recent Updates (2026-03-17)

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
