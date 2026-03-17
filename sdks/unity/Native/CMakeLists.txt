cmake_minimum_required(VERSION 3.20)
project(chirp_unity CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)

# Include chirp core SDK
set(CHIRP_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)

# Include directories
include_directories(
    ${CHIRP_ROOT_DIR}/sdks/core/include
    ${CHIRP_ROOT_DIR}/libs/common/include
    ${CHIRP_ROOT_DIR}/libs/network/include
    ${CHIRP_ROOT_DIR}/proto
    ${PROTOBUF_INCLUDE_DIRS}
)

# Source files
set(SOURCES
    src/chirp_unity_bridge.cc
)

# Collect all chirp library sources
file(GLOB_RECURSE CHIRP_CORE_SRCS
    ${CHIRP_ROOT_DIR}/sdks/core/src/*.cc
    ${CHIRP_ROOT_DIR}/libs/common/src/*.cc
    ${CHIRP_ROOT_DIR}/libs/network/src/*.cc
)

# Platform-specific configuration
if(WIN32)
    add_library(chirp_unity SHARED ${SOURCES} ${CHIRP_CORE_SRCS})
    target_compile_definitions(chirp_unity PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(chirp_unity
        PRIVATE
        ${PROTOBUF_LIBRARIES}
        ${absl_pkg_LIBRARIES}
        Threads::Threads
        ws2_32
    )
elseif(APPLE)
    if(IOS)
        # iOS configuration
        set(CMAKE_OSX_ARCHITECTURES "arm64")
        add_library(chirp_unity STATIC ${SOURCES} ${CHIRP_CORE_SRCS})
    else()
        # macOS configuration
        add_library(chirp_unity SHARED ${SOURCES} ${CHIRP_CORE_SRCS})
        set_target_properties(chirp_unity PROPERTIES
            PREFIX ""
            SUFFIX ".bundle"
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/macOS
        )
    endif()
    target_link_libraries(chirp_unity
        PRIVATE
        ${PROTOBUF_LIBRARIES}
        ${absl_pkg_LIBRARIES}
        Threads::Threads
    )
elseif(UNIX)
    # Linux configuration
    add_library(chirp_unity SHARED ${SOURCES} ${CHIRP_CORE_SRCS})
    set_target_properties(chirp_unity PROPERTIES
        PREFIX ""
        SUFFIX ".so"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Plugins/linux
    )
    target_link_libraries(chirp_unity
        PRIVATE
        ${PROTOBUF_LIBRARIES}
        ${absl_pkg_LIBRARIES}
        Threads::Threads
    )
endif()

# Android configuration (built with Gradle)
if(ANDROID)
    add_library(chirp_unity SHARED ${SOURCES} ${CHIRP_CORE_SRCS})
    target_link_libraries(chirp_unity
        PRIVATE
        ${PROTOBUF_LIBRARIES}
        ${absl_pkg_LIBRARIES}
        android
        log
    )
endif()

# Install targets
install(TARGETS chirp_unity
    RUNTIME DESTINATION Plugins
    LIBRARY DESTINATION Plugins
)
