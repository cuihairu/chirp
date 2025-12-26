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
       proto/common.proto proto/auth.proto proto/gateway.proto proto/chat.proto

# Generate Go Code
# We simply output to proto/go. The go_package in .proto files will handle the subdirectories usually,
# but here we force it to be relative to proto/go if needed, or just standard.
if command -v protoc-gen-go >/dev/null 2>&1; then
  protoc --proto_path=. \
         --go_out=proto/go --go_opt=paths=source_relative \
         proto/common.proto proto/auth.proto proto/gateway.proto proto/chat.proto
else
  echo "warning: protoc-gen-go not found; skipping Go code generation (install Go + protoc-gen-go to enable)"
fi

echo "Protobuf generation complete."
