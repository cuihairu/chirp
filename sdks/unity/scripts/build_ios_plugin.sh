#!/bin/bash
# Build script for Chirp Unity iOS Plugin

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build/unity_plugin/ios"
INSTALL_DIR="$PROJECT_ROOT/sdks/unity/Plugins/iOS"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}Building iOS plugin for Unity...${NC}"

mkdir -p "$BUILD_DIR"
mkdir -p "$INSTALL_DIR"

# Build for iOS (arm64) and iOS Simulator (arm64 + x86_64)
PLATFORMS=("ios" "ios-simulator")

for PLATFORM in "${PLATFORMS[@]}"; do
    echo -e "${YELLOW}Building for $PLATFORM...${NC}"

    local BUILD_SUBDIR="$BUILD_DIR/$PLATFORM"
    mkdir -p "$BUILD_SUBDIR"

    local CMAKE_ARGS=(
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/ios.cmake"
        -DCHIRP_BUILD_SDK=ON
        -DCHIRP_BUILD_TESTS=OFF
        -DBUILD_SHARED_LIBS=NO
        -DCMAKE_INSTALL_PREFIX="$BUILD_SUBDIR/install"
    )

    if [[ "$PLATFORM" == "ios-simulator" ]]; then
        CMAKE_ARGS+=(-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64")
        CMAKE_ARGS+=(-DCMAKE_OSX_SYSROOT="iphonesimulator")
    else
        CMAKE_ARGS+=(-DCMAKE_OSX_ARCHITECTURES="arm64")
        CMAKE_ARGS+=(-DCMAKE_OSX_SYSROOT="iphoneos")
    fi

    cmake "${CMAKE_ARGS[@]}" \
        -B "$BUILD_SUBDIR" \
        -S "$PROJECT_ROOT"

    cmake --build "$BUILD_SUBDIR" --config Release --parallel
    cmake --install "$BUILD_SUBDIR" --config Release
done

# Create XCFramework
echo -e "${YELLOW}Creating XCFramework...${NC}"

xcodebuild -create-xcframework \
    -library "$BUILD_DIR/ios/install/lib/libchirp_unity.a" \
    -library "$BUILD_DIR/ios-simulator/install/lib/libchirp_unity.a" \
    -output "$INSTALL_DIR/libchirp_unity.xcframework"

# Copy headers
mkdir -p "$INSTALL_DIR/Headers"
cp -r "$PROJECT_ROOT/sdks/unity/Native/include/"*.h "$INSTALL_DIR/Headers/" 2>/dev/null || true

echo -e "${GREEN}✓ iOS plugin complete${NC}"
