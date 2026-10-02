# CLI Test Client

find_package(Threads REQUIRED)

file(GLOB PROTO_SRCS "${CMAKE_SOURCE_DIR}/proto/cpp/proto/*.cc")
file(GLOB_RECURSE CLIENT_SRCS "src/*.cc")

add_executable(chirp_cli
    ${CLIENT_SRCS}
    ${PROTO_SRCS}
)

# PROTOBUF_LIBRARIES 是 protobuf-config 的 legacy 兼容变量:它抓的是
# LOCATION(Windows 上是 DLL 本体)而非 IMPLIB,直接喂链接器会 LNK1107;
# vcpkg/CONFIG 场景一律走 protobuf::libprotobuf(import lib 路径正确),
# 仅在纯模块模式发现的系统安装下退回旧变量。
if(TARGET protobuf::libprotobuf)
    target_link_libraries(chirp_cli PRIVATE protobuf::libprotobuf)
else()
    target_link_libraries(chirp_cli PRIVATE ${PROTOBUF_LIBRARIES})
endif()

target_link_libraries(chirp_cli
    PRIVATE
    chirp_network
    chirp_common
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)

target_include_directories(chirp_cli PRIVATE src)
