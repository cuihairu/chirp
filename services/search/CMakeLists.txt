# Search Service

find_package(Threads REQUIRED)

file(GLOB_RECURSE SERVICE_SRCS "src/*.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*main\\.cc$")

add_executable(chirp_search
    src/main.cc
    ${SERVICE_SRCS}
)

target_link_libraries(chirp_search
    PRIVATE
    chirp_network
    chirp_common
    chirp_protos
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)


target_include_directories(chirp_search PRIVATE src)
