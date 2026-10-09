# Search Service (chirp_search, message_search 批)
#
# MySQL messages 表 -> SQLite FTS5 检索索引；5007 挂 gateway 的 per-client
# pipe。复用 services/shared/chat 的 runtime_utils（CLI 参数）与
# chat_validation（PRIVATE 频道可见性判据）。

find_package(Threads REQUIRED)
find_package(MySQL QUIET)
# unofficial-sqlite3 是 vcpkg 端口，apt-only 环境（演示镜像等）没有该配置包；
# search 不在演示栈（deployments/docker/docker-compose.deploy.yml），缺依赖时
# 跳过本目录而不是让整个 configure 炸掉。
find_package(unofficial-sqlite3 CONFIG QUIET)
if(NOT unofficial-sqlite3_FOUND)
    message(STATUS "unofficial-sqlite3 not found (vcpkg port); skipping chirp_search")
    return()
endif()

file(GLOB_RECURSE SERVICE_SRCS "src/*.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*main\\.cc$")

add_executable(chirp_search
    src/main.cc
    ${SERVICE_SRCS}
    ${CMAKE_SOURCE_DIR}/services/shared/chat/src/runtime_utils.cc
    ${CMAKE_SOURCE_DIR}/services/shared/chat/src/chat_validation.cc
    # MySQLConnectionPool（tail 泵与查询核对的权威事实源）。
    ${CMAKE_SOURCE_DIR}/services/shared/chat/src/mysql_message_store.cc
)

target_link_libraries(chirp_search
    PRIVATE
    chirp_network
    chirp_common
    chirp_protos
    ${absl_pkg_LIBRARIES}
    unofficial::sqlite3::sqlite3
    Threads::Threads
)

if(MYSQL_FOUND)
    target_link_libraries(chirp_search PRIVATE ${MYSQL_CLIENT_LIBRARIES})
    target_include_directories(chirp_search PRIVATE ${MYSQL_INCLUDE_DIRS})
else()
    message(FATAL_ERROR "MySQL client library not found; chirp_search requires MySQL as its authoritative source")
endif()

target_include_directories(chirp_search PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/services/shared/chat/src
)
