#!/bin/bash

# Chirp Project Validation Script (Simplified)

echo "=== Chirp Project Validation ==="
echo ""

PASSED=0
FAILED=0

check_file() {
    if [ -f "$1" ]; then
        echo "  ✓ $1"
        ((PASSED++))
    else
        echo "  ✗ $1 (missing)"
        ((FAILED++))
    fi
}

check_dir() {
    if [ -d "$1" ]; then
        echo "  ✓ $1/"
        ((PASSED++))
    else
        echo "  ✗ $1/ (missing)"
        ((FAILED++))
    fi
}

echo "[1/6] Protocol Definitions"
check_file "proto/common.proto"
check_file "proto/gateway.proto"
check_file "proto/auth.proto"
check_file "proto/chat.proto"
check_file "proto/social.proto"
check_file "proto/voice.proto"

echo ""
echo "[2/6] Services"
check_file "services/gateway/src/main.cc"
check_file "services/auth/src/main.cc"
check_file "services/chat/src/main.cc"
check_file "services/social/src/main.cc"
check_file "services/voice/src/main.cc"

echo ""
echo "[3/6] Libraries"
check_file "libs/common/logger.cc"
check_file "libs/network/tcp_server.cc"
check_file "libs/network/websocket_server.cc"
check_file "libs/network/tcp_client.cc"
check_file "libs/network/websocket_client.cc"

echo ""
echo "[4/6] SDK"
check_file "sdks/core/include/chirp/core/sdk.h"
check_file "sdks/core/include/chirp/core/client.h"
check_file "sdks/core/include/chirp/core/modules/chat/chat_module.h"
check_file "sdks/core/include/chirp/core/modules/social/social_module.h"
check_file "sdks/core/include/chirp/core/modules/voice/voice_module.h"

echo ""
echo "[5/6] Testing"
check_file "tests/integration/integration_test.cc"
check_file "apps/cli_client/src/main.cc"

echo ""
echo "[6/6] Deployment"
check_file "docker-compose.yml"
check_file "scripts/init_db.sql"
check_file "scripts/build_and_test.sh"

echo ""
echo "=== Validation Summary ==="
echo "Passed: $PASSED"
echo "Failed: $FAILED"

if [ $FAILED -eq 0 ]; then
    echo "✓ All validations passed!"
    exit 0
else
    echo "✗ Some validations failed"
    exit 1
fi
