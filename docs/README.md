# Chirp Documentation

This directory contains the project documentation used by VitePress and by repository readers.

## Read First

- [Overall Architecture](./architecture.md): current runtime topology, service boundaries, protocol baseline, and architecture review
- [Capability Matrix](./CAPABILITY_MATRIX.md): implementation status by service, SDK, and app
- [Getting Started](./guide/getting-started.md): local build, Docker Compose, and smoke tests
- [API Overview](./api/overview.md): supported packet envelope and core message flows

## Current Positioning

Chirp is currently best described as:

- a supported backend skeleton for `gateway + auth + chat`
- an experimental playground for distributed chat, richer auth, voice, social, search, notification, and SDK work
- a demo/stub repository for mobile and admin surfaces

Avoid presenting every service, SDK, or app in this repository as stable. Use [Capability Matrix](./CAPABILITY_MATRIX.md) as the source of truth for status language.

## Documentation Map

| Document | Purpose |
| --- | --- |
| [architecture.md](./architecture.md) | Primary architecture overview and reasonableness review |
| [game_chat_architecture.md](./game_chat_architecture.md) | Game-chat design notes and target shape |
| [QUICKSTART.md](./QUICKSTART.md) | Short local start guide |
| [DEPLOYMENT.md](./DEPLOYMENT.md) | Deployment notes; verify against current service flags before production use |
| [DISTRIBUTED_DEPLOYMENT.md](./DISTRIBUTED_DEPLOYMENT.md) | Distributed deployment notes and cluster examples |
| [SCALABILITY.md](./SCALABILITY.md) | Scalability design notes and roadmap |
| [API.md](./API.md) | Broader API notes, including experimental surfaces |
| [api/overview.md](./api/overview.md) | VitePress API overview for the current protocol |

## Documentation Rule

When adding docs, label each capability as one of:

- `Supported`: part of the default documented backend path and should be kept buildable
- `Experimental`: code exists, but the path is conditional, alternate, or not minimal verified runtime
- `Demo`: useful for local exploration, not a stable backend contract
- `Stub`: incomplete or mock-driven
