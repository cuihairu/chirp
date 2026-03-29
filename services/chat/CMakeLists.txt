# Chat Service

find_package(Threads REQUIRED)

# Find MySQL for enhanced chat service only
find_package(MySQL QUIET)

file(GLOB PROTO_SRCS "${CMAKE_SOURCE_DIR}/proto/cpp/proto/*.cc")
file(GLOB_RECURSE SERVICE_SRCS "src/*.cc")

# Exclude enhanced/experimental sources from the standard build
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*main_distributed\\.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*main_enhanced\\.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*mysql_message_store\\.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*message_store_config\\.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*hybrid_message_store\\.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*message_delivery_tracker\\.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*message_migration_worker\\.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*paginated_history_retriever\\.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*channel_manager\\.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*mention_manager\\.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*message_edit_manager\\.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*file_storage_manager\\.cc")

add_executable(chirp_chat
    ${SERVICE_SRCS}
    ${PROTO_SRCS}
)

target_link_libraries(chirp_chat
    PRIVATE
    chirp_network
    chirp_common
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)

if(TARGET protobuf::libprotobuf)
    target_link_libraries(chirp_chat PRIVATE protobuf::libprotobuf)
elseif(DEFINED PROTOBUF_LIBRARIES)
    target_link_libraries(chirp_chat PRIVATE ${PROTOBUF_LIBRARIES})
endif()

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
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)

if(TARGET protobuf::libprotobuf)
    target_link_libraries(chirp_chat_distributed PRIVATE protobuf::libprotobuf)
elseif(DEFINED PROTOBUF_LIBRARIES)
    target_link_libraries(chirp_chat_distributed PRIVATE ${PROTOBUF_LIBRARIES})
endif()

target_include_directories(chirp_chat_distributed PRIVATE src)

# Add an alias for convenience
add_executable(chat::dist ALIAS chirp_chat_distributed)

# ==============================================================================
# Enhanced Chat Service (with Hybrid Message Store)
# ==============================================================================

if(MYSQL_FOUND)
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
    )

    target_link_libraries(chirp_chat_enhanced
        PRIVATE
        chirp_network
        chirp_common
        ${absl_pkg_LIBRARIES}
        ${MYSQL_CLIENT_LIBRARIES}
        Threads::Threads
    )

    if(TARGET protobuf::libprotobuf)
        target_link_libraries(chirp_chat_enhanced PRIVATE protobuf::libprotobuf)
    elseif(DEFINED PROTOBUF_LIBRARIES)
        target_link_libraries(chirp_chat_enhanced PRIVATE ${PROTOBUF_LIBRARIES})
    endif()

    target_include_directories(chirp_chat_enhanced PRIVATE src)
    target_include_directories(chirp_chat_enhanced PRIVATE ${MYSQL_INCLUDE_DIRS})
else()
    message(WARNING "MySQL client library not found; skipping chirp_chat_enhanced target")
endif()
