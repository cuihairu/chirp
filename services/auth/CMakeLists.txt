# Auth Service

find_package(Threads REQUIRED)

# Find MySQL for enhanced auth targets only
find_package(MySQL QUIET)

# Find libsodium (prefer vcpkg target, fallback to pkg-config)
find_package(unofficial-sodium CONFIG QUIET)
if(TARGET unofficial-sodium::sodium)
  set(LIBSODIUM_LIBRARIES unofficial-sodium::sodium)
else()
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(LIBSODIUM QUIET libsodium)
  endif()
endif()

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

# chirp_auth target
target_link_libraries(chirp_auth
    PRIVATE
    chirp_network
    chirp_common
    ${absl_pkg_LIBRARIES}
    ${LIBSODIUM_LIBRARIES}
    Threads::Threads
)

if(TARGET protobuf::libprotobuf)
    target_link_libraries(chirp_auth PRIVATE protobuf::libprotobuf)
elseif(DEFINED PROTOBUF_LIBRARIES)
    target_link_libraries(chirp_auth PRIVATE ${PROTOBUF_LIBRARIES})
endif()

target_include_directories(chirp_auth PRIVATE src)
target_include_directories(chirp_auth PRIVATE ${LIBSODIUM_INCLUDE_DIRS})

if(MYSQL_FOUND AND (TARGET unofficial-sodium::sodium OR LIBSODIUM_FOUND))
    add_executable(chirp_auth_enhanced
        ${ENHANCED_AUTH_SRCS}
        ${PROTO_SRCS}
        "src/main_enhanced.cc"
    )

    # chirp_auth_enhanced target
    target_link_libraries(chirp_auth_enhanced
        PRIVATE
        chirp_network
        chirp_common
        ${absl_pkg_LIBRARIES}
        ${LIBSODIUM_LIBRARIES}
        ${MYSQL_CLIENT_LIBRARIES}
        Threads::Threads
    )

    if(TARGET protobuf::libprotobuf)
        target_link_libraries(chirp_auth_enhanced PRIVATE protobuf::libprotobuf)
    elseif(DEFINED PROTOBUF_LIBRARIES)
        target_link_libraries(chirp_auth_enhanced PRIVATE ${PROTOBUF_LIBRARIES})
    endif()

    target_include_directories(chirp_auth_enhanced PRIVATE src)
    target_include_directories(chirp_auth_enhanced PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
    target_include_directories(chirp_auth_enhanced PRIVATE ${MYSQL_INCLUDE_DIRS})
else()
    message(WARNING
        "MySQL client library or libsodium not found; skipping chirp_auth_enhanced target")
endif()
