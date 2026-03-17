# Load Tester Application

cmake_minimum_required(VERSION 3.20)
project(chirp_load_tester CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/../..
    ${CMAKE_SOURCE_DIR}/../../libs/common/include
    ${CMAKE_SOURCE_DIR}/../../libs/network/include
    ${CMAKE_SOURCE_DIR}/../../proto
    ${PROTOBUF_INCLUDE_DIRS}
)

# Source files
file(GLOB_RECURSE PROTO_SRCS "${CMAKE_SOURCE_DIR}/../../proto/cpp/proto/*.cc")
file(GLOB_RECURSE COMMON_SRCS "${CMAKE_SOURCE_DIR}/../../libs/common/src/*.cc")
file(GLOB_RECURSE NETWORK_SRCS "${CMAKE_SOURCE_DIR}/../../libs/network/src/*.cc")

add_executable(chirp_load_tester
    src/main.cc
    src/load_tester.cc
    ${PROTO_SRCS}
    ${COMMON_SRCS}
    ${NETWORK_SRCS}
)

target_link_libraries(chirp_load_tester
    PRIVATE
    ${PROTOBUF_LIBRARIES}
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)

if(UNIX AND NOT APPLE)
    target_link_libraries(chirp_load_tester PRIVATE pthread)
endif()

if(WIN32)
    target_link_libraries(chirp_load_tester PRIVATE ws2_32)
endif()

# Install target
install(TARGETS chirp_load_tester DESTINATION bin)
