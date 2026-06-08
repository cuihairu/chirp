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

function(chirp_configure_auth_target target_name)
    target_link_libraries(${target_name}
        PRIVATE
        chirp_network
        chirp_common
        ${absl_pkg_LIBRARIES}
        ${LIBSODIUM_LIBRARIES}
        Threads::Threads
    )

    if(TARGET protobuf::libprotobuf)
        target_link_libraries(${target_name} PRIVATE protobuf::libprotobuf)
    elseif(DEFINED PROTOBUF_LIBRARIES)
        target_link_libraries(${target_name} PRIVATE ${PROTOBUF_LIBRARIES})
    endif()

    target_include_directories(${target_name} PRIVATE src)
    target_include_directories(${target_name} PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
endfunction()

if(MYSQL_FOUND AND (TARGET unofficial-sodium::sodium OR LIBSODIUM_FOUND))
    add_executable(chirp_auth
        ${ENHANCED_AUTH_SRCS}
        ${PROTO_SRCS}
        "src/main_enhanced.cc"
    )

    chirp_configure_auth_target(chirp_auth)
    target_link_libraries(chirp_auth PRIVATE ${MYSQL_CLIENT_LIBRARIES})
    target_include_directories(chirp_auth PRIVATE ${MYSQL_INCLUDE_DIRS})

    add_executable(chirp_auth_enhanced ALIAS chirp_auth)
else()
    add_executable(chirp_auth
        "src/main.cc"
        ${PROTO_SRCS}
    )

    chirp_configure_auth_target(chirp_auth)

    message(WARNING
        "MySQL client library or libsodium not found; chirp_auth will use the basic token validation implementation")
endif()
