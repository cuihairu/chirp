> 🌐 Language: [中文](./QUICKSTART.md) | **English** (this page)

# Chirp Quickstart

The maintained getting-started guide has moved to [guide/getting-started.md](./guide/getting-started.en.md).

Minimal local path:

```bash
./gen_proto.sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Smoke tests:

```bash
./test_services.sh --smoke
./test_services.sh --smoke-chat
./test_services.sh --smoke-sdk
./test_services.sh --smoke-npc
./test_services.sh --smoke-edge
./test_services.sh --smoke-jwt
./test_services.sh --smoke-redis
./test_services.sh --smoke-game
./test_services.sh --smoke-voice
./test_services.sh --smoke-party
./test_services.sh --smoke-search # search plane: FTS5 index + gateway --search_host pipe (needs MySQL)
```

Before enabling non-core services, read [Core](./CORE.en.md) and the [Capability Matrix](./CAPABILITY_MATRIX.en.md).
