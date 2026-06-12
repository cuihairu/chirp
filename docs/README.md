# Chirp Documentation

This directory contains the VitePress documentation and a few historical design notes.

## Read First

- [Core](./CORE.md): compact source of truth for the current runtime contract
- [Capability Matrix](./CAPABILITY_MATRIX.md): status by service, SDK, and app
- [Getting Started](./guide/getting-started.md): build, Docker Compose, and smoke tests
- [API Overview](./api/overview.md): packet envelope, message IDs, and core flows
- [Overall Architecture](./architecture.md): topology, service boundaries, and architecture review

## Current Positioning

Chirp is currently best described as:

- a supported backend skeleton for `gateway + auth + chat`
- an experimental playground for distributed chat, richer auth, voice, social, search, notification, and SDK work
- a demo/stub repository for mobile and admin surfaces

Avoid presenting every service, SDK, or app in this repository as stable. Use [Capability Matrix](./CAPABILITY_MATRIX.md) as the status source of truth.

## Documentation Rule

When adding or updating docs, label each capability as one of:

- `Supported`: part of the default documented backend path and should be kept buildable
- `Experimental`: code exists, but the path is conditional, alternate, or not minimal verified runtime
- `Demo`: useful for local exploration, not a stable backend contract
- `Stub`: incomplete or mock-driven

Prefer updating [Core](./CORE.md), [Getting Started](./guide/getting-started.md), [API Overview](./api/overview.md), or [Overall Architecture](./architecture.md) instead of creating another overlapping overview.
