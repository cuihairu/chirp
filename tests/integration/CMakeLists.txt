# Integration Tests

cmake_minimum_required(VERSION 3.20)
project(chirp_integration_test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Get the chirp root directory (two levels up from integration test)
get_filename_component(CHIRP_ROOT ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
get_filename_component(CHIRP_ROOT ${CHIRP_ROOT} DIRECTORY)

# Find required packages
find_package(Threads REQUIRED)

# Find protobuf via vcpkg
find_package(protobuf CONFIG REQUIRED)

# Use FetchContent for ASIO (header-only)
include(FetchContent)
if(NOT TARGET asio)
  FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(asio)
endif()

# Include directories
include_directories(${CHIRP_ROOT})
include_directories(${CHIRP_ROOT}/libs)
include_directories(${CHIRP_ROOT}/sdks/core/include)
include_directories(${CHIRP_ROOT}/proto/cpp)

# Add ASIO include directory
if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/_deps/asio-src/asio/include")
  include_directories("${CMAKE_CURRENT_BINARY_DIR}/_deps/asio-src/asio/include")
endif()

# Protocol buffers - use pre-generated files
set(PROTO_CPP_PATH ${CHIRP_ROOT}/proto/cpp/proto)
file(GLOB PROTO_SRCS "${PROTO_CPP_PATH}/*.pb.cc")

# Integration test executable
add_executable(chirp_integration_test
  integration_test.cc
  ${PROTO_SRCS}
)

# Link against libraries from main build
target_link_libraries(chirp_integration_test
  ${CHIRP_ROOT}/build/libs/common/Debug/chirp_common.lib
  ${CHIRP_ROOT}/build/libs/network/Debug/chirp_network.lib
  protobuf::libprotobuf
  Threads::Threads
)

# Platform-specific libraries
if(WIN32)
  target_link_libraries(chirp_integration_test ws2_32 winmm)
endif()

# Add test
enable_testing()
add_test(NAME IntegrationTest COMMAND chirp_integration_test)
