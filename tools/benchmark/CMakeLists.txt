# Tools: Benchmark / simple integration clients

find_package(Threads REQUIRED)

add_executable(chirp_ping_client
    ping_client.cc
    ${CMAKE_SOURCE_DIR}/proto/cpp/proto/common.pb.cc
    ${CMAKE_SOURCE_DIR}/proto/cpp/proto/auth.pb.cc
    ${CMAKE_SOURCE_DIR}/proto/cpp/proto/gateway.pb.cc
)

target_link_libraries(chirp_ping_client
    PRIVATE
    chirp_network
    chirp_common
    ${PROTOBUF_LIBRARIES}
    Threads::Threads
)

target_include_directories(chirp_ping_client PRIVATE ${CMAKE_SOURCE_DIR}/libs ${CMAKE_SOURCE_DIR}/proto/cpp)

