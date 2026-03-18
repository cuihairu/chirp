#!/bin/bash
# Build script for Chirp Unity Android Plugin

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build/unity_plugin/android"
INSTALL_DIR="$PROJECT_ROOT/sdks/unity/Plugins/Android"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# Android NDK detection
ANDROID_NDK=${ANDROID_NDK:-$ANDROID_HOME/ndk/25.2.9519653}

if [ ! -d "$ANDROID_NDK" ]; then
    echo -e "${RED}Android NDK not found at $ANDROID_NDK${NC}"
    echo "Set ANDROID_NDK environment variable to point to your NDK"
    exit 1
fi

echo -e "${YELLOW}Building Android plugin for Unity...${NC}"
echo "Using NDK: $ANDROID_NDK"

mkdir -p "$BUILD_DIR"
mkdir -p "$INSTALL_DIR/libs"

# Architectures to build
ARCHITECTURES=("armeabi-v7a" "arm64-v8a" "x86_64")

for ARCH in "${ARCHITECTURES[@]}"; do
    echo -e "${YELLOW}Building for $ARCH...${NC}"

    local BUILD_SUBDIR="$BUILD_DIR/$ARCH"
    mkdir -p "$BUILD_SUBDIR"

    # Set ABI-specific variables
    case $ARCH in
        armeabi-v7a)
            ABI=armeabi-v7a
            ;;
        arm64-v8a)
            ABI=arm64-v8a
            ;;
        x86_64)
            ABI=x86_64
            ;;
    esac

    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=$ABI \
        -DANDROID_PLATFORM=android-24 \
        -DCHIRP_BUILD_SDK=ON \
        -DCHIRP_BUILD_TESTS=OFF \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_INSTALL_PREFIX="$BUILD_SUBDIR/install" \
        -B "$BUILD_SUBDIR" \
        -S "$PROJECT_ROOT"

    cmake --build "$BUILD_SUBDIR" --config Release --parallel
    cmake --install "$BUILD_SUBDIR" --config Release

    # Copy to Unity Android plugin directory
    cp "$BUILD_SUBDIR/install/lib/libchirp_unity.so" "$INSTALL_DIR/libs/$ARCH/"
done

echo -e "${GREEN}✓ Android plugin complete${NC}"
