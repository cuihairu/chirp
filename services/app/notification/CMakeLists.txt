# Notification Service

find_package(Threads REQUIRED)

file(GLOB_RECURSE SERVICE_SRCS "src/*.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*main\\.cc$")

add_executable(chirp_app_notification
    src/main.cc
    ${SERVICE_SRCS}
)

target_link_libraries(chirp_app_notification
    PRIVATE
    chirp_network
    chirp_common
    chirp_protos
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)


target_include_directories(chirp_app_notification PRIVATE src)

# Provider HTTP goes through the in-tree asio/OpenSSL transport
# (http_push_transport.h); there is deliberately no libcurl dependency.

