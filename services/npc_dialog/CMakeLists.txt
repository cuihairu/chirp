# NPC Dialog Service (server-plane client, no player-facing listener)
#
# Reuses the chat service's server_gateway_peer (same repo-wide convention:
# services compile shared TUs by path instead of a common library); the
# npc_dialog-specific units are the rule engine, the responder, and main.cc.

find_package(Threads REQUIRED)

file(GLOB PROTO_SRCS "${CMAKE_SOURCE_DIR}/proto/cpp/proto/*.cc")

add_executable(chirp_npc_dialog
    src/main.cc
    src/npc_engine.cc
    src/npc_responder.cc
    ${CMAKE_SOURCE_DIR}/services/chat/src/server_gateway_peer.cc
    ${CMAKE_SOURCE_DIR}/services/chat/src/runtime_utils.cc
    ${PROTO_SRCS}
)

target_link_libraries(chirp_npc_dialog
    PRIVATE
    chirp_network
    chirp_common
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)

if(TARGET protobuf::libprotobuf)
    target_link_libraries(chirp_npc_dialog PRIVATE protobuf::libprotobuf)
elseif(DEFINED PROTOBUF_LIBRARIES)
    target_link_libraries(chirp_npc_dialog PRIVATE ${PROTOBUF_LIBRARIES})
endif()

target_include_directories(chirp_npc_dialog PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/services/chat/src
)
