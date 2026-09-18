# Party Service

find_package(Threads REQUIRED)

file(GLOB_RECURSE SERVICE_SRCS "src/*.cc")

add_executable(chirp_party
    ${SERVICE_SRCS}
)

target_link_libraries(chirp_party
    PRIVATE
    chirp_network
    chirp_common
    chirp_protos
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)

target_include_directories(chirp_party PRIVATE src)
