# Chirp 快速上手

受维护的快速上手指南已迁移到 [guide/getting-started.md](./guide/getting-started.md)。

本地最小路径:

```bash
./gen_proto.sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

冒烟测试(smoke test):

```bash
./test_services.sh --smoke
./test_services.sh --smoke-chat
./test_services.sh --smoke-sdk
./test_services.sh --smoke-npc
./test_services.sh --smoke-redis
```

启用非核心服务前,先读 [Core](./CORE.md) 和 [Capability Matrix](./CAPABILITY_MATRIX.md)。
