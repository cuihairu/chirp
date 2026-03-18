#!/bin/bash
# Build script for Chirp Unity Native Plugin
# Builds the native plugin for all Unity-supported platforms

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
UNITY_PLUGIN_DIR="$PROJECT_ROOT/sdks/unity"
BUILD_DIR="$PROJECT_ROOT/build/unity_plugin"
INSTALL_DIR="$UNITY_PLUGIN_DIR/Plugins"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Chirp Unity Plugin Build Script${NC}"
echo -e "${GREEN}========================================${NC}"

# Create directories
mkdir -p "$BUILD_DIR"
mkdir -p "$INSTALL_DIR"

# Common CMake args
COMMON_CMAKE_ARGS=(
    -DCHIRP_BUILD_SDK=ON
    -DCHIRP_BUILD_TESTS=OFF
    -DBUILD_SHARED_LIBS=ON
    -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/install"
)

# Function to build for desktop platform
build_desktop_plugin() {
    local platform=$1
    local generator=$2
    local arch=$3

    echo -e "${YELLOW}Building $platform plugin...${NC}"

    local build_dir="$BUILD_DIR/$platform"
    mkdir -p "$build_dir"

    cmake -G "$generator" \
        ${COMMON_CMAKE_ARGS[@]} \
        -DCMAKE_BUILD_TYPE=Release \
        -B "$build_dir" \
        -S "$PROJECT_ROOT"

    cmake --build "$build_dir" --config Release --parallel

    # Copy to Unity Plugins directory
    local plugin_dir="$INSTALL_DIR/$platform"
    mkdir -p "$plugin_dir"

    if [[ "$platform" == "windows" ]]; then
        cp "$build_dir/src/Release/chirp_unity.dll" "$plugin_dir/"
    elif [[ "$platform" == "macos" ]]; then
        # Create macOS bundle
        mkdir -p "$plugin_dir/chirp_unity.bundle/Contents/MacOS"
        cp "$build_dir/src/libchirp_unity.dylib" "$plugin_dir/chirp_unity.bundle/Contents/MacOS/"
    else
        cp "$build_dir/src/libchirp_unity.so" "$plugin_dir/"
    fi

    echo -e "${GREEN}✓ $platform plugin complete${NC}"
}

# Detect OS and build accordingly
OS="$(uname -s)"

case "$OS" in
    Linux*)
        # Build Linux plugin
        build_desktop_plugin "linux" "Unix Makefiles" "x86_64"

        # Build Android plugins (if NDK is available)
        if [ -n "$ANDROID_NDK" ] || [ -n "$ANDROID_HOME" ]; then
            echo -e "${YELLOW}Building Android plugins...${NC}"
            bash "$SCRIPT_DIR/build_android_plugin.sh"
        fi
        ;;

    Darwin*)
        # Build macOS plugin
        build_desktop_plugin "macos" "Xcode" "arm64;x86_64"

        # Build iOS plugins
        echo -e "${YELLOW}Building iOS plugin...${NC}"
        bash "$SCRIPT_DIR/build_ios_plugin.sh"
        ;;

    MINGW*|MSYS*|CYGWIN*)
        # Build Windows plugin
        build_desktop_plugin "windows" "Visual Studio 17 2022" "x64"

        # Build Android plugins (if NDK is available)
        if [ -n "$ANDROID_NDK" ] || [ -n "$ANDROID_HOME" ]; then
            echo -e "${YELLOW}Building Android plugins...${NC}"
            "$SCRIPT_DIR/build_android_plugin.bat"
        fi
        ;;

    *)
        echo "Unknown OS: $OS"
        exit 1
        ;;
esac

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Unity plugin build complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo "Plugin directory: $INSTALL_DIR"
