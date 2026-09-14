# Lib: Common
# Contains: Logger, Config Loader, Utils, Metrics

file(GLOB SRCS "*.cc")
file(GLOB SRC_SUB_SRCS "src/*.cc")
file(GLOB HEADERS "*.h")

add_library(chirp_common STATIC ${SRCS} ${SRC_SUB_SRCS} ${HEADERS})

if(TARGET chirp_asio)
  # The mini metrics HTTP server is ASIO-based.
  target_link_libraries(chirp_common PUBLIC chirp_asio)
endif()

target_include_directories(chirp_common PUBLIC
    ${CMAKE_SOURCE_DIR}/libs
    ${CMAKE_SOURCE_DIR}/libs/common
    ${CMAKE_SOURCE_DIR}/libs/common/include
)

if(MSVC)
    target_compile_options(chirp_common PRIVATE /FS)
endif()

target_link_libraries(chirp_common PUBLIC
    # fmt
    # spdlog
)
