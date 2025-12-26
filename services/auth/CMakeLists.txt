# Auth Service

find_package(Threads REQUIRED)

file(GLOB PROTO_SRCS "${CMAKE_SOURCE_DIR}/proto/cpp/proto/*.cc")
file(GLOB_RECURSE SERVICE_SRCS "src/*.cc")

add_executable(chirp_auth
    ${SERVICE_SRCS}
    ${PROTO_SRCS}
)

target_link_libraries(chirp_auth
    PRIVATE
    chirp_network
    chirp_common
    ${PROTOBUF_LIBRARIES}
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)

target_include_directories(chirp_auth PRIVATE src)

