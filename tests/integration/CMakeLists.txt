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

find_package(protobuf CONFIG QUIET)

# Include directories
include_directories(${CHIRP_ROOT})
include_directories(${CHIRP_ROOT}/libs)
include_directories(${CHIRP_ROOT}/sdks/core/include)
include_directories(${CHIRP_ROOT}/proto/cpp)

# Protocol buffers - use pre-generated files
set(PROTO_CPP_PATH ${CHIRP_ROOT}/proto/cpp/proto)
file(GLOB PROTO_SRCS "${PROTO_CPP_PATH}/*.pb.cc")

# Integration test executable
add_executable(chirp_integration_test
  integration_test.cc
  ${PROTO_SRCS}
)

if(TARGET chirp_network AND TARGET chirp_common)
  target_link_libraries(chirp_integration_test
    PRIVATE
    chirp_common
    chirp_network
    Threads::Threads
  )
else()
  target_link_libraries(chirp_integration_test
    PRIVATE
    ${CHIRP_ROOT}/build/libs/common/Debug/chirp_common.lib
    ${CHIRP_ROOT}/build/libs/network/Debug/chirp_network.lib
    Threads::Threads
  )
endif()

if(TARGET protobuf::libprotobuf)
  target_link_libraries(chirp_integration_test PRIVATE protobuf::libprotobuf)
elseif(DEFINED PROTOBUF_LIBRARIES)
  target_link_libraries(chirp_integration_test PRIVATE ${PROTOBUF_LIBRARIES})
endif()

# Platform-specific libraries
if(WIN32)
  target_link_libraries(chirp_integration_test ws2_32 winmm)
endif()

# Add test
enable_testing()
add_test(NAME IntegrationTest COMMAND chirp_integration_test)
