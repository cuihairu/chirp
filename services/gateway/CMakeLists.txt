
# Gateway Service

find_package(Threads REQUIRED)

# Collect proto sources
file(GLOB PROTO_SRCS "${CMAKE_SOURCE_DIR}/proto/cpp/proto/*.cc")

# Collect service sources
file(GLOB_RECURSE SERVICE_SRCS "src/*.cc")

add_executable(chirp_gateway
    ${SERVICE_SRCS}
    ${PROTO_SRCS}
)

target_link_libraries(chirp_gateway
    PRIVATE
    chirp_network
    ${PROTOBUF_LIBRARIES}
    Threads::Threads
)

target_include_directories(chirp_gateway PRIVATE src)
