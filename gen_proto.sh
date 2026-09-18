#!/bin/bash

# Create output directories
mkdir -p proto/cpp
mkdir -p proto/go

# Ensure protoc exists.
if ! command -v protoc >/dev/null 2>&1; then
  echo "error: protoc not found (install protobuf compiler first)" >&2
  exit 1
fi

# Generate C++ Code
protoc --proto_path=. \
       --cpp_out=proto/cpp \
       proto/common.proto \
       proto/auth.proto \
       proto/gateway.proto \
       proto/chat.proto \
       proto/social.proto \
       proto/voice.proto \
       proto/notification.proto \
       proto/server_gateway.proto

# Generate Go Code
# We simply output to proto/go. The go_package in .proto files will handle the subdirectories usually,
# but here we force it to be relative to proto/go if needed, or just standard.
if command -v protoc-gen-go >/dev/null 2>&1; then
  protoc --proto_path=. \
         --go_out=proto/go --go_opt=paths=source_relative \
         proto/common.proto \
         proto/auth.proto \
         proto/gateway.proto \
         proto/chat.proto \
         proto/social.proto \
         proto/voice.proto \
         proto/notification.proto \
         proto/server_gateway.proto
else
  echo "warning: protoc-gen-go not found; skipping Go code generation (install Go + protoc-gen-go to enable)"
fi

# Generate TypeScript code (ts-proto), used by apps/web_companion. The plugin
# ships with the app's devDependencies; the generated files are committed so
# that neither CI nor other consumers need the protobuf toolchain. int64 fields
# become number (sequence counters and millisecond timestamps fit in 2^53).
TS_PROTO_PLUGIN="apps/web_companion/node_modules/.bin/protoc-gen-ts_proto"
if command -v protoc >/dev/null 2>&1 && [ -x "$TS_PROTO_PLUGIN" ]; then
  mkdir -p proto/ts
  protoc --proto_path=. \
         --plugin=protoc-gen-ts_proto="$TS_PROTO_PLUGIN" \
         --ts_proto_out=proto/ts \
         --ts_proto_opt=forceLong=number,esModuleInterop=true \
         proto/common.proto \
         proto/auth.proto \
         proto/gateway.proto \
         proto/chat.proto \
         proto/social.proto \
         proto/voice.proto \
         proto/notification.proto \
         proto/server_gateway.proto
else
  echo "warning: protoc or ts-proto plugin not found; skipping TS code generation (npm install in apps/web_companion to enable)"
fi

echo "Protobuf generation complete."
