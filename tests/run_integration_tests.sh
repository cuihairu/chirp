#!/bin/bash

# Chirp Integration Test Runner
# This script builds and runs integration tests using vcpkg for dependencies

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default values
USE_DOCKER=false
GATEWAY_HOST="localhost"
GATEWAY_PORT=5000
USE_VCPKG_TOOLCHAIN="false"

# Parse arguments first so --help does not trigger dependency setup.
while [[ $# -gt 0 ]]; do
  case $1 in
    --docker)
      USE_DOCKER=true
      shift
      ;;
    --gateway-host)
      GATEWAY_HOST="$2"
      shift 2
      ;;
    --gateway-port)
      GATEWAY_PORT="$2"
      shift 2
      ;;
    --help|-h)
      echo "Usage: $0 [options]"
      echo "Options:"
      echo "  --docker             Start Docker services before testing"
      echo "  --gateway-host HOST  Gateway host (default: localhost)"
      echo "  --gateway-port PORT  Gateway port (default: 5000)"
      echo "  --help, -h           Show this help"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
      ;;
  esac
done

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
    echo -e "${RED}Error: Could not find vcpkg installation${NC}"
    echo "Please set VCPKG_ROOT environment variable or install vcpkg"
    exit 1
fi

VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
VCPKG_BIN="$VCPKG_ROOT/vcpkg"
if [ -f "$VCPKG_ROOT/vcpkg.exe" ]; then
  VCPKG_BIN="$VCPKG_ROOT/vcpkg.exe"
fi
if [ -f "$VCPKG_TOOLCHAIN" ]; then
  USE_VCPKG_TOOLCHAIN="true"
fi

# Get the chirp root directory
CHIRP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" || "$OSTYPE" == "cygwin" ]]; then
  CMAKE_GENERATOR="Visual Studio 17 2022"
  CMAKE_PLATFORM=(-A x64)
  MAIN_LIB_NETWORK="$CHIRP_ROOT/build/libs/network/Debug/chirp_network.lib"
  TEST_BIN="./Debug/chirp_integration_test.exe"
else
  CMAKE_GENERATOR="Unix Makefiles"
  CMAKE_PLATFORM=()
  MAIN_LIB_NETWORK="$CHIRP_ROOT/build/libs/network/libchirp_network.a"
  TEST_BIN="./chirp_integration_test"
fi

echo -e "${YELLOW}=== Chirp Integration Test Runner ===${NC}"
echo ""
echo "Using vcpkg at: $VCPKG_ROOT"
echo ""

if [ "$USE_VCPKG_TOOLCHAIN" = "true" ]; then
  if ! "$VCPKG_BIN" install; then
    echo -e "${YELLOW}Warning: vcpkg install failed, falling back to system dependencies${NC}"
    USE_VCPKG_TOOLCHAIN="false"
  fi
else
  echo -e "${YELLOW}Skipping vcpkg install; using system dependencies${NC}"
fi

# Function to cleanup Docker containers
cleanup_docker() {
  if [ "$USE_DOCKER" = true ]; then
    echo -e "${YELLOW}Stopping Docker containers...${NC}"
    docker compose -f docker-compose.yml down 2>/dev/null || true
  fi
}

# Trap to ensure cleanup on exit
trap cleanup_docker EXIT INT TERM

# Start Docker services if requested
if [ "$USE_DOCKER" = true ]; then
  echo -e "${YELLOW}Starting Docker services...${NC}"
  docker compose -f docker-compose.yml up -d redis auth gateway chat social 2>/dev/null || {
    echo -e "${RED}Failed to start Docker services${NC}"
    echo "Please make sure docker compose is available"
    exit 1
  }

  echo -e "${YELLOW}Waiting for services to be ready...${NC}"
  sleep 5
fi

# Build main libraries first if needed
if [ ! -f "$MAIN_LIB_NETWORK" ]; then
    echo -e "${YELLOW}Building main libraries...${NC}"
    mkdir -p "$CHIRP_ROOT/build"
    cd "$CHIRP_ROOT/build"
    if [ "$USE_VCPKG_TOOLCHAIN" = "true" ]; then
      cmake .. -G "$CMAKE_GENERATOR" "${CMAKE_PLATFORM[@]}" \
          -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN" \
          -DENABLE_TESTS=ON
    else
      cmake .. -G "$CMAKE_GENERATOR" "${CMAKE_PLATFORM[@]}" \
          -DENABLE_TESTS=ON
    fi
    cmake --build . --config Debug --target chirp_common chirp_network
fi

# Build integration test
echo -e "${YELLOW}Building integration test...${NC}"
cd "$CHIRP_ROOT/tests/integration"
mkdir -p build
cd build

if [ "$USE_VCPKG_TOOLCHAIN" = "true" ]; then
  cmake .. -G "$CMAKE_GENERATOR" "${CMAKE_PLATFORM[@]}" \
      -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN"
else
  cmake .. -G "$CMAKE_GENERATOR" "${CMAKE_PLATFORM[@]}"
fi

if [ $? -ne 0 ]; then
  echo -e "${RED}CMake configuration failed${NC}"
  exit 1
fi

cmake --build . --config Debug

if [ $? -ne 0 ]; then
  echo -e "${RED}Build failed${NC}"
  exit 1
fi

echo ""
echo -e "${YELLOW}Running integration tests...${NC}"
echo ""

# Run the test
if [ "$USE_DOCKER" = true ]; then
    "$TEST_BIN" --connect --gateway "$GATEWAY_HOST" --gateway-port "$GATEWAY_PORT"
else
    "$TEST_BIN"
fi

TEST_RESULT=$?

echo ""
if [ $TEST_RESULT -eq 0 ]; then
    echo -e "${GREEN}=== Tests PASSED ===${NC}"
else
    echo -e "${RED}=== Tests FAILED ===${NC}"
fi

exit $TEST_RESULT
