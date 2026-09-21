# Server Gateway Service (trusted service-plane hub: game backend <-> chirp)

find_package(Threads REQUIRED)


set(SG_SRCS
    src/event_queue.cc
    src/identity_registry.cc
    src/subscription_registry.cc
    src/server_gateway_handlers.cc
    src/service_registry.cc
    src/stream_broker.cc
    src/unread_ledger.cc
)

add_executable(chirp_game_server_gateway
    src/main.cc
    ${SG_SRCS}
)

target_link_libraries(chirp_game_server_gateway
    PRIVATE
    chirp_network
    chirp_common
    chirp_protos
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)


target_include_directories(chirp_game_server_gateway PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/libs
    ${CMAKE_SOURCE_DIR}/proto/cpp
)
