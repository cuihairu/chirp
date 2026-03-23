# Search Service

find_package(Threads REQUIRED)

file(GLOB PROTO_SRCS "${CMAKE_SOURCE_DIR}/proto/cpp/proto/*.cc")
file(GLOB_RECURSE SERVICE_SRCS "src/*.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*main\\.cc$")

add_executable(chirp_search
    src/main.cc
    ${SERVICE_SRCS}
    ${PROTO_SRCS}
)

target_link_libraries(chirp_search
    PRIVATE
    chirp_network
    chirp_common
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)

if(TARGET protobuf::libprotobuf)
    target_link_libraries(chirp_search PRIVATE protobuf::libprotobuf)
elseif(DEFINED PROTOBUF_LIBRARIES)
    target_link_libraries(chirp_search PRIVATE ${PROTOBUF_LIBRARIES})
endif()

target_include_directories(chirp_search PRIVATE src)
