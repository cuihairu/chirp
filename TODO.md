# Project Chirp Roadmap

## 1. Infrastructure (Monorepo & C++)

- [x] **Project Scaffolding**: Setup `libs`, `services`, `proto` directory structure.
- [x] **Build System**: Root `CMakeLists.txt` and module configurations.
- [x] **Protobuf Definitions**: `common`, `auth`, `gateway` schemas.
- [x] **Library - Network**: `chirp_network` (ASIO wrapper, Packet Parser).
- [x] **Library - Common**: Config loader, Logger (spdlog wrapper).

## 2. Backend Services (Microservices)

- [ ] **Gateway Service** (Edge)
  - [x] TCP Server (for Game Clients).
  - [x] WebSocket Server (for Web/Mobile App).
  - [x] Session Management (Local In-Memory).
  - [x] Session Management (Distributed, Redis).
- [ ] **Auth Service**
  - [ ] Login/Logout Logic.
  - [ ] Token Validation (JWT).
  - [ ] Mult-device Conflict Handling (Kick logic).
- [ ] **Chat Service** (Core)
  - [x] 1v1 Message Routing.
  - [ ] Offline Message Storage (Redis List -> MySQL).
  - [x] History Message Retrieval.
- [ ] **Social & Presence Service**
  - [ ] Friend List Management.
  - [ ] Real-time Status Sync (Online/In-Game) via Redis Pub/Sub.

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

- [ ] **Core SDK (C++)** (`sdks/core`)
  - [ ] Unified interface for Chat & Voice.
  - [ ] Cross-platform compilation (Windows/Mac/iOS/Android).
- [ ] **Unity SDK** (`sdks/unity` - C# Adapter)
- [ ] **Unreal SDK** (`sdks/unreal` - UPlugin)

## 5. Client Apps (in `apps/`)

- [ ] **Mobile Companion App** (`apps/mobile_companion` - Flutter/RN)
  - [ ] **UI Framework**: Contact List, Chat Window, Voice Room.
  - [ ] **Background Services**: Push Notifications (APNs/FCM) for keeping users "reachable" when app is killed.
  - [ ] **Voice Interaction**: CallKit/ConnectionService integration (System Phone UI).
- [ ] **CLI Client** (`apps/cli_client` - C++)
  - [ ] Simple terminal based chat client for testing.

## 6. Operations & Testing

- [x] **Deployment**: Dockerfiles for all services.
- [x] **Orchestration**: Docker Compose / K8s manifests.
- [ ] **Load Testing**: Benchmark 10k+ concurrent connections (Gateway).
