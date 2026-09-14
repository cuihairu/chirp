# App Gateway Service (companion-app edge)
#
# The only app_gateway-specific translation unit is main.cc; everything else
# comes from public libs (chirp_network carries the session registry, auth
# client, redis session manager and the notification rpc client).

find_package(Threads REQUIRED)


add_executable(chirp_app_gateway
    src/main.cc
)

target_link_libraries(chirp_app_gateway
    PRIVATE
    chirp_network
    chirp_common
    chirp_protos
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)


target_include_directories(chirp_app_gateway PRIVATE
    src
)
