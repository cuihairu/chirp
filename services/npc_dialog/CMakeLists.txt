# NPC Dialog Service (server-plane client, no player-facing listener)
#
# Reuses the chat service's server_gateway_peer (same repo-wide convention:
# services compile shared TUs by path instead of a common library); the
# npc_dialog-specific units are the rule engine, the responder, and main.cc.

find_package(Threads REQUIRED)


add_executable(chirp_npc_dialog
    src/main.cc
    src/npc_engine.cc
    src/npc_responder.cc
    ${CMAKE_SOURCE_DIR}/services/chat/src/server_gateway_peer.cc
    ${CMAKE_SOURCE_DIR}/services/chat/src/runtime_utils.cc
)

target_link_libraries(chirp_npc_dialog
    PRIVATE
    chirp_network
    chirp_common
    chirp_protos
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)


target_include_directories(chirp_npc_dialog PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/services/chat/src
)
