# Lib: Common
# Contains: Logger, Config Loader, Utils

file(GLOB SRCS "*.cc")
file(GLOB HEADERS "*.h")

add_library(chirp_common STATIC ${SRCS} ${HEADERS})

target_include_directories(chirp_common PUBLIC ${CMAKE_SOURCE_DIR}/libs)

target_link_libraries(chirp_common PUBLIC
    # fmt
    # spdlog
)
