# Lib: Common
# Contains: Logger, Config Loader, Utils

file(GLOB SRCS "*.cc")
file(GLOB HEADERS "*.h")

add_library(chirp_common STATIC ${SRCS} ${HEADERS})

target_link_libraries(chirp_common PUBLIC
    # fmt
    # spdlog
)
