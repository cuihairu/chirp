# Chat Service

find_package(Threads REQUIRED)

# Find MySQL for enhanced chat service only
find_package(MySQL QUIET)

file(GLOB PROTO_SRCS "${CMAKE_SOURCE_DIR}/proto/cpp/proto/*.cc")
file(GLOB_RECURSE CHAT_BASE_SRCS "src/*.cc")

list(FILTER CHAT_BASE_SRCS EXCLUDE REGEX ".*main_distributed\\.cc")
list(FILTER CHAT_BASE_SRCS EXCLUDE REGEX ".*main_enhanced\\.cc")
list(FILTER CHAT_BASE_SRCS EXCLUDE REGEX ".*mysql_message_store\\.cc")
list(FILTER CHAT_BASE_SRCS EXCLUDE REGEX ".*message_store_config\\.cc")
list(FILTER CHAT_BASE_SRCS EXCLUDE REGEX ".*hybrid_message_store\\.cc")
list(FILTER CHAT_BASE_SRCS EXCLUDE REGEX ".*message_delivery_tracker\\.cc")
list(FILTER CHAT_BASE_SRCS EXCLUDE REGEX ".*message_migration_worker\\.cc")
list(FILTER CHAT_BASE_SRCS EXCLUDE REGEX ".*paginated_history_retriever\\.cc")
list(FILTER CHAT_BASE_SRCS EXCLUDE REGEX ".*channel_manager\\.cc")
list(FILTER CHAT_BASE_SRCS EXCLUDE REGEX ".*mention_manager\\.cc")
list(FILTER CHAT_BASE_SRCS EXCLUDE REGEX ".*message_edit_manager\\.cc")
list(FILTER CHAT_BASE_SRCS EXCLUDE REGEX ".*file_storage_manager\\.cc")

function(chirp_configure_chat_target target_name)
    target_link_libraries(${target_name}
        PRIVATE
        chirp_network
        chirp_common
        ${absl_pkg_LIBRARIES}
        Threads::Threads
    )

    if(TARGET protobuf::libprotobuf)
        target_link_libraries(${target_name} PRIVATE protobuf::libprotobuf)
    elseif(DEFINED PROTOBUF_LIBRARIES)
        target_link_libraries(${target_name} PRIVATE ${PROTOBUF_LIBRARIES})
    endif()

    target_include_directories(${target_name} PRIVATE src)
endfunction()

if(MYSQL_FOUND)
    add_executable(chirp_chat
        src/distributed_dispatch.cc
        src/distributed_runtime.cc
        src/main_enhanced.cc
        src/runtime_utils.cc
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

    chirp_configure_chat_target(chirp_chat)
    target_link_libraries(chirp_chat PRIVATE ${MYSQL_CLIENT_LIBRARIES})
    target_include_directories(chirp_chat PRIVATE ${MYSQL_INCLUDE_DIRS})

    add_executable(chirp_chat_enhanced ALIAS chirp_chat)
else()
    add_executable(chirp_chat
        ${CHAT_BASE_SRCS}
        ${PROTO_SRCS}
    )

    chirp_configure_chat_target(chirp_chat)
    message(WARNING "MySQL client library not found; chirp_chat will use the basic in-memory/Redis implementation")
endif()

# ==============================================================================
# Distributed Chat Service (with MessageRouter support)
# ==============================================================================

add_executable(chirp_chat_distributed
    src/distributed_dispatch.cc
    src/distributed_runtime.cc
    src/main_distributed.cc
    src/runtime_utils.cc
    ${PROTO_SRCS}
    src/group_manager.cc
    src/read_receipt_manager.cc
)

chirp_configure_chat_target(chirp_chat_distributed)

# Add an alias for convenience
add_executable(chat::dist ALIAS chirp_chat_distributed)
