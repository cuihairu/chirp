cmake_minimum_required(VERSION 3.15)
project(chirp_monorepo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------
find_package(Protobuf REQUIRED)
# Asio, gRPC, etc. would go here

# ------------------------------------------------------------------------------
# Global Include Paths
# ------------------------------------------------------------------------------
include_directories(
    ${CMAKE_SOURCE_DIR}/libs
    ${CMAKE_SOURCE_DIR}/proto/cpp
    ${PROTOBUF_INCLUDE_DIRS}
)

# ------------------------------------------------------------------------------
# Subdirectories (Monorepo Modules)
# ------------------------------------------------------------------------------

# 1. Libraries (Shared Code)
add_subdirectory(libs/common)
add_subdirectory(libs/network)

# 2. Services (Microservices)
add_subdirectory(services/gateway)
# add_subdirectory(services/auth)
# add_subdirectory(services/chat)
# add_subdirectory(services/router)

# 3. SDKs
# add_subdirectory(sdks)

# 4. Apps
# add_subdirectory(apps)

# 5. Tools / Tests
# add_subdirectory(tools/benchmark)
