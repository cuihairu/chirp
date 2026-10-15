> 🌐 Language: [中文](./README.zh.md) | **English** (this page)

# Chirp Documentation

This directory holds the VitePress documentation: notes on the current runtime, getting-started guides, and a set of historical design notes.

## Read these first

- [Core](./CORE.md): a condensed source of truth for the current runtime contract
- [Capability Matrix](./CAPABILITY_MATRIX.md): current state by service, SDK and application
- [Introduction](./guide/introduction.md): a positioning page for new readers, very short
- [Installation](./guide/installation.md): environment setup and dependencies
- [Getting Started](./guide/getting-started.md): build, Docker Compose and smoke tests
- [API Overview](./api/overview.md): packet framing, message IDs and core flows
- [Overall Architecture](./architecture.md): topology, service boundaries and architecture review conclusions

## Page map

- `Core docs` (this directory + `guide/` + `api/`): the supported runtime paths and protocol contracts
- `design-notes/`: historical/design articles — deployment, scalability, game chat, combat, NPC dialog, integration-test notes and so on; kept for reference only and **not** maintained as current contracts
- `Redirect pages`: `API.md` and `QUICKSTART.md` point at maintained pages and are kept only for old-link compatibility
- `server_plane.md`: the integration contract for the server plane (game backend <-> chirp)

## Current positioning

The most accurate description of Chirp today is:

- One supported backend trunk: `gateway + auth + chat`
- A proving ground: distributed chat, more complete authentication, voice, social, search, notification and SDK work are all tried here
- Demo/placeholder repositories for the mobile and admin ends

Please do not present every service, SDK or application in this repository as a stable capability. The current state is defined by the [Capability Matrix](./CAPABILITY_MATRIX.md).

## Documentation writing rules

When adding or updating documentation, every capability must be labeled with one of these four statuses:

- `Supported`: part of the default documented backend trunk, must stay buildable
- `Experimental`: the code exists, but the path is conditional, alternative, or outside the minimal verified runtime
- `Demo`: suitable for local exploration, not a stable backend contract
- `Stub`: unfinished or mock-driven

If you can update [Core](./CORE.md), [Getting Started](./guide/getting-started.md), [API Overview](./api/overview.md) or [Overall Architecture](./architecture.md), do not start another overlapping overview.
