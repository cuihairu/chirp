# Chirp Core SDK
cmake_minimum_required(VERSION 3.20)

# Get the project root directory (parent of sdks/)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

find_package(Threads REQUIRED)

# Proto files from project root
file(GLOB PROTO_SRCS "${PROJECT_ROOT}/proto/cpp/proto/*.cc")

set(SDK_PUBLIC_HEADERS
  include/chirp/sdk.h
  include/chirp/sdk_client.h
)

set(SDK_SOURCES
  src/sdk.cc
  src/sdk_client.cc
)

# Include libs subdirectories
set(LIBS_DIR "${PROJECT_ROOT}/libs")
set(NETWORK_DIR "${LIBS_DIR}/network")
set(COMMON_DIR "${LIBS_DIR}/common")
set(PROTO_DIR "${PROJECT_ROOT}/proto/cpp")

# Include network library (provides asio)
include(FetchContent)
if(TARGET asio::asio)
    add_library(chirp_asio INTERFACE)
    target_link_libraries(chirp_asio INTERFACE asio::asio)
    target_compile_definitions(chirp_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
else()
    FetchContent_Declare(
        asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG asio-1-30-2
    )
    FetchContent_MakeAvailable(asio)
    add_library(chirp_asio INTERFACE)
    target_include_directories(chirp_asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
    target_compile_definitions(chirp_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
endif()

# Build network library
file(GLOB NETWORK_SRCS "${NETWORK_DIR}/*.cc")
file(GLOB NETWORK_HEADERS "${NETWORK_DIR}/*.h")
add_library(chirp_network STATIC ${NETWORK_SRCS} ${NETWORK_HEADERS})
target_include_directories(chirp_network PUBLIC ${LIBS_DIR})
target_link_libraries(chirp_network PUBLIC chirp_asio Threads::Threads)

# Build common library
file(GLOB COMMON_SRCS "${COMMON_DIR}/*.cc")
file(GLOB COMMON_HEADERS "${COMMON_DIR}/*.h")
add_library(chirp_common STATIC ${COMMON_SRCS} ${COMMON_HEADERS})
target_include_directories(chirp_common PUBLIC ${LIBS_DIR} ${PROTO_DIR})
target_link_libraries(chirp_common PUBLIC Threads::Threads)

# Link protobuf if found via vcpkg
if(TARGET protobuf::libprotobuf)
    target_link_libraries(chirp_network PUBLIC protobuf::libprotobuf)
    target_link_libraries(chirp_common PUBLIC protobuf::libprotobuf)
elseif(DEFINED PROTOBUF_LIBRARIES)
    target_link_libraries(chirp_network PUBLIC ${PROTOBUF_LIBRARIES})
    target_link_libraries(chirp_common PUBLIC ${PROTOBUF_LIBRARIES})
endif()

# Link abseil if found via vcpkg
if(TARGET absl::strings)
    target_link_libraries(chirp_common PUBLIC absl::strings absl::spinlock_wait absl::base)
endif()

# Static library
add_library(chirp_core_sdk_static STATIC
  ${SDK_SOURCES}
  ${PROTO_SRCS}
)

target_link_libraries(chirp_core_sdk_static
  PUBLIC
  chirp_network
  chirp_common
  Threads::Threads
)

target_include_directories(chirp_core_sdk_static
  PUBLIC
    ${LIBS_DIR}
    ${PROTO_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Shared library
add_library(chirp_core_sdk SHARED
  ${SDK_SOURCES}
  ${PROTO_SRCS}
)

target_link_libraries(chirp_core_sdk
  PUBLIC
  chirp_network
  chirp_common
  Threads::Threads
)

target_include_directories(chirp_core_sdk
  PUBLIC
    ${LIBS_DIR}
    ${PROTO_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# SDK example program
add_executable(sdk_example
  examples/sdk_example.cc
)

target_link_libraries(sdk_example
  PRIVATE
  chirp_core_sdk
)

target_include_directories(sdk_example
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
