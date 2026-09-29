#!/bin/bash

# Create output directories
mkdir -p proto/cpp
mkdir -p proto/go

# Pick the protoc binary. The committed proto/cpp gencode must stay compatible
# with the protobuf runtime the C++ build actually links (vcpkg installs its
# own); a PATH protoc of a different generation produces gencode the compiler
# rejects ("built with an incompatible version"). Resolution order:
#   1. $CHIRP_PROTOC, when set
#   2. the protoc recorded in build/CMakeCache.txt (what the build uses)
#   3. whatever `protoc` is on PATH (CI installs it before configuring cmake)
PROTOC_BIN="${CHIRP_PROTOC:-}"
if [ -z "${PROTOC_BIN}" ] && [ -f build/CMakeCache.txt ]; then
  CACHED="$(sed -n 's/^Protobuf_PROTOC_EXECUTABLE:FILEPATH=//p' build/CMakeCache.txt | head -1)"
  if [ -n "${CACHED}" ] && [ -x "${CACHED}" ]; then
    PROTOC_BIN="${CACHED}"
  fi
fi
if [ -z "${PROTOC_BIN}" ]; then
  PROTOC_BIN="$(command -v protoc || true)"
fi
if [ -z "${PROTOC_BIN}" ]; then
  echo "error: protoc not found (install protobuf compiler first)" >&2
  exit 1
fi

# Generate C++ Code
"${PROTOC_BIN}" --proto_path=. \
       --cpp_out=proto/cpp \
       proto/common.proto \
       proto/auth.proto \
       proto/gateway.proto \
       proto/chat.proto \
       proto/social.proto \
       proto/voice.proto \
       proto/party.proto \
       proto/app_notification.proto \
       proto/game_server_gateway.proto

# Generate Go code (protoc-gen-go), part of the repo-root Go module
# (github.com/cui/chirp). go_package = github.com/cui/chirp/proto/go/<name>;
# the module= strip places one package per directory under proto/go/. Same
# committed-gencode convention as ts/csharp: CI regenerates and drift-
# checks it with the pinned plugin (see go-sdk.yml).
if command -v protoc-gen-go >/dev/null 2>&1; then
  "${PROTOC_BIN}" --proto_path=. \
         --go_out=proto/go --go_opt=paths=import \
         --go_opt=module=github.com/cui/chirp/proto/go \
         proto/common.proto \
         proto/auth.proto \
         proto/gateway.proto \
         proto/chat.proto \
         proto/social.proto \
         proto/voice.proto \
         proto/party.proto \
         proto/app_notification.proto \
         proto/game_server_gateway.proto
else
  echo "warning: protoc-gen-go not found; skipping Go code generation (install Go + protoc-gen-go to enable)"
fi

# Generate TypeScript code (ts-proto), used by apps/web_companion. The plugin
# ships with the app's devDependencies; the generated files are committed so
# that neither CI nor other consumers need the protobuf toolchain. int64 fields
# become number (sequence counters and millisecond timestamps fit in 2^53).
# Under npm workspaces the plugin may be hoisted to the repo-root node_modules.
TS_PROTO_PLUGIN="apps/web_companion/node_modules/.bin/protoc-gen-ts_proto"
if [ ! -x "$TS_PROTO_PLUGIN" ] && [ -x "node_modules/.bin/protoc-gen-ts_proto" ]; then
  TS_PROTO_PLUGIN="node_modules/.bin/protoc-gen-ts_proto"
fi
if [ -x "${PROTOC_BIN}" ] && [ -x "$TS_PROTO_PLUGIN" ]; then
  mkdir -p proto/ts
  "${PROTOC_BIN}" --proto_path=. \
         --plugin=protoc-gen-ts_proto="$TS_PROTO_PLUGIN" \
         --ts_proto_out=proto/ts \
         --ts_proto_opt=forceLong=number,esModuleInterop=true \
         proto/common.proto \
         proto/auth.proto \
         proto/gateway.proto \
         proto/chat.proto \
         proto/social.proto \
         proto/voice.proto \
         proto/party.proto \
         proto/app_notification.proto \
         proto/game_server_gateway.proto
else
  echo "warning: protoc or ts-proto plugin not found; skipping TS code generation (npm install in apps/web_companion to enable)"
fi

# Generate C# code (protoc built-in csharp_out), used by sdks/unity: the
# dotnet test project compiles it in CI, Unity projects import it together
# with the Google.Protobuf runtime. Same committed-gencode convention as
# proto/ts — consumers never need the toolchain.
mkdir -p proto/csharp
"${PROTOC_BIN}" --proto_path=. \
       --csharp_out=proto/csharp \
       proto/common.proto \
       proto/auth.proto \
       proto/gateway.proto \
       proto/chat.proto \
       proto/social.proto \
       proto/voice.proto \
       proto/party.proto \
       proto/app_notification.proto \
       proto/game_server_gateway.proto

# Generate Java code (protoc built-in java_out), used by apps/android (native
# Kotlin protocol core). Same committed-gencode convention as proto/ts
# and csharp — consumers never need the toolchain. No java_package options in
# the .proto sources, so classes land in the default proto packages
# (chirp.auth, chirp.gateway, ...) as nested classes of the outer files. The
# protobuf-java runtime is NOT vendored: apps/android/Makefile fetches the
# pinned jar (4.33.4, must match this protoc's generation) with a checksum.
mkdir -p proto/java
"${PROTOC_BIN}" --proto_path=. \
       --java_out=proto/java \
       proto/common.proto \
       proto/auth.proto \
       proto/gateway.proto \
       proto/chat.proto \
       proto/social.proto \
       proto/voice.proto \
       proto/party.proto \
       proto/app_notification.proto \
       proto/game_server_gateway.proto

echo "Protobuf generation complete."
