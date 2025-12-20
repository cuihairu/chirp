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

## Docs

See `docs/README.md`.
