# Notification Service

find_package(Threads REQUIRED)

file(GLOB_RECURSE SERVICE_SRCS "src/*.cc")
list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*main\\.cc$")

add_executable(chirp_notification
    src/main.cc
    ${SERVICE_SRCS}
)

target_link_libraries(chirp_notification
    PRIVATE
    chirp_network
    chirp_common
    chirp_protos
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)


target_include_directories(chirp_notification PRIVATE src)

# Optional: libcurl for HTTP requests
find_package(CURL)
if(CURL_FOUND)
    target_link_libraries(chirp_notification PRIVATE CURL::libcurl)
    target_compile_definitions(chirp_notification PRIVATE USE_CURL=1)
endif()
