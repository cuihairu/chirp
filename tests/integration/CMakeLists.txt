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

find_path(ASIO_INCLUDE_DIR
  NAMES asio.hpp
  PATHS
    ${CHIRP_ROOT}/build/_deps/asio-src/asio/include
    ${CHIRP_ROOT}/third_party/asio/asio/include
    ${CHIRP_ROOT}/third_party/asio/include
)

# Include directories
include_directories(${CHIRP_ROOT})
include_directories(${CHIRP_ROOT}/libs)
include_directories(${CHIRP_ROOT}/libs/common)
include_directories(${CHIRP_ROOT}/libs/common/include)
include_directories(${CHIRP_ROOT}/sdks/core/include)
include_directories(${CHIRP_ROOT}/proto/cpp)
if(ASIO_INCLUDE_DIR)
  include_directories(${ASIO_INCLUDE_DIR})
  add_compile_definitions(ASIO_STANDALONE ASIO_NO_DEPRECATED)
endif()

# Integration test executable
add_executable(chirp_integration_test
  integration_test.cc
)

if(TARGET chirp_network AND TARGET chirp_common AND TARGET chirp_protos)
  target_link_libraries(chirp_integration_test
    PRIVATE
    chirp_common
    chirp_network
    chirp_protos
    Threads::Threads
  )
else()
  if(WIN32)
    set(CHIRP_COMMON_LIB "${CHIRP_ROOT}/build/libs/common/Debug/chirp_common.lib")
    set(CHIRP_NETWORK_LIB "${CHIRP_ROOT}/build/libs/network/Debug/chirp_network.lib")
    set(CHIRP_PROTOS_LIB "${CHIRP_ROOT}/build/proto/Debug/chirp_protos.lib")
  else()
    set(CHIRP_COMMON_LIB "${CHIRP_ROOT}/build/libs/common/libchirp_common.a")
    set(CHIRP_NETWORK_LIB "${CHIRP_ROOT}/build/libs/network/libchirp_network.a")
    set(CHIRP_PROTOS_LIB "${CHIRP_ROOT}/build/proto/libchirp_protos.a")
  endif()

  target_link_libraries(chirp_integration_test
    PRIVATE
    ${CHIRP_COMMON_LIB}
    ${CHIRP_NETWORK_LIB}
    ${CHIRP_PROTOS_LIB}
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
