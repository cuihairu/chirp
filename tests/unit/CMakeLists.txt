# Unit tests

if(NOT GTest_FOUND)
  return()
endif()

# Common library tests
add_executable(common_tests
  common_test.cc
  ${CMAKE_SOURCE_DIR}/libs/common/base64.cc
  ${CMAKE_SOURCE_DIR}/libs/common/sha256.cc
  ${CMAKE_SOURCE_DIR}/libs/common/jwt.cc
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
