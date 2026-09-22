# Unit tests

if(NOT GTest_FOUND)
  return()
endif()

find_package(Threads REQUIRED)

# Common helper for chirp unit test targets: protobuf, asio and threads.
function(chirp_unit_test_common_links target)
  target_link_libraries(${target}
    PRIVATE
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
    ${absl_pkg_LIBRARIES}
  )
  if(TARGET chirp_asio)
    target_link_libraries(${target} PRIVATE chirp_asio)
  endif()
  if(TARGET protobuf::libprotobuf)
    target_link_libraries(${target} PRIVATE protobuf::libprotobuf)
  elseif(DEFINED PROTOBUF_LIBRARIES)
    target_link_libraries(${target} PRIVATE ${PROTOBUF_LIBRARIES})
  endif()
endfunction()

# Common library tests
add_executable(common_tests
  common_test.cc
  ${CMAKE_SOURCE_DIR}/libs/common/base64.cc
  ${CMAKE_SOURCE_DIR}/libs/common/config.cc
  ${CMAKE_SOURCE_DIR}/libs/common/jwt.cc
  ${CMAKE_SOURCE_DIR}/libs/common/logger.cc
  ${CMAKE_SOURCE_DIR}/libs/common/sha256.cc
)

target_link_libraries(common_tests
  PRIVATE
  GTest::gtest
  GTest::gtest_main
  ${absl_pkg_LIBRARIES}
)

target_include_directories(common_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/proto/cpp
)

add_test(NAME common_tests COMMAND common_tests)

# Internal JWT helper tests (include the .cc directly to reach private helpers)
add_executable(common_jwt_internal_tests
  common_jwt_internal_test.cc
  ${CMAKE_SOURCE_DIR}/libs/common/base64.cc
  ${CMAKE_SOURCE_DIR}/libs/common/sha256.cc
)

target_link_libraries(common_jwt_internal_tests
  PRIVATE
  GTest::gtest
  GTest::gtest_main
  ${absl_pkg_LIBRARIES}
)

target_include_directories(common_jwt_internal_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/proto/cpp
)

add_test(NAME common_jwt_internal_tests COMMAND common_jwt_internal_tests)

# Network library tests
add_executable(network_tests
  network_test.cc
  ${CMAKE_SOURCE_DIR}/libs/network/protobuf_framing.cc
  ${CMAKE_SOURCE_DIR}/libs/network/length_prefixed_framer.cc
)

target_link_libraries(network_tests
  PRIVATE
  GTest::gtest
  GTest::gtest_main
  chirp_protos
  ${absl_pkg_LIBRARIES}
)

target_include_directories(network_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/proto/cpp
)

add_test(NAME network_tests COMMAND network_tests)

# Chat validation tests
add_executable(chat_validation_tests
  chat_validation_test.cc
  chat_login_verifier_test.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/chat_validation.cc
  ${CMAKE_SOURCE_DIR}/libs/common/login_token_verifier.cc
  ${CMAKE_SOURCE_DIR}/libs/network/protobuf_framing.cc
)

target_link_libraries(chat_validation_tests
  PRIVATE
  GTest::gtest
  GTest::gtest_main
  chirp_common
  chirp_protos
  ${absl_pkg_LIBRARIES}
)

target_include_directories(chat_validation_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/proto/cpp
)

add_test(NAME chat_validation_tests COMMAND chat_validation_tests)

# Shared session registry tests (libs/network/session_registry)
add_executable(session_registry_tests
  session_registry_test.cc
)

target_link_libraries(session_registry_tests
  PRIVATE
  GTest::gtest
  GTest::gtest_main
  chirp_network
  ${absl_pkg_LIBRARIES}
)

target_include_directories(session_registry_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/proto/cpp
)

add_test(NAME session_registry_tests COMMAND session_registry_tests)

# Full network library coverage tests (framing, websocket, tcp, redis)
add_executable(network_full_tests
  network_full_test.cc
)

chirp_unit_test_common_links(network_full_tests)
target_link_libraries(network_full_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(network_full_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME network_full_tests COMMAND network_full_tests)

# Chat service manager coverage tests
add_executable(chat_managers_tests
  chat_managers_test.cc
  chat_group_internal_test.cc
  chat_group_handlers_test.cc
  chat_message_handlers_test.cc
  chat_edit_mention_test.cc
  chat_inject_consumer_test.cc
  chat_npc_uplink_test.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/distributed_dispatch.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/distributed_runtime.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/delivery_ack_manager.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/group_manager.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/group_handlers.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/message_handlers.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/reaction_manager.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/read_receipt_manager.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/message_edit_manager.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/mention_manager.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/inject_consumer.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/npc_uplink.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/runtime_utils.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/typing_manager.cc
  ${CMAKE_SOURCE_DIR}/libs/common/login_token_verifier.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/push_bridge.cc
)
# main_distributed.cc is pulled in via #include with main() renamed in
# chat_managers_test.cc; compiling it separately would hijack the test entry
# point.

chirp_unit_test_common_links(chat_managers_tests)
target_link_libraries(chat_managers_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(chat_managers_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src
  ${CMAKE_SOURCE_DIR}/services/app/notification/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME chat_managers_tests COMMAND chat_managers_tests)

# Gateway auth client / redis session manager coverage tests
add_executable(gateway_extra_tests
  gateway_extra_test.cc
  gateway_auth_internal_test.cc
)

chirp_unit_test_common_links(gateway_extra_tests)
target_link_libraries(gateway_extra_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(gateway_extra_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME gateway_extra_tests COMMAND gateway_extra_tests)

# Voice service packet-handler tests (main.cc included with renamed main)
add_executable(voice_tests
  voice_service_test.cc
)

chirp_unit_test_common_links(voice_tests)
target_link_libraries(voice_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(voice_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/voice/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME voice_tests COMMAND voice_tests)

# Social service packet-handler tests (main.cc included with renamed main,
# same pattern as voice_tests; main.cc is NOT listed as a source - it would
# hijack the test entry point and double-compile the handlers)
add_executable(social_tests
  social_service_test.cc
)

chirp_unit_test_common_links(social_tests)
target_link_libraries(social_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(social_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/social/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME social_tests COMMAND social_tests)

# Party service packet-handler tests (main.cc included with renamed main,
# same pattern as social_tests; main.cc is NOT listed as a source)
add_executable(party_tests
  party_service_test.cc
)

chirp_unit_test_common_links(party_tests)
target_link_libraries(party_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(party_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/party/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME party_tests COMMAND party_tests)

# App gateway edge tests: scaffold login/logout, device-message forwarding
# through a real NotificationClient against a loopback server
add_executable(app_sdk_gateway_tests
  app_gateway_service_test.cc
)

chirp_unit_test_common_links(app_sdk_gateway_tests)
target_link_libraries(app_sdk_gateway_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(app_sdk_gateway_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME app_sdk_gateway_tests COMMAND app_sdk_gateway_tests)

# Game gateway edge tests: scaffold login binding (the 2c08a21 fix — without
# --auth_host the registry must still be bound or 2xxx chat packets are
# silently dropped) plus 2xxx forwarding through a real ChatBridge against
# the shared loopback fake chat server
add_executable(game_sdk_gateway_tests
  game_sdk_gateway_test.cc
)

chirp_unit_test_common_links(game_sdk_gateway_tests)
target_link_libraries(game_sdk_gateway_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(game_sdk_gateway_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME game_sdk_gateway_tests COMMAND game_sdk_gateway_tests)

# Gateway chat-bridge loopback tests against a scripted in-process fake chat
# server (shared with the app_sdk_gateway tests via fake_chat_server.h). The
# bridge itself comes from chirp_network.
add_executable(chat_bridge_tests
  chat_bridge_test.cc
)

chirp_unit_test_common_links(chat_bridge_tests)
target_link_libraries(chat_bridge_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(chat_bridge_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME chat_bridge_tests COMMAND chat_bridge_tests)

# Notification service coverage tests (internal access tests are compiled
# into the same binary: the implementation is compiled exactly once).
add_executable(notification_tests
  notification_test.cc
  notification_internal_test.cc
  push_transport_http_test.cc
  ${CMAKE_SOURCE_DIR}/services/app/notification/src/notification_service.cc
  ${CMAKE_SOURCE_DIR}/services/app/notification/src/push_transport.cc
  ${CMAKE_SOURCE_DIR}/services/app/notification/src/http_push_transport.cc
)

chirp_unit_test_common_links(notification_tests)
target_link_libraries(notification_tests PRIVATE chirp_network chirp_common chirp_protos)


target_include_directories(notification_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/app/notification/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME notification_tests COMMAND notification_tests)

# Notification plane packet handlers (6xxx dispatch over NotificationService)
add_executable(notification_handlers_tests
  notification_handlers_test.cc
  ${CMAKE_SOURCE_DIR}/services/app/notification/src/notification_handlers.cc
  ${CMAKE_SOURCE_DIR}/services/app/notification/src/notification_service.cc
  ${CMAKE_SOURCE_DIR}/services/app/notification/src/push_transport.cc
)

chirp_unit_test_common_links(notification_handlers_tests)
target_link_libraries(notification_handlers_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(notification_handlers_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/app/notification/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME notification_handlers_tests COMMAND notification_handlers_tests)

# Notification plane RPC client (worker-thread loopback tests) plus the chat
# push bridge that produces through it
add_executable(notification_client_tests
  notification_client_test.cc
  ${CMAKE_SOURCE_DIR}/services/app/notification/src/notification_handlers.cc
  ${CMAKE_SOURCE_DIR}/services/app/notification/src/notification_service.cc
  ${CMAKE_SOURCE_DIR}/services/app/notification/src/push_transport.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/push_bridge.cc
)

chirp_unit_test_common_links(notification_client_tests)
target_link_libraries(notification_client_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(notification_client_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/app/notification/src
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME notification_client_tests COMMAND notification_client_tests)

# Gateway auth client target above already compiles auth_client.cc together
# with gateway_auth_internal_test.cc; chat_managers_tests compiles
# group_manager.cc together with chat_group_internal_test.cc.

# Message search service coverage tests
add_executable(search_tests
  search_test.cc
  ${CMAKE_SOURCE_DIR}/services/search/src/message_search_service.cc
)

chirp_unit_test_common_links(search_tests)
target_link_libraries(search_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(search_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/search/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME search_tests COMMAND search_tests)

# Core SDK (sdk.cc / sdk_client.cc / message_store.cc) coverage tests
add_executable(sdk_core_tests
  sdk_core_test.cc
  ${CMAKE_SOURCE_DIR}/sdks/core/src/sdk.cc
  ${CMAKE_SOURCE_DIR}/sdks/core/src/sdk_client.cc
  ${CMAKE_SOURCE_DIR}/sdks/core/src/message_store.cc
)

chirp_unit_test_common_links(sdk_core_tests)
target_link_libraries(sdk_core_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(sdk_core_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/sdks/core/include
  ${CMAKE_SOURCE_DIR}/sdks/core/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME sdk_core_tests COMMAND sdk_core_tests)

# Network pure-logic layer tests (frames, RESP, router, server construction)
add_executable(network_logic_tests
  network_logic_test.cc
)

chirp_unit_test_common_links(network_logic_tests)
target_link_libraries(network_logic_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(network_logic_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME network_logic_tests COMMAND network_logic_tests)

# Session-layer tests driven by in-process socketpairs (no network stack)
add_executable(network_session_tests
  network_session_test.cc
)

chirp_unit_test_common_links(network_session_tests)
target_link_libraries(network_session_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(network_session_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME network_session_tests COMMAND network_session_tests)

# TLS edge sessions/servers driven over real 127.0.0.1 loopback (wss + TLS-TCP)
add_executable(network_tls_tests
  network_tls_test.cc
)

chirp_unit_test_common_links(network_tls_tests)
target_link_libraries(network_tls_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(network_tls_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME network_tls_tests COMMAND network_tls_tests)

# Chat extension managers (mentions / edits / channels / store config)
add_executable(chat_extensions_tests
  chat_extensions_test.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/channel_manager.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/mention_manager.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/message_edit_manager.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/message_store_config.cc
)

chirp_unit_test_common_links(chat_extensions_tests)
target_link_libraries(chat_extensions_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(chat_extensions_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME chat_extensions_tests COMMAND chat_extensions_tests)

# Server gateway (service plane hub) coverage tests
add_executable(server_gateway_tests
  server_gateway_test.cc
  server_gateway_stream_broker_test.cc
  ${CMAKE_SOURCE_DIR}/services/game/server_gateway/src/event_queue.cc
  ${CMAKE_SOURCE_DIR}/services/game/server_gateway/src/server_gateway_handlers.cc
  ${CMAKE_SOURCE_DIR}/services/game/server_gateway/src/service_registry.cc
  ${CMAKE_SOURCE_DIR}/services/game/server_gateway/src/stream_broker.cc
)

chirp_unit_test_common_links(server_gateway_tests)
target_link_libraries(server_gateway_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(server_gateway_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/game/server_gateway/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME server_gateway_tests COMMAND server_gateway_tests)

# App-plane player directory (WP-8): the identity/subscription/unread
# registries and the hub fan-out tail moved from game_server_gateway into
# app_chat; their coverage moved with them.
add_executable(chat_player_directory_tests
  chat_player_directory_test.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/identity_registry.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/player_directory.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/runtime_utils.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/subscription_registry.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/unread_ledger.cc
)

chirp_unit_test_common_links(chat_player_directory_tests)
target_link_libraries(chat_player_directory_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(chat_player_directory_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME chat_player_directory_tests COMMAND chat_player_directory_tests)

# Lexicon-based content filter (game_chat_features P0 敏感词过滤): lexicon
# parsing, the three-level policy and the hot-reload throttle.
add_executable(word_filter_tests
  word_filter_test.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/word_filter.cc
)

chirp_unit_test_common_links(word_filter_tests)
target_link_libraries(word_filter_tests PRIVATE chirp_common)

target_include_directories(word_filter_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
)

add_test(NAME word_filter_tests COMMAND word_filter_tests)

# Per-channel send pacing (game_chat_features P0 发送频率限制): interval
# table and the per-user/per-channel window anchoring.
add_executable(channel_pacer_tests
  channel_pacer_test.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/channel_pacer.cc
)

chirp_unit_test_common_links(channel_pacer_tests)
target_link_libraries(channel_pacer_tests PRIVATE chirp_protos)

target_include_directories(channel_pacer_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src
  ${CMAKE_SOURCE_DIR}/proto/cpp
)

add_test(NAME channel_pacer_tests COMMAND channel_pacer_tests)

# Repeat-message mute (game_chat_features P0 重复消息检测): streak
# counting, mute window and expiry semantics.
add_executable(repeat_guard_tests
  repeat_guard_test.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/repeat_guard.cc
)

chirp_unit_test_common_links(repeat_guard_tests)

target_include_directories(repeat_guard_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src
)

add_test(NAME repeat_guard_tests COMMAND repeat_guard_tests)
# NOTE: presence_manager.cc used to be excluded from the social build
# because it did not compile (a proto enum name clash plus a copyable-mutex
# member); it is fixed and covered by social_presence_tests below. The
# MySQL/sodium-backed components are exercised through scripted fake C API
# layers (fake_mysql.cc / fake_sodium.cc) so no external database or crypto
# library is needed to run the tests.

# Social presence manager (pure in-memory state machine)
add_executable(social_presence_tests
  social_presence_test.cc
  ${CMAKE_SOURCE_DIR}/services/social/src/presence_manager.cc
)

chirp_unit_test_common_links(social_presence_tests)
target_link_libraries(social_presence_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(social_presence_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/social/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME social_presence_tests COMMAND social_presence_tests)

# Common metrics library (counters/gauges/histograms + mini HTTP endpoint)
add_executable(common_metrics_tests
  common_metrics_test.cc
  ${CMAKE_SOURCE_DIR}/libs/common/src/metrics.cc
  ${CMAKE_SOURCE_DIR}/libs/common/src/metrics_http_server.cc
)

chirp_unit_test_common_links(common_metrics_tests)
target_link_libraries(common_metrics_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(common_metrics_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME common_metrics_tests COMMAND common_metrics_tests)

# Scripted fake MySQL C API + fake libsodium, shared by the auth/chat tests
# below. The include path is prepended so <mysql/mysql.h> / <sodium.h>
# resolve to the fakes instead of any system library.
function(chirp_fake_deps_include target)
  target_include_directories(${target} BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fake_mysql
    ${CMAKE_CURRENT_SOURCE_DIR}/fake_sodium
  )
endfunction()

# Enhanced auth stores (MySQL UserStore/SessionStore + RedisAuthStore)
add_executable(auth_stores_tests
  auth_stores_test.cc
  fake_mysql.cc
  fake_sodium.cc
  ${CMAKE_SOURCE_DIR}/services/app/auth/src/mysql_session_store.cc
  ${CMAKE_SOURCE_DIR}/services/app/auth/src/mysql_user_store.cc
  ${CMAKE_SOURCE_DIR}/services/app/auth/src/password_hasher.cc
  ${CMAKE_SOURCE_DIR}/services/app/auth/src/redis_auth_store.cc
  ${CMAKE_SOURCE_DIR}/services/app/auth/src/token_generator.cc
)

chirp_unit_test_common_links(auth_stores_tests)
target_link_libraries(auth_stores_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(auth_stores_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/app/auth/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)
chirp_fake_deps_include(auth_stores_tests)

add_test(NAME auth_stores_tests COMMAND auth_stores_tests)

# Enhanced auth components (hasher/token/limiter/protector/service)
add_executable(auth_service_tests
  auth_service_test.cc
  fake_mysql.cc
  fake_sodium.cc
  ${CMAKE_SOURCE_DIR}/services/app/auth/src/auth_service.cc
  ${CMAKE_SOURCE_DIR}/services/app/auth/src/brute_force_protector.cc
  ${CMAKE_SOURCE_DIR}/services/app/auth/src/password_hasher.cc
  ${CMAKE_SOURCE_DIR}/services/app/auth/src/rate_limiter.cc
  ${CMAKE_SOURCE_DIR}/services/app/auth/src/redis_auth_store.cc
  ${CMAKE_SOURCE_DIR}/services/app/auth/src/store_factory.cc
  ${CMAKE_SOURCE_DIR}/services/app/auth/src/token_generator.cc
  ${CMAKE_SOURCE_DIR}/services/app/auth/src/mysql_session_store.cc
  ${CMAKE_SOURCE_DIR}/services/app/auth/src/mysql_user_store.cc
)

chirp_unit_test_common_links(auth_service_tests)
target_link_libraries(auth_service_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(auth_service_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/app/auth/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)
chirp_fake_deps_include(auth_service_tests)

add_test(NAME auth_service_tests COMMAND auth_service_tests)

# MySQL-backed chat storage stack (connection pool / store / hybrid /
# migration worker / delivery tracker / pagination)
add_executable(chat_mysql_tests
  chat_mysql_test.cc
  fake_mysql.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/hybrid_message_store.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/message_delivery_tracker.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/message_migration_worker.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/message_store_config.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/message_store_factory.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/mysql_message_store.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/paginated_history_retriever.cc
)

chirp_unit_test_common_links(chat_mysql_tests)
target_link_libraries(chat_mysql_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(chat_mysql_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)
chirp_fake_deps_include(chat_mysql_tests)

add_test(NAME chat_mysql_tests COMMAND chat_mysql_tests)

# Client delivery-ack manager: pending tracking, timeout requeue, late-ack
# cleanup and the ack-capable session lifecycle.
add_executable(chat_delivery_ack_tests
  chat_delivery_ack_test.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/delivery_ack_manager.cc
)

chirp_unit_test_common_links(chat_delivery_ack_tests)
target_link_libraries(chat_delivery_ack_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(chat_delivery_ack_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
)

add_test(NAME chat_delivery_ack_tests COMMAND chat_delivery_ack_tests)

# Chat direct-entry rate limiting: login per-IP / send per-user fixed windows
# against the real RedisClient through the loopback fake server.
add_executable(chat_rate_limiter_tests
  chat_rate_limiter_test.cc
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src/chat_rate_limiter.cc
)

chirp_unit_test_common_links(chat_rate_limiter_tests)
target_link_libraries(chat_rate_limiter_tests PRIVATE chirp_network chirp_common)

target_include_directories(chat_rate_limiter_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
)
chirp_fake_deps_include(chat_rate_limiter_tests)

add_test(NAME chat_rate_limiter_tests COMMAND chat_rate_limiter_tests)

# Chat <-> server_gateway peer loopback tests (real TCP through the peer's
# reconnect/auth/heartbeat state machine).
add_executable(chat_hub_peer_tests
  chat_hub_peer_test.cc
)

chirp_unit_test_common_links(chat_hub_peer_tests)
target_link_libraries(chat_hub_peer_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(chat_hub_peer_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME chat_hub_peer_tests COMMAND chat_hub_peer_tests)

# Chat peer registration protocol loopback tests: the spoke-side link against
# a scripted fake hub, and the real hub against raw client sockets.
add_executable(chat_peer_tests
  chat_peer_test.cc
)

chirp_unit_test_common_links(chat_peer_tests)
target_link_libraries(chat_peer_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(chat_peer_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME chat_peer_tests COMMAND chat_peer_tests)

# NPC dialog service tests (rule engine + event responder with fake senders)
add_executable(npc_dialog_tests
  npc_engine_test.cc
  npc_responder_test.cc
  ${CMAKE_SOURCE_DIR}/services/game/npc_dialog/src/npc_engine.cc
  ${CMAKE_SOURCE_DIR}/services/game/npc_dialog/src/npc_responder.cc
)

chirp_unit_test_common_links(npc_dialog_tests)
target_link_libraries(npc_dialog_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(npc_dialog_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/game/npc_dialog/src
  ${CMAKE_SOURCE_DIR}/services/shared/chat/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME npc_dialog_tests COMMAND npc_dialog_tests)
