#!/bin/bash

# Chirp Project Validation Script
# Validates that all components are properly configured

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=== Chirp Project Validation ===${NC}"
echo ""

VALIDATION_PASSED=0
VALIDATION_FAILED=0

check_item() {
    local description="$1"
    local command="$2"
    local required="${3:-true}"

    echo -n "  Checking $description... "

    if eval "$command" >/dev/null 2>&1; then
        echo -e "${GREEN}✓${NC}"
        ((VALIDATION_PASSED++))
        return 0
    else
        if [ "$required" = "true" ]; then
            echo -e "${RED}✗${NC} (REQUIRED)"
            ((VALIDATION_FAILED++))
            return 1
        else
            echo -e "${YELLOW}!${NC} (optional)"
            return 0
        fi
    fi
}

echo -e "${BLUE}[1/7] Project Structure${NC}"

# Check core directories
check_item "proto directory" "[ -d proto ]"
check_item "services directory" "[ -d services ]"
check_item "libs directory" "[ -d libs ]"
check_item "sdks directory" "[ -d sdks ]"
check_item "tests directory" "[ -d tests ]"
check_item "apps directory" "[ -d apps ]"
check_item "scripts directory" "[ -d scripts ]"
check_item "docs directory" "[ -d docs ]"

echo ""
echo -e "${BLUE}[2/7] Protocol Definitions${NC}"

check_item "common.proto" "[ -f proto/common.proto ]"
check_item "gateway.proto" "[ -f proto/gateway.proto ]"
check_item "auth.proto" "[ -f proto/auth.proto ]"
check_item "chat.proto" "[ -f proto/chat.proto ]"
check_item "social.proto" "[ -f proto/social.proto ]"
check_item "voice.proto" "[ -f proto/voice.proto ]"

echo ""
echo -e "${BLUE}[3/7] Service Implementations${NC}"

check_item "Gateway service" "[ -f services/gateway/src/main.cc ]"
check_item "Auth service" "[ -f services/auth/src/main.cc ]"
check_item "Chat service" "[ -f services/chat/src/main.cc ]"
check_item "Social service" "[ -f services/social/src/main.cc ]"
check_item "Voice service" "[ -f services/voice/src/main.cc ]"

# Check service CMakeLists
check_item "Gateway CMakeLists" "[ -f services/gateway/CMakeLists.txt ]"
check_item "Auth CMakeLists" "[ -f services/auth/CMakeLists.txt ]"
check_item "Chat CMakeLists" "[ -f services/chat/CMakeLists.txt ]"
check_item "Social CMakeLists" "[ -f services/social/CMakeLists.txt ]"
check_item "Voice CMakeLists" "[ -f services/voice/CMakeLists.txt ]"

echo ""
echo -e "${BLUE}[4/7] Libraries${NC}"

check_item "Common library" "[ -f libs/common/CMakeLists.txt ]"
check_item "Network library" "[ -f libs/network/CMakeLists.txt ]"

# Check key library files
check_item "Logger" "[ -f libs/common/logger.cc ]"
check_item "JWT" "[ -f libs/common/jwt.cc ]"
check_item "TCP Server" "[ -f libs/network/tcp_server.cc ]"
check_item "WebSocket Server" "[ -f libs/network/websocket_server.cc ]"
check_item "TCP Client" "[ -f libs/network/tcp_client.cc ]"
check_item "WebSocket Client" "[ -f libs/network/websocket_client.cc ]"

echo ""
echo -e "${BLUE}[5/7] SDK Modules${NC}"

check_item "SDK structure" "[ -d sdks/core/include/chirp/core ]"
check_item "SDK chat module" "[ -f sdks/core/include/chirp/core/modules/chat/chat_module.h ]"
check_item "SDK social module" "[ -f sdks/core/include/chirp/core/modules/social/social_module.h ]"
check_item "SDK voice module" "[ -f sdks/core/include/chirp/core/modules/voice/voice_module.h ]"

check_item "Chat module impl" "[ -f sdks/core/src/modules/chat/chat_module_impl.cc ]"
check_item "Social module impl" "[ -f sdks/core/src/modules/social/social_module_impl.cc ]"
check_item "Voice module impl" "[ -f sdks/core/src/modules/voice/voice_module_impl.cc ]"

echo ""
echo -e "${BLUE}[6/7] Testing & Tools${NC}"

check_item "Integration test" "[ -f tests/integration/integration_test.cc ]"
check_item "Integration CMakeLists" "[ -f tests/integration/CMakeLists.txt ]"
check_item "Test runner script" "[ -f tests/run_integration_tests.sh ]"
check_item "CLI test client" "[ -f apps/cli_client/src/main.cc ]"
check_item "CLI client CMakeLists" "[ -f apps/cli_client/CMakeLists.txt ]"

echo ""
echo -e "${BLUE}[7/7] Deployment & Documentation${NC}"

check_item "Docker Compose" "[ -f docker-compose.yml ]"
check_item "Database schema" "[ -f scripts/init_db.sql ]"
check_item "Build script" "[ -f scripts/build_and_test.sh ]"
check_item "README" "[ -f README.md ]"
check_item "Implementation summary" "[ -f IMPLEMENTATION_SUMMARY.md ]"

# Optional: Check if protobuf files exist
echo ""
echo -e "${BLUE}[8/7] Generated Files${NC}"

PROTO_COUNT=$(find proto/cpp/proto -name "*.pb.h" 2>/dev/null | wc -l)
if [ "$PROTO_COUNT" -gt 0 ]; then
    echo "  Found $PROTO_COUNT protobuf header files"
    check_item "Common protobuf" "[ -f proto/cpp/proto/common.pb.h ]"
    check_item "Gateway protobuf" "[ -f proto/cpp/proto/gateway.pb.h ]"
    check_item "Auth protobuf" "[ -f proto/cpp/proto/auth.pb.h ]"
    check_item "Chat protobuf" "[ -f proto/cpp/proto/chat.pb.h ]"
else
    echo -e "  ${YELLOW}! No protobuf files found (run protoc or use pre-generated)${NC}"
fi

echo ""
echo -e "${BLUE}=== Validation Summary ===${NC}"

if [ $VALIDATION_FAILED -eq 0 ]; then
    echo -e "${GREEN}All validations passed! ($VALIDATION_PASSED items)${NC}"
    echo ""
    echo "Project structure is complete. You can now:"
    echo "  1. Build the project: ./scripts/build_and_test.sh"
    echo "  2. Start services: docker compose up -d"
    echo "  3. Run tests: bash tests/run_integration_tests.sh"
    echo "  4. Run login smoke without Docker: bash tests/run_integration_tests.sh --local-services --gateway-port 5500 --auth-port 6500"
    exit 0
else
    echo -e "${RED}Validation failed: $VALIDATION_FAILED required item(s) missing${NC}"
    echo ""
    echo "Please ensure all required files are present."
    exit 1
fi
