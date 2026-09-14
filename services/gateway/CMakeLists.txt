
# Gateway Service

find_package(Threads REQUIRED)

# Collect service sources
file(GLOB_RECURSE SERVICE_SRCS "src/*.cc")

add_executable(chirp_gateway
    ${SERVICE_SRCS}
)

target_link_libraries(chirp_gateway
    PRIVATE
    chirp_network
    chirp_common
    chirp_protos
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)

target_include_directories(chirp_gateway PRIVATE src)
