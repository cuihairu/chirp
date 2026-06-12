# Chirp Quick Start

The maintained quick start is now [guide/getting-started.md](./guide/getting-started.md).

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
./test_services.sh --smoke-redis
```

Before using non-core services, read [Core](./CORE.md) and [Capability Matrix](./CAPABILITY_MATRIX.md).
