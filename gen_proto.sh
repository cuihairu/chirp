#!/bin/bash

# Create output directories
mkdir -p proto/cpp
mkdir -p proto/go

# Generate C++ Code
protoc --proto_path=. \
       --cpp_out=proto/cpp \
       proto/common.proto proto/auth.proto proto/gateway.proto

# Generate Go Code
# We simply output to proto/go. The go_package in .proto files will handle the subdirectories usually,
# but here we force it to be relative to proto/go if needed, or just standard.
protoc --proto_path=. \
       --go_out=proto/go --go_opt=paths=source_relative \
       proto/common.proto proto/auth.proto proto/gateway.proto

echo "Protobuf generation complete."
