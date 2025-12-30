cmake_minimum_required(VERSION 3.15)
project(chirp_monorepo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(Protobuf REQUIRED)

# Find Abseil (required for Protobuf 6.x)
set(CHIRP_NEEDS_ABSL OFF)
if(NOT Protobuf_VERSION)
  # Older CMake FindProtobuf may not expose a version; keep the previous behavior to be safe.
  set(CHIRP_NEEDS_ABSL ON)
elseif(Protobuf_VERSION VERSION_GREATER_EQUAL "6.0")
  set(CHIRP_NEEDS_ABSL ON)
endif()

if(CHIRP_NEEDS_ABSL)
  # Try using PkgConfig for system Abseil first
  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(absl_pkg absl_base absl_log)
  endif()

  if(absl_pkg_FOUND)
    # Use system Abseil from Homebrew
    message(STATUS "Using system Abseil from: ${absl_pkg_INCLUDE_DIRS}")
    include_directories(SYSTEM ${absl_pkg_INCLUDE_DIRS})
    link_directories(${absl_pkg_LIBRARY_DIRS})
    add_compile_options(${absl_pkg_CFLAGS_OTHER})
    # Add missing log library that pkg-config doesn't include
    list(APPEND absl_pkg_LIBRARIES absl_log_internal_check_op)
  else()
    # Use FetchContent for Abseil
    include(FetchContent)
    FetchContent_Declare(
      abseil_cpp
      GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
      GIT_TAG 20240116.2
      GIT_SHALLOW TRUE
    )
    set(ABSL_PROPAGATE_CXX_STD ON)
    set(ABSL_ENABLE_INSTALL ON)
    FetchContent_MakeAvailable(abseil_cpp)
  endif()
endif()

# Use FetchContent for ASIO (header-only)
if(NOT TARGET asio)
  include(FetchContent)
  FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(asio)
endif()

# ------------------------------------------------------------------------------
# Global Include Paths
# ------------------------------------------------------------------------------
# Include Abseil headers first to avoid macro conflicts
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
add_subdirectory(services/auth)
add_subdirectory(services/chat)
# add_subdirectory(services/router)

# 3. SDKs
add_subdirectory(sdks/core)

# 4. Apps
# add_subdirectory(apps)

# 5. Tools / Tests
add_subdirectory(tools/benchmark)

# 6. Unit Tests (optional, requires GTest)
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt)
  option(ENABLE_TESTS "Enable unit tests" ON)
  if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()
endif()
