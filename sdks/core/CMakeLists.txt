# Chirp Core SDK
cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Get the project root directory (parent of sdks/)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

find_package(Threads REQUIRED)

# Standalone SDK builds compile the checked-in protobuf gencode
# (proto/cpp), so protobuf must be discoverable here: the vcpkg
# toolchain provides the CONFIG package, the CMake module form covers
# system installs. QUIET keeps protobuf optional for pure consumers
# that do not compile the generated sources.
if(NOT TARGET protobuf::libprotobuf)
    find_package(Protobuf CONFIG QUIET)
endif()
if(NOT TARGET protobuf::libprotobuf)
    find_package(Protobuf QUIET)
endif()

# Proto gencode: in-tree builds link the shared chirp_protos static lib;
# standalone SDK builds (no chirp tree around this file) compile the
# checked-in gencode from proto/cpp.
set(SDK_PROTO_SRCS "")
set(SDK_EXTRA_LIBS "")
if(TARGET chirp_protos)
    list(APPEND SDK_EXTRA_LIBS chirp_protos)
else()
    file(GLOB SDK_PROTO_SRCS "${PROJECT_ROOT}/proto/cpp/proto/*.cc")
endif()

set(SDK_PUBLIC_HEADERS
  include/chirp/sdk.h
  include/chirp/sdk_client.h
  include/chirp/message_interceptor.h
  include/chirp/command_handler.h
  include/chirp/message_store.h
  include/chirp/file_message_store.h
  include/chirp/auth_provider.h
  include/chirp/chat_event_listener.h
  include/chirp/word_filter.h
)

# The shared chirp_core_sdk links the static chirp_network/chirp_common, so
# those objects must be position-independent wherever they were created (the
# in-tree root build or the standalone build below); otherwise the .so link
# fails with R_X86_64_PC32 against non-PIC objects.
set(SDK_PIC_LIBS chirp_network chirp_common)

set(SDK_SOURCES
  src/sdk.cc
  src/sdk_client.cc
  src/message_store.cc
)

# Include libs subdirectories
set(LIBS_DIR "${PROJECT_ROOT}/libs")
set(NETWORK_DIR "${LIBS_DIR}/network")
set(COMMON_DIR "${LIBS_DIR}/common")
set(PROTO_DIR "${PROJECT_ROOT}/proto/cpp")

# Only create chirp_asio if it doesn't exist (for standalone SDK builds)
if(NOT TARGET chirp_asio)
    find_path(ASIO_INCLUDE_DIR
        NAMES asio.hpp
        PATHS
            "${PROJECT_ROOT}/third_party/asio/asio/include"
            "${PROJECT_ROOT}/third_party/asio/include"
        PATH_SUFFIXES include asio/include
    )

    if(ASIO_INCLUDE_DIR)
        add_library(chirp_asio INTERFACE)
        target_include_directories(chirp_asio INTERFACE ${ASIO_INCLUDE_DIR})
        target_compile_definitions(chirp_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
    elseif(TARGET asio::asio)
        add_library(chirp_asio INTERFACE)
        target_link_libraries(chirp_asio INTERFACE asio::asio)
        target_compile_definitions(chirp_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
    else()
        if(DEFINED CHIRP_FETCH_DEPS AND NOT CHIRP_FETCH_DEPS)
            message(FATAL_ERROR
                "ASIO headers were not found for standalone SDK build. Preferred fix: configure with the "
                "vcpkg toolchain so vcpkg.json can provide asio, or enable CHIRP_FETCH_DEPS.")
        endif()
        include(FetchContent)
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
endif()

# Only create chirp_network if it doesn't exist
if(NOT TARGET chirp_network)
    file(GLOB NETWORK_SRCS "${NETWORK_DIR}/*.cc")
    file(GLOB NETWORK_HEADERS "${NETWORK_DIR}/*.h")
    add_library(chirp_network STATIC ${NETWORK_SRCS} ${NETWORK_HEADERS})
    # NETWORK_SRCS includes the auth/session/notification clients, whose
    # headers include generated proto headers -- the same include surface the
    # in-tree build gets from the PUBLIC chirp_protos dependency.
    target_include_directories(chirp_network PUBLIC ${LIBS_DIR} ${PROTO_DIR})
    target_link_libraries(chirp_network PUBLIC chirp_asio Threads::Threads)

    # Link protobuf if found via vcpkg
    if(TARGET protobuf::libprotobuf)
        target_link_libraries(chirp_network PUBLIC protobuf::libprotobuf)
    elseif(DEFINED PROTOBUF_LIBRARIES)
        target_link_libraries(chirp_network PUBLIC ${PROTOBUF_LIBRARIES})
    endif()
endif()

# Only create chirp_common if it doesn't exist
if(NOT TARGET chirp_common)
    file(GLOB COMMON_SRCS "${COMMON_DIR}/*.cc")
    file(GLOB COMMON_HEADERS "${COMMON_DIR}/*.h")
    add_library(chirp_common STATIC ${COMMON_SRCS} ${COMMON_HEADERS})
    target_include_directories(chirp_common PUBLIC ${LIBS_DIR} ${PROTO_DIR})
    target_link_libraries(chirp_common PUBLIC Threads::Threads)

    # Link protobuf if found via vcpkg
    if(TARGET protobuf::libprotobuf)
        target_link_libraries(chirp_common PUBLIC protobuf::libprotobuf)
    elseif(DEFINED PROTOBUF_LIBRARIES)
        target_link_libraries(chirp_common PUBLIC ${PROTOBUF_LIBRARIES})
    endif()

    # Link abseil if found via vcpkg
    if(TARGET absl::strings)
        target_link_libraries(chirp_common PUBLIC absl::strings absl::spinlock_wait absl::base)
    endif()
endif()

# Static library
foreach(pilib IN LISTS SDK_PIC_LIBS)
  if(TARGET ${pilib})
    set_target_properties(${pilib} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endif()
endforeach()

add_library(chirp_core_sdk_static STATIC
  ${SDK_SOURCES}
  ${SDK_PROTO_SRCS}
)

target_link_libraries(chirp_core_sdk_static
  PUBLIC
  chirp_network
  chirp_common
  ${SDK_EXTRA_LIBS}
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
  ${SDK_PROTO_SRCS}
)

target_link_libraries(chirp_core_sdk
  PUBLIC
  chirp_network
  chirp_common
  ${SDK_EXTRA_LIBS}
  Threads::Threads
)

target_include_directories(chirp_core_sdk
  PUBLIC
    ${LIBS_DIR}
    ${PROTO_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# SDK example program (only if built standalone or explicitly enabled)
if(CHIRP_BUILD_SDK_EXAMPLES)
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
endif()
