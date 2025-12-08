
# Gateway Service

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
    ${PROTOBUF_LIBRARIES}
    pthread
)

target_include_directories(chirp_gateway PRIVATE src)
