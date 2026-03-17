# Chat Service

find_package(Threads REQUIRED)

file(GLOB PROTO_SRCS "${CMAKE_SOURCE_DIR}/proto/cpp/proto/*.cc")
file(GLOB_RECURSE SERVICE_SRCS "src/*.cc")

# Exclude the distributed main from the standard build
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*main_distributed\\.cc")

add_executable(chirp_chat
    ${SERVICE_SRCS}
    ${PROTO_SRCS}
)

target_link_libraries(chirp_chat
    PRIVATE
    chirp_network
    chirp_common
    ${PROTOBUF_LIBRARIES}
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)

target_include_directories(chirp_chat PRIVATE src)

# ==============================================================================
# Distributed Chat Service (with MessageRouter support)
# ==============================================================================

add_executable(chirp_chat_distributed
    src/main_distributed.cc
    ${PROTO_SRCS}
    src/group_manager.cc
    src/read_receipt_manager.cc
)

target_link_libraries(chirp_chat_distributed
    PRIVATE
    chirp_network
    chirp_common
    ${PROTOBUF_LIBRARIES}
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)

target_include_directories(chirp_chat_distributed PRIVATE src)

# Add an alias for convenience
add_executable(chat::dist ALIAS chirp_chat_distributed)
