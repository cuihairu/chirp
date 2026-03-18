# Auth Service

find_package(Threads REQUIRED)

# Find MySQL
find_package(MySQL REQUIRED)

# Find libsodium
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSODIUM REQUIRED libsodium)

file(GLOB PROTO_SRCS "${CMAKE_SOURCE_DIR}/proto/cpp/proto/*.cc")

# Auth service source files
set(SERVICE_SRCS
  "src/main.cc"
)

# Enhanced auth service files (when fully implemented)
set(ENHANCED_AUTH_SRCS
  "src/auth_service.cc"
  "src/brute_force_protector.cc"
  "src/rate_limiter.cc"
  "src/user_store.cc"
  "src/session_store.cc"
  "src/redis_auth_store.cc"
  "src/password_hasher.cc"
  "src/token_generator.cc"
)

add_executable(chirp_auth
    ${SERVICE_SRCS}
    ${PROTO_SRCS}
)

add_executable(chirp_auth_enhanced
    ${ENHANCED_AUTH_SRCS}
    ${PROTO_SRCS}
    "src/main_enhanced.cc"
)

# chirp_auth target
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

# chirp_auth_enhanced target
target_link_libraries(chirp_auth_enhanced
    PRIVATE
    chirp_network
    chirp_common
    ${PROTOBUF_LIBRARIES}
    ${absl_pkg_LIBRARIES}
    ${LIBSODIUM_LIBRARIES}
    ${MYSQL_CLIENT_LIBRARIES}
    Threads::Threads
)

target_include_directories(chirp_auth_enhanced PRIVATE src)
target_include_directories(chirp_auth_enhanced PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
target_include_directories(chirp_auth_enhanced PRIVATE ${MYSQL_INCLUDE_DIRS})

