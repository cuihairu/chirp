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
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto/common.pb.cc
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto/gateway.pb.cc
)

target_link_libraries(network_tests
  PRIVATE
  GTest::gtest
  GTest::gtest_main
  ${absl_pkg_LIBRARIES}
)

if(TARGET protobuf::libprotobuf)
  target_link_libraries(network_tests PRIVATE protobuf::libprotobuf)
elseif(DEFINED PROTOBUF_LIBRARIES)
  target_link_libraries(network_tests PRIVATE ${PROTOBUF_LIBRARIES})
endif()

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
  ${CMAKE_SOURCE_DIR}/services/chat/src/chat_session_registry.cc
  ${CMAKE_SOURCE_DIR}/services/chat/src/chat_validation.cc
  ${CMAKE_SOURCE_DIR}/libs/network/protobuf_framing.cc
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto/auth.pb.cc
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto/chat.pb.cc
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto/common.pb.cc
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto/gateway.pb.cc
)

target_link_libraries(chat_validation_tests
  PRIVATE
  GTest::gtest
  GTest::gtest_main
  ${absl_pkg_LIBRARIES}
)

if(TARGET protobuf::libprotobuf)
  target_link_libraries(chat_validation_tests PRIVATE protobuf::libprotobuf)
elseif(DEFINED PROTOBUF_LIBRARIES)
  target_link_libraries(chat_validation_tests PRIVATE ${PROTOBUF_LIBRARIES})
endif()

target_include_directories(chat_validation_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/chat/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/proto/cpp
)

add_test(NAME chat_validation_tests COMMAND chat_validation_tests)

# Gateway session registry tests
add_executable(gateway_session_registry_tests
  gateway_session_registry_test.cc
  ${CMAKE_SOURCE_DIR}/services/gateway/src/gateway_session_registry.cc
)

target_link_libraries(gateway_session_registry_tests
  PRIVATE
  GTest::gtest
  GTest::gtest_main
  ${absl_pkg_LIBRARIES}
)

target_include_directories(gateway_session_registry_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/gateway/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/proto/cpp
)

add_test(NAME gateway_session_registry_tests COMMAND gateway_session_registry_tests)

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
  ${CMAKE_SOURCE_DIR}/services/chat/src/distributed_dispatch.cc
  ${CMAKE_SOURCE_DIR}/services/chat/src/distributed_runtime.cc
  ${CMAKE_SOURCE_DIR}/services/chat/src/group_manager.cc
  ${CMAKE_SOURCE_DIR}/services/chat/src/group_handlers.cc
  ${CMAKE_SOURCE_DIR}/services/chat/src/message_handlers.cc
  ${CMAKE_SOURCE_DIR}/services/chat/src/reaction_manager.cc
  ${CMAKE_SOURCE_DIR}/services/chat/src/read_receipt_manager.cc
  ${CMAKE_SOURCE_DIR}/services/chat/src/message_edit_manager.cc
  ${CMAKE_SOURCE_DIR}/services/chat/src/mention_manager.cc
  ${CMAKE_SOURCE_DIR}/services/chat/src/runtime_utils.cc
  ${CMAKE_SOURCE_DIR}/services/chat/src/typing_manager.cc
)
# main_distributed.cc is pulled in via #include with main() renamed in
# chat_managers_test.cc; compiling it separately would hijack the test entry
# point.

chirp_unit_test_common_links(chat_managers_tests)
target_link_libraries(chat_managers_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(chat_managers_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/chat/src
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
  ${CMAKE_SOURCE_DIR}/services/gateway/src/auth_client.cc
  ${CMAKE_SOURCE_DIR}/services/gateway/src/redis_session_manager.cc
)

chirp_unit_test_common_links(gateway_extra_tests)
target_link_libraries(gateway_extra_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(gateway_extra_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/gateway/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME gateway_extra_tests COMMAND gateway_extra_tests)

# Notification service coverage tests (internal access tests are compiled
# into the same binary: the implementation is compiled exactly once).
add_executable(notification_tests
  notification_test.cc
  notification_internal_test.cc
  ${CMAKE_SOURCE_DIR}/services/notification/src/notification_service.cc
)

chirp_unit_test_common_links(notification_tests)
target_link_libraries(notification_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(notification_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/notification/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME notification_tests COMMAND notification_tests)

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

# Core SDK (sdk.cc / sdk_client.cc) coverage tests
add_executable(sdk_core_tests
  sdk_core_test.cc
  ${CMAKE_SOURCE_DIR}/sdks/core/src/sdk.cc
  ${CMAKE_SOURCE_DIR}/sdks/core/src/sdk_client.cc
)

chirp_unit_test_common_links(sdk_core_tests)
target_link_libraries(sdk_core_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(sdk_core_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/sdks/core/include
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

# Chat extension managers (mentions / edits / channels / store config)
add_executable(chat_extensions_tests
  chat_extensions_test.cc
  ${CMAKE_SOURCE_DIR}/services/chat/src/channel_manager.cc
  ${CMAKE_SOURCE_DIR}/services/chat/src/mention_manager.cc
  ${CMAKE_SOURCE_DIR}/services/chat/src/message_edit_manager.cc
  ${CMAKE_SOURCE_DIR}/services/chat/src/message_store_config.cc
)

chirp_unit_test_common_links(chat_extensions_tests)
target_link_libraries(chat_extensions_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(chat_extensions_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/chat/src
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
  ${CMAKE_SOURCE_DIR}/services/server_gateway/src/event_queue.cc
  ${CMAKE_SOURCE_DIR}/services/server_gateway/src/server_gateway_handlers.cc
  ${CMAKE_SOURCE_DIR}/services/server_gateway/src/service_registry.cc
)

chirp_unit_test_common_links(server_gateway_tests)
target_link_libraries(server_gateway_tests PRIVATE chirp_network chirp_common chirp_protos)

target_include_directories(server_gateway_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/services/server_gateway/src
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/libs/common
  ${CMAKE_SOURCE_DIR}/libs/common/include
  ${CMAKE_SOURCE_DIR}/libs/network
  ${CMAKE_SOURCE_DIR}/proto/cpp
  ${CMAKE_SOURCE_DIR}/proto/cpp/proto
)

add_test(NAME server_gateway_tests COMMAND server_gateway_tests)

# NOTE: presence_manager_v2.cc is explicitly excluded from the social service
# build (it does not compile: proto enum conflicts and a copyable mutex); and
# the enhanced auth components (password_hasher / token_generator) require
# libsodium, which is not installed in this workspace, so they have never
# been part of an actual build. Both groups are tracked as dead code instead
# of being tested here.
