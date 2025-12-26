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
  ${PROTOBUF_LIBRARIES}
  ${absl_pkg_LIBRARIES}
)

target_include_directories(network_tests
  PRIVATE
  ${CMAKE_SOURCE_DIR}/libs
  ${CMAKE_SOURCE_DIR}/proto/cpp
)

add_test(NAME network_tests COMMAND network_tests)
