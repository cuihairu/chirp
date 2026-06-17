# Chirp Documentation

This directory contains the VitePress documentation for the current runtime, onboarding guides, and a few historical design notes.

## Read First

- [Core](./CORE.md): compact source of truth for the current runtime contract
- [Capability Matrix](./CAPABILITY_MATRIX.md): status by service, SDK, and app
- [Introduction](./guide/introduction.md): short positioning page for new readers
- [Installation](./guide/installation.md): environment setup and dependency notes
- [Getting Started](./guide/getting-started.md): build, Docker Compose, and smoke tests
- [API Overview](./api/overview.md): packet envelope, message IDs, and core flows
- [Overall Architecture](./architecture.md): topology, service boundaries, and architecture review

## Page Map

- `Core docs`: supported runtime path and protocol contract
- `Guide docs`: onboarding and local validation
- `Historical / design notes`: deployment, scalability, game chat, combat, NPC, and integration-test writeups
- `Redirect pages`: `API.md` and `QUICKSTART.md` point to the maintained pages and are kept for compatibility

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
