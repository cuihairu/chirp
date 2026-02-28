# Lib: Network
# Contains: TCP Connection, MsgCodec (Packet Parser)

include(FetchContent)
find_package(Threads REQUIRED)

# Standalone Asio (header-only)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
)
FetchContent_MakeAvailable(asio)
add_library(chirp_asio INTERFACE)
target_include_directories(chirp_asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(chirp_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)

file(GLOB SRCS "*.cc")
file(GLOB HEADERS "*.h")

add_library(chirp_network STATIC ${SRCS} ${HEADERS})

target_include_directories(chirp_network PUBLIC
    ${CMAKE_SOURCE_DIR}/libs
)

target_link_libraries(chirp_network PUBLIC
    chirp_common
    chirp_asio
    Threads::Threads
)

# Link against protobuf if found via vcpkg
if(TARGET protobuf::libprotobuf)
    target_link_libraries(chirp_network PUBLIC protobuf::libprotobuf)
elseif(DEFINED PROTOBUF_LIBRARIES)
    target_link_libraries(chirp_network PUBLIC ${PROTOBUF_LIBRARIES})
endif()
