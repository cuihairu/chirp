# Chat Service

find_package(Threads REQUIRED)

# Find MySQL for enhanced chat service
find_package(MySQL REQUIRED)

file(GLOB PROTO_SRCS "${CMAKE_SOURCE_DIR}/proto/cpp/proto/*.cc")
file(GLOB_RECURSE SERVICE_SRCS "src/*.cc")

# Exclude the distributed main files from the standard build
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*main_distributed\\.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*main_enhanced\\.cc")

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

# ==============================================================================
# Enhanced Chat Service (with Hybrid Message Store)
# ==============================================================================

add_executable(chirp_chat_enhanced
    src/main_enhanced.cc
    ${PROTO_SRCS}
    src/group_manager.cc
    src/read_receipt_manager.cc
    src/mysql_message_store.cc
    src/message_store_config.cc
    src/hybrid_message_store.cc
    src/message_delivery_tracker.cc
    src/message_migration_worker.cc
    src/paginated_history_retriever.cc
    src/channel_manager.cc
    src/reaction_manager.cc
    src/message_edit_manager.cc
    src/typing_manager.cc
    src/mention_manager.cc
    src/file_storage_manager.cc
)

target_link_libraries(chirp_chat_enhanced
    PRIVATE
    chirp_network
    chirp_common
    ${PROTOBUF_LIBRARIES}
    ${absl_pkg_LIBRARIES}
    ${MYSQL_CLIENT_LIBRARIES}
    Threads::Threads
)

target_include_directories(chirp_chat_enhanced PRIVATE src)
target_include_directories(chirp_chat_enhanced PRIVATE ${MYSQL_INCLUDE_DIRS})
