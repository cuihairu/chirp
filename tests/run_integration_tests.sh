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
USE_LOCAL_SERVICES=false
RUN_CONNECTION_TESTS=false
GATEWAY_HOST="localhost"
GATEWAY_PORT=5000
AUTH_PORT=6000
GATEWAY_WS_PORT=5001
USE_VCPKG_TOOLCHAIN="false"
FORCE_VCPKG_INSTALL=false
AUTH_PID=""
GATEWAY_PID=""
LOG_DIR=""

# Parse arguments first so --help does not trigger dependency setup.
while [[ $# -gt 0 ]]; do
  case $1 in
    --docker)
      USE_DOCKER=true
      shift
      ;;
    --local-services)
      USE_LOCAL_SERVICES=true
      RUN_CONNECTION_TESTS=true
      shift
      ;;
    --connect|-c)
      RUN_CONNECTION_TESTS=true
      shift
      ;;
    --gateway-host)
      GATEWAY_HOST="$2"
      shift 2
      ;;
    --gateway-port)
      GATEWAY_PORT="$2"
      GATEWAY_WS_PORT="$((GATEWAY_PORT + 1))"
      shift 2
      ;;
    --auth-port)
      AUTH_PORT="$2"
      shift 2
      ;;
    --use-vcpkg)
      FORCE_VCPKG_INSTALL=true
      shift
      ;;
    --help|-h)
      echo "Usage: $0 [options]"
      echo "Options:"
      echo "  --docker             Start Docker services before testing"
      echo "  --local-services     Start local auth/gateway binaries and run connection smoke"
      echo "  --connect, -c        Run connection smoke tests against a live gateway"
      echo "  --gateway-host HOST  Gateway host (default: localhost)"
      echo "  --gateway-port PORT  Gateway port (default: 5000)"
      echo "  --auth-port PORT     Auth port for --local-services (default: 6000)"
      echo "  --use-vcpkg          Run 'vcpkg install' before configuring tests"
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
    VCPKG_ROOT=""
fi

VCPKG_TOOLCHAIN=""
VCPKG_BIN=""
if [ -n "$VCPKG_ROOT" ]; then
  VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
  VCPKG_BIN="$VCPKG_ROOT/vcpkg"
  if [ -f "$VCPKG_ROOT/vcpkg.exe" ]; then
    VCPKG_BIN="$VCPKG_ROOT/vcpkg.exe"
  fi
fi
if [ -n "$VCPKG_TOOLCHAIN" ] && [ -f "$VCPKG_TOOLCHAIN" ]; then
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

AUTH_BIN="$CHIRP_ROOT/build/services/auth/chirp_auth"
GATEWAY_BIN="$CHIRP_ROOT/build/services/gateway/chirp_gateway"

is_port_listening() {
  local port="$1"
  lsof -nP -iTCP:"$port" -sTCP:LISTEN >/dev/null 2>&1
}

wait_for_port() {
  local port="$1"
  local label="$2"
  local attempts=50
  local i
  for ((i=1; i<=attempts; i++)); do
    if is_port_listening "$port"; then
      return 0
    fi
    sleep 0.2
  done
  echo -e "${RED}${label} did not start on port ${port}${NC}"
  return 1
}

echo -e "${YELLOW}=== Chirp Integration Test Runner ===${NC}"
echo ""
if [ -n "$VCPKG_ROOT" ]; then
  echo "Using vcpkg at: $VCPKG_ROOT"
else
  echo "vcpkg not detected; using system dependencies"
fi
echo ""

if [ "$FORCE_VCPKG_INSTALL" = "true" ] && [ "$USE_VCPKG_TOOLCHAIN" = "true" ]; then
  if ! "$VCPKG_BIN" install; then
    echo -e "${YELLOW}Warning: vcpkg install failed, falling back to system dependencies${NC}"
    USE_VCPKG_TOOLCHAIN="false"
  fi
else
  echo -e "${YELLOW}Skipping vcpkg install; reusing available dependencies${NC}"
fi

# Function to cleanup Docker containers
cleanup_docker() {
  if [ "$USE_DOCKER" = true ]; then
    echo -e "${YELLOW}Stopping Docker containers...${NC}"
    docker compose -f docker-compose.yml down 2>/dev/null || true
  fi
}

cleanup_local_services() {
  if [ -n "$GATEWAY_PID" ]; then
    kill "$GATEWAY_PID" 2>/dev/null || true
    wait "$GATEWAY_PID" 2>/dev/null || true
  fi
  if [ -n "$AUTH_PID" ]; then
    kill "$AUTH_PID" 2>/dev/null || true
    wait "$AUTH_PID" 2>/dev/null || true
  fi
}

cleanup_all() {
  cleanup_local_services
  cleanup_docker
}

# Trap to ensure cleanup on exit
trap cleanup_all EXIT INT TERM

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

if [ "$USE_LOCAL_SERVICES" = true ]; then
  if [ "$USE_DOCKER" = true ]; then
    echo -e "${RED}Choose either --docker or --local-services, not both${NC}"
    exit 1
  fi

  if is_port_listening "$AUTH_PORT"; then
    echo -e "${RED}Auth port ${AUTH_PORT} is already in use${NC}"
    exit 1
  fi

  if is_port_listening "$GATEWAY_PORT"; then
    echo -e "${RED}Gateway port ${GATEWAY_PORT} is already in use${NC}"
    exit 1
  fi

  if is_port_listening "$GATEWAY_WS_PORT"; then
    echo -e "${RED}Gateway WebSocket port ${GATEWAY_WS_PORT} is already in use${NC}"
    exit 1
  fi
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

if [ "$USE_LOCAL_SERVICES" = true ]; then
  echo -e "${YELLOW}Starting local auth/gateway services...${NC}"
  LOG_DIR="$CHIRP_ROOT/tests/integration/logs"
  mkdir -p "$LOG_DIR"

  "$AUTH_BIN" --port "$AUTH_PORT" --jwt_secret dev_secret >"$LOG_DIR/auth.log" 2>&1 &
  AUTH_PID=$!
  wait_for_port "$AUTH_PORT" "Auth service"

  GATEWAY_HOST="127.0.0.1"
  "$GATEWAY_BIN" --port "$GATEWAY_PORT" --ws_port "$GATEWAY_WS_PORT" \
      --auth_host 127.0.0.1 --auth_port "$AUTH_PORT" >"$LOG_DIR/gateway.log" 2>&1 &
  GATEWAY_PID=$!
  wait_for_port "$GATEWAY_PORT" "Gateway service"
fi

echo ""
echo -e "${YELLOW}Running integration tests...${NC}"
echo ""

# Run the test
if [ "$RUN_CONNECTION_TESTS" = true ]; then
    "$TEST_BIN" --connect --gateway-host "$GATEWAY_HOST" --gateway-port "$GATEWAY_PORT"
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
