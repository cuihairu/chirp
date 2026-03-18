#!/bin/bash
# Cross-platform build script for Chirp Core SDK
# Builds the SDK for Windows, macOS, Linux, iOS, and Android

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
INSTALL_DIR="$PROJECT_ROOT/install"

# Parse arguments
BUILD_TYPE=${BUILD_TYPE:-Release}
BUILD_ANDROID=${BUILD_ANDROID:-true}
BUILD_IOS=${BUILD_IOS:-false}
BUILD_DESKTOP=${BUILD_DESKTOP:-true}

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Chirp Core SDK Build Script${NC}"
echo -e "${GREEN}========================================${NC}"
echo "Build Type: $BUILD_TYPE"
echo "Build Android: $BUILD_ANDROID"
echo "Build iOS: $BUILD_IOS"
echo "Build Desktop: $BUILD_DESKTOP"
echo ""

# Function to build for desktop platforms
build_desktop() {
    local platform=$1
    echo -e "${YELLOW}Building for $platform...${NC}"

    local build_subdir="$BUILD_DIR/$platform"
    local install_subdir="$INSTALL_DIR/$platform"

    mkdir -p "$build_subdir"
    mkdir -p "$install_subdir"

    local cmake_args=(
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE
        -DCMAKE_INSTALL_PREFIX="$install_subdir"
        -DBUILD_SHARED_LIBS=ON
        -DCHIRP_BUILD_SDK=ON
        -DCHIRP_BUILD_TESTS=OFF
    )

    case $platform in
        linux)
            cmake_args+=(-DCMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/linux-gcc.cmake")
            ;;
        macos)
            cmake_args+=(-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15)
            ;;
        windows)
            cmake_args+=(-G "Visual Studio 17 2022" -A x64)
            ;;
    esac

    cmake "${cmake_args[@]}" -B "$build_subdir" -S "$PROJECT_ROOT"
    cmake --build "$build_subdir" --config $BUILD_TYPE --parallel
    cmake --install "$build_subdir" --config $BUILD_TYPE

    echo -e "${GREEN}✓ $platform build complete${NC}"
}

# Function to build for Android
build_android() {
    echo -e "${YELLOW}Building for Android...${NC}"

    local build_subdir="$BUILD_DIR/android"
    local install_subdir="$INSTALL_DIR/android"

    mkdir -p "$build_subdir"
    mkdir -p "$install_subdir"

    # Android NDK path (can be overridden via environment)
    local ANDROID_NDK=${ANDROID_NDK:-$ANDROID_HOME/ndk/25.2.9519653}

    if [ ! -d "$ANDROID_NDK" ]; then
        echo -e "${RED}Android NDK not found at $ANDROID_NDK${NC}"
        echo "Set ANDROID_NDK environment variable to point to your NDK"
        return 1
    fi

    local architectures=(armeabi-v7a arm64-v8a x86_64)

    for arch in "${architectures[@]}"; do
        echo -e "${YELLOW}Building Android $arch...${NC}"

        local abi
        case $arch in
            armeabi-v7a)
                abi=armeabi-v7a
                toolchain=arm-linux-androideabi
                ;;
            arm64-v8a)
                abi=arm64-v8a
                toolchain=aarch64-linux-android
                ;;
            x86_64)
                abi=x86_64
                toolchain=x86_64-linux-android
                ;;
        esac

        local arch_build_dir="$build_subdir/$arch"
        mkdir -p "$arch_build_dir"

        cmake \
            -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
            -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI=$abi \
            -DANDROID_PLATFORM=android-24 \
            -DCMAKE_INSTALL_PREFIX="$install_subdir/$arch" \
            -DBUILD_SHARED_LIBS=ON \
            -DCHIRP_BUILD_SDK=ON \
            -DCHIRP_BUILD_TESTS=OFF \
            -B "$arch_build_dir" \
            -S "$PROJECT_ROOT"

        cmake --build "$arch_build_dir" --config $BUILD_TYPE --parallel
        cmake --install "$arch_build_dir" --config $BUILD_TYPE
    done

    echo -e "${GREEN}✓ Android build complete${NC}"
}

# Function to build for iOS
build_ios() {
    echo -e "${YELLOW}Building for iOS...${NC}"

    if [[ "$OSTYPE" != "darwin"* ]]; then
        echo -e "${RED}iOS builds require macOS${NC}"
        return 1
    fi

    local build_subdir="$BUILD_DIR/ios"
    local install_subdir="$INSTALL_DIR/ios"

    mkdir -p "$build_subdir"
    mkdir -p "$install_subdir"

    # Build for both simulator and device
    local platforms=("iOS"("ios64"))

    for platform in "${platforms[@]}"; do
        cmake \
            -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
            -DCMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/ios.cmake" \
            -DCMAKE_INSTALL_PREFIX="$install_subdir" \
            -DCMAKE_OSX_ARCHITECTURES=arm64 \
            -DBUILD_SHARED_LIBS=NO \
            -DCHIRP_BUILD_SDK=ON \
            -DCHIRP_BUILD_TESTS=OFF \
            -B "$build_subdir" \
            -S "$PROJECT_ROOT"

        cmake --build "$build_subdir" --config $BUILD_TYPE --parallel
        cmake --install "$build_subdir" --config $BUILD_TYPE

        # Create XCFramework
        xcodebuild -create-xcframework \
            -library "$install_subdir/lib/libchirp_sdk.a" \
            -output "$install_subdir/libchirp_sdk.xcframework"
    done

    echo -e "${GREEN}✓ iOS build complete${NC}"
}

# Main build logic
main() {
    # Detect OS
    OS="$(uname -s)"

    if [ "$BUILD_DESKTOP" = true ]; then
        case "$OS" in
            Linux*)
                build_desktop linux
                ;;
            Darwin*)
                build_desktop macos
                ;;
            MINGW*|MSYS*|CYGWIN*)
                build_desktop windows
                ;;
            *)
                echo -e "${RED}Unknown OS: $OS${NC}"
                exit 1
                ;;
        esac
    fi

    if [ "$BUILD_ANDROID" = true ]; then
        build_android
    fi

    if [ "$BUILD_IOS" = true ]; then
        build_ios
    fi

    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}Build complete!${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo "Install directory: $INSTALL_DIR"
}

main "$@"
