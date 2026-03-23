# Notification Service

find_package(Threads REQUIRED)

file(GLOB PROTO_SRCS "${CMAKE_SOURCE_DIR}/proto/cpp/proto/*.cc")
file(GLOB_RECURSE SERVICE_SRCS "src/*.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*main\\.cc$")

add_executable(chirp_notification
    src/main.cc
    ${SERVICE_SRCS}
    ${PROTO_SRCS}
)

target_link_libraries(chirp_notification
    PRIVATE
    chirp_network
    chirp_common
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)

if(TARGET protobuf::libprotobuf)
    target_link_libraries(chirp_notification PRIVATE protobuf::libprotobuf)
elseif(DEFINED PROTOBUF_LIBRARIES)
    target_link_libraries(chirp_notification PRIVATE ${PROTOBUF_LIBRARIES})
endif()

target_include_directories(chirp_notification PRIVATE src)

# Optional: libcurl for HTTP requests
find_package(CURL)
if(CURL_FOUND)
    target_link_libraries(chirp_notification PRIVATE CURL::libcurl)
    target_compile_definitions(chirp_notification PRIVATE USE_CURL=1)
endif()
