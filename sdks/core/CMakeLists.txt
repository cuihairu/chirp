# Chirp Core SDK

# 将 SDK 构建为静态库和共享库
find_package(Threads REQUIRED)

# Proto 文件
file(GLOB PROTO_SRCS "${CMAKE_SOURCE_DIR}/proto/cpp/proto/*.cc")

set(SDK_PUBLIC_HEADERS
  include/chirp/sdk.h
  include/chirp/sdk_client.h
)

set(SDK_SOURCES
  src/sdk.cc
  src/sdk_client.cc
)

# 静态库
add_library(chirp_core_sdk_static STATIC
  ${SDK_SOURCES}
  ${PROTO_SRCS}
)

target_link_libraries(chirp_core_sdk_static
  PUBLIC
  chirp_network
  chirp_common
  ${PROTOBUF_LIBRARIES}
  ${absl_pkg_LIBRARIES}
  Threads::Threads
)

target_include_directories(chirp_core_sdk_static
  PUBLIC
    ${CMAKE_SOURCE_DIR}/libs
    ${CMAKE_SOURCE_DIR}/proto/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 共享库
add_library(chirp_core_sdk SHARED
  ${SDK_SOURCES}
  ${PROTO_SRCS}
)

target_link_libraries(chirp_core_sdk
  PUBLIC
  chirp_network
  chirp_common
  ${PROTOBUF_LIBRARIES}
  ${absl_pkg_LIBRARIES}
  Threads::Threads
)

target_include_directories(chirp_core_sdk
  PUBLIC
    ${CMAKE_SOURCE_DIR}/libs
    ${CMAKE_SOURCE_DIR}/proto/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# SDK 示例程序
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
