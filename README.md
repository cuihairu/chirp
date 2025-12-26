# chirp

C++ monorepo scaffold for a game backend (gateway/auth/chat/voice). Current focus: a TCP gateway service plus shared `common`/`network` libraries and Protobuf schemas.

## Layout

- `libs/common`: logger + config loader.
- `libs/network`: ASIO-based TCP server/session + length-prefixed Protobuf framing.
- `services/gateway`: TCP gateway service (login + heartbeat scaffolding).
- `proto`: Protobuf definitions; generated code in `proto/cpp` and `proto/go`.
- `tools/benchmark`: local benchmark utilities.

## Build

Prereqs: CMake >= 3.15, Protobuf (`protoc` + C++ libs), and a C++17 compiler. (Go is optional for generating `proto/go`.)

```bash
./gen_proto.sh
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/services/gateway/chirp_gateway --port 5000 --ws_port 5001
```

Quick smoke test (requires a successful build):

```bash
./test_services.sh --smoke
```

Chat service smoke test:

```bash
./test_services.sh --smoke-chat
```

Chat clients (TCP):

```bash
./build/services/chat/chirp_chat --port 7000
./build/tools/benchmark/chirp_chat_listen_client --port 7000 --user user_2 --max 1
./build/tools/benchmark/chirp_chat_send_client --port 7000 --sender user_1 --receiver user_2 --text hello
./build/tools/benchmark/chirp_chat_history_client --port 7000 --user user_1 --channel_type 0 --channel_id user_1\\|user_2 --limit 10
```

Optional (Auth RPC):

```bash
./build/services/auth/chirp_auth --port 6000
./build/services/gateway/chirp_gateway --port 5000 --ws_port 5001 --auth_host 127.0.0.1 --auth_port 6000
```

Optional (Distributed session via Redis):

```bash
./build/services/gateway/chirp_gateway --port 5000 --ws_port 5001 --redis_host 127.0.0.1 --redis_port 6379
```

## Docs

See `docs/README.md`.
