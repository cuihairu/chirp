# App Gateway Service (companion-app edge)
#
# Reuses the game gateway's edge components (session registry, auth client,
# redis session manager) and the notification plane's rpc client instead of
# duplicating them; the only app_gateway-specific translation unit is main.cc.

find_package(Threads REQUIRED)


add_executable(chirp_app_gateway
    src/main.cc
    ${CMAKE_SOURCE_DIR}/services/gateway/src/gateway_session_registry.cc
    ${CMAKE_SOURCE_DIR}/services/gateway/src/auth_client.cc
    ${CMAKE_SOURCE_DIR}/services/gateway/src/redis_session_manager.cc
    ${CMAKE_SOURCE_DIR}/services/notification/src/notification_client.cc
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
    ${CMAKE_SOURCE_DIR}/services/gateway/src
    ${CMAKE_SOURCE_DIR}/services/notification/src
)
