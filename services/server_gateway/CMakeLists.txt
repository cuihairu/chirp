# Server Gateway Service (trusted service-plane hub: game backend <-> chirp)

find_package(Threads REQUIRED)


set(SG_SRCS
    src/event_queue.cc
    src/server_gateway_handlers.cc
    src/service_registry.cc
    src/stream_broker.cc
)

add_executable(chirp_server_gateway
    src/main.cc
    ${SG_SRCS}
)

target_link_libraries(chirp_server_gateway
    PRIVATE
    chirp_network
    chirp_common
    chirp_protos
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)


target_include_directories(chirp_server_gateway PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/libs
    ${CMAKE_SOURCE_DIR}/proto/cpp
)
