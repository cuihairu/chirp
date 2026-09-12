# Server Gateway Service (trusted service-plane hub: game backend <-> chirp)

find_package(Threads REQUIRED)

file(GLOB PROTO_SRCS "${CMAKE_SOURCE_DIR}/proto/cpp/proto/*.cc")

set(SG_SRCS
    src/event_queue.cc
    src/server_gateway_handlers.cc
    src/service_registry.cc
)

add_executable(chirp_server_gateway
    src/main.cc
    ${SG_SRCS}
    ${PROTO_SRCS}
)

target_link_libraries(chirp_server_gateway
    PRIVATE
    chirp_network
    chirp_common
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)

if(TARGET protobuf::libprotobuf)
    target_link_libraries(chirp_server_gateway PRIVATE protobuf::libprotobuf)
elseif(DEFINED PROTOBUF_LIBRARIES)
    target_link_libraries(chirp_server_gateway PRIVATE ${PROTOBUF_LIBRARIES})
endif()

target_include_directories(chirp_server_gateway PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/libs
    ${CMAKE_SOURCE_DIR}/proto/cpp
)
