# NPC Dialog Service (server-plane client, no player-facing listener)
#
# Dials the server-plane hub through the shared ServerGatewayPeer in
# chirp_network; the npc_dialog-specific units are the rule engine, the
# responder, and main.cc.

find_package(Threads REQUIRED)


add_executable(chirp_npc_dialog
    src/main.cc
    src/npc_engine.cc
    src/npc_responder.cc
    ${CMAKE_SOURCE_DIR}/services/shared/chat/src/runtime_utils.cc
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
    ${CMAKE_SOURCE_DIR}/services/shared/chat/src
)
