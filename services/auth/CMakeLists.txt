# Auth Service

find_package(Threads REQUIRED)

# Find MySQL
find_package(MySQL REQUIRED)

# Find libsodium
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSODIUM REQUIRED libsodium)

file(GLOB PROTO_SRCS "${CMAKE_SOURCE_DIR}/proto/cpp/proto/*.cc")

# Only build main.cc - enhanced auth files are incomplete (TODO: implement properly)
set(SERVICE_SRCS "src/main.cc")

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
    ${LIBSODIUM_LIBRARIES}
    ${MYSQL_CLIENT_LIBRARIES}
    Threads::Threads
)

target_include_directories(chirp_auth PRIVATE src)
target_include_directories(chirp_auth PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
target_include_directories(chirp_auth PRIVATE ${MYSQL_INCLUDE_DIRS})

