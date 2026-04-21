#!/bin/bash

# Chirp Build and Test Script
# This script handles the complete build and test process for the Chirp project

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
BUILD_TYPE="${BUILD_TYPE:-Debug}"
BUILD_DIR="${BUILD_DIR:-build}"
RUN_TESTS="${RUN_TESTS:-true}"
START_SERVICES="${START_SERVICES:-false}"
FORCE_VCPKG_INSTALL="${FORCE_VCPKG_INSTALL:-false}"

echo -e "${BLUE}=== Chirp Build and Test Script ===${NC}"
echo ""

USE_VCPKG_TOOLCHAIN="false"

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Detect vcpkg location
if [ -n "${VCPKG_ROOT:-}" ]; then
    VCPKG_ROOT="$VCPKG_ROOT"
elif [ -d "$HOME/vcpkg" ]; then
    VCPKG_ROOT="$HOME/vcpkg"
elif [ -d "/c/Users/$USER/vcpkg" ]; then
    VCPKG_ROOT="/c/Users/$USER/vcpkg"
elif [ -d "./vcpkg" ]; then
    VCPKG_ROOT="./vcpkg"
else
    VCPKG_ROOT=""
fi

VCPKG_TOOLCHAIN=""
if [ -n "$VCPKG_ROOT" ]; then
    VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
fi
if [ -n "$VCPKG_TOOLCHAIN" ] && [ -f "$VCPKG_TOOLCHAIN" ]; then
    USE_VCPKG_TOOLCHAIN="true"
    echo -e "${GREEN}Using vcpkg at: $VCPKG_ROOT${NC}"
else
    echo -e "${YELLOW}Using system dependencies by default${NC}"
fi
echo ""

# Check prerequisites
echo -e "${YELLOW}Checking prerequisites...${NC}"

check_command() {
    if command -v "$1" >/dev/null 2>&1; then
        echo -e "  ${GREEN}✓${NC} $1: $(command -v $1)"
        return 0
    else
        echo -e "  ${RED}✗${NC} $1: not found"
        return 1
    fi
}

MISSING=0

# Check for required tools
check_command cmake || MISSING=1
check_command git || MISSING=1

# Check for vcpkg
if [ -n "$VCPKG_ROOT" ] && { [ -f "$VCPKG_ROOT/vcpkg" ] || [ -f "$VCPKG_ROOT/vcpkg.exe" ]; }; then
    echo -e "  ${GREEN}✓${NC} vcpkg: $VCPKG_ROOT"
else
    echo -e "  ${YELLOW}!${NC} vcpkg: not found, system dependencies will be used"
fi

# Check for optional tools
echo ""
echo "Optional tools:"
check_command docker || echo "  ${YELLOW}!${NC} docker: containerized tests will be skipped"

if [ $MISSING -eq 1 ]; then
    echo ""
    echo -e "${RED}Error: Missing required tools.${NC}"
    exit 1
fi

echo ""

# Detect OS and set generator
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" || "$OSTYPE" == "cygwin" ]]; then
    CMAKE_GENERATOR="Visual Studio 17 2022"
    CMAKE_PLATFORM="-A x64"
    CONFIG_CMD="cmake -S \"$PROJECT_ROOT\" -B \"$PROJECT_ROOT/$BUILD_DIR\" -G \"$CMAKE_GENERATOR\" $CMAKE_PLATFORM -DENABLE_TESTS=ON"
    BUILD_CMD="cmake --build \"$PROJECT_ROOT/$BUILD_DIR\" --config $BUILD_TYPE"
    INTEGRATION_TEST_BIN="$PROJECT_ROOT/tests/integration/build/Debug/chirp_integration_test.exe"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    CONFIG_CMD="cmake -S \"$PROJECT_ROOT\" -B \"$PROJECT_ROOT/$BUILD_DIR\" -DENABLE_TESTS=ON"
    BUILD_CMD="cmake --build \"$PROJECT_ROOT/$BUILD_DIR\" --config $BUILD_TYPE"
    INTEGRATION_TEST_BIN="$PROJECT_ROOT/tests/integration/build/chirp_integration_test"
else
    CONFIG_CMD="cmake -S \"$PROJECT_ROOT\" -B \"$PROJECT_ROOT/$BUILD_DIR\" -DENABLE_TESTS=ON"
    BUILD_CMD="cmake --build \"$PROJECT_ROOT/$BUILD_DIR\" --config $BUILD_TYPE"
    INTEGRATION_TEST_BIN="$PROJECT_ROOT/tests/integration/build/chirp_integration_test"
fi

if [ "$USE_VCPKG_TOOLCHAIN" = "true" ]; then
    if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" || "$OSTYPE" == "cygwin" ]]; then
        CONFIG_CMD="$CONFIG_CMD -DCMAKE_TOOLCHAIN_FILE=\"$VCPKG_TOOLCHAIN\""
    else
        CONFIG_CMD="$CONFIG_CMD -DCMAKE_TOOLCHAIN_FILE=\"$VCPKG_TOOLCHAIN\""
    fi
fi

# Step 1: Check/install vcpkg packages
echo -e "${BLUE}[1/6] Checking dependency setup...${NC}"

VCPKG_BIN=""
if [ -n "$VCPKG_ROOT" ]; then
    VCPKG_BIN="$VCPKG_ROOT/vcpkg"
    if [ -f "$VCPKG_ROOT/vcpkg.exe" ]; then
        VCPKG_BIN="$VCPKG_ROOT/vcpkg.exe"
    fi
fi

if [ "$FORCE_VCPKG_INSTALL" = "true" ] && [ "$USE_VCPKG_TOOLCHAIN" = "true" ]; then
    if ! "$VCPKG_BIN" list protobuf 2>/dev/null | grep -q "protobuf"; then
        echo "Installing vcpkg manifest dependencies..."
        if ! "$VCPKG_BIN" install; then
            echo -e "${YELLOW}Warning: vcpkg install failed, continuing with system dependencies${NC}"
            USE_VCPKG_TOOLCHAIN="false"
            CONFIG_CMD="${CONFIG_CMD/ -DCMAKE_TOOLCHAIN_FILE=\"$VCPKG_TOOLCHAIN\"/}"
        fi
    else
        echo -e "${GREEN}vcpkg protobuf dependency already installed${NC}"
    fi
else
    echo -e "${YELLOW}Skipping vcpkg install; reusing available dependencies${NC}"
fi

# Step 2: Generate protobuf files
echo -e "${BLUE}[2/6] Generating protobuf files...${NC}"

VCPKG_PROTOC=""
if [ -n "$VCPKG_ROOT" ]; then
    VCPKG_PROTOC="$VCPKG_ROOT/installed/x64-windows/tools/protobuf/protoc"
fi
if [ -f "$VCPKG_PROTOC" ] || [ -f "$VCPKG_PROTOC.exe" ]; then
    PROTOC="$VCPKG_PROTOC"
elif command -v protoc >/dev/null 2>&1; then
    PROTOC="protoc"
else
    echo -e "${YELLOW}Warning: protoc not found, skipping protobuf generation${NC}"
    echo "Pre-generated files will be used if available"
fi

if [ -n "$PROTOC" ]; then
    cd "$PROJECT_ROOT"
    rm -f proto/cpp/proto/*.pb.cc proto/cpp/proto/*.pb.h 2>/dev/null || true
    "$PROTOC" --proto_path=. --cpp_out=proto/cpp \
        proto/common.proto proto/gateway.proto proto/auth.proto \
        proto/chat.proto proto/social.proto proto/voice.proto proto/notification.proto
    echo -e "${GREEN}✓ Protobuf files generated${NC}"
fi

# Step 3: Configure build
echo -e "${BLUE}[3/6] Configuring build...${NC}"

cd "$PROJECT_ROOT"
mkdir -p "$BUILD_DIR"

eval $CONFIG_CMD

if [ $? -ne 0 ]; then
    echo -e "${RED}CMake configuration failed${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Build configured${NC}"

# Step 4: Build project
echo -e "${BLUE}[4/6] Building project...${NC}"

eval $BUILD_CMD

if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Build completed${NC}"

# Step 5: Start services (optional)
echo -e "${BLUE}[5/6] Starting services...${NC}"

if [ "$START_SERVICES" = "true" ] && command -v docker >/dev/null 2>&1; then
    docker compose up -d redis auth gateway chat social
    echo "Waiting for services to be ready..."
    sleep 5
    echo -e "${GREEN}✓ Services started${NC}"
else
    echo -e "${YELLOW}Skipping service startup (use START_SERVICES=true to enable)${NC}"
fi

# Step 6: Run tests (optional)
echo -e "${BLUE}[6/6] Running tests...${NC}"

if [ "$RUN_TESTS" = "true" ]; then
    echo "Running unit tests with CTest..."
    ctest --test-dir "$PROJECT_ROOT/$BUILD_DIR" --output-on-failure

    if [ "$START_SERVICES" = "true" ] && command -v docker >/dev/null 2>&1; then
        echo "Running integration tests with Docker-backed connection smoke..."
        bash "$PROJECT_ROOT/tests/run_integration_tests.sh" --docker --connect
    else
        echo "Running integration smoke tests..."
        bash "$PROJECT_ROOT/tests/run_integration_tests.sh"
    fi
    TEST_RESULT=$?
    if [ $TEST_RESULT -eq 0 ]; then
        echo -e "${GREEN}✓ Integration tests passed${NC}"
    else
        echo -e "${RED}✗ Integration tests failed${NC}"
    fi
else
    echo -e "${YELLOW}Skipping tests (use RUN_TESTS=false to skip)${NC}"
fi

echo ""
echo -e "${GREEN}=== Build Complete ===${NC}"
echo ""
echo "Built artifacts:"
echo "  Libraries: $BUILD_DIR/libs/"
echo "  Services: $BUILD_DIR/services/"
echo "  SDK: $BUILD_DIR/sdks/"
echo ""
echo "To start all services:"
echo "  docker compose up -d"
echo ""
echo "To run integration tests:"
echo "  bash tests/run_integration_tests.sh"
echo "  bash tests/run_integration_tests.sh --docker --connect"
echo "  bash tests/run_integration_tests.sh --local-services --gateway-port 5500 --auth-port 6500"
