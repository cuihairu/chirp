# Notification Service

find_package(Threads REQUIRED)

file(GLOB PROTO_SRCS "${CMAKE_SOURCE_DIR}/proto/cpp/proto/*.cc")
file(GLOB_RECURSE SERVICE_SRCS "src/*.cc")

add_executable(chirp_notification
    src/main.cc
    ${SERVICE_SRCS}
    ${PROTO_SRCS}
)

target_link_libraries(chirp_notification
    PRIVATE
    chirp_network
    chirp_common
    ${PROTOBUF_LIBRARIES}
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)

target_include_directories(chirp_notification PRIVATE src)

# Optional: libcurl for HTTP requests
find_package(CURL)
if(CURL_FOUND)
    target_link_libraries(chirp_notification PRIVATE CURL::libcurl)
    target_compile_definitions(chirp_notification PRIVATE USE_CURL=1)
endif()
