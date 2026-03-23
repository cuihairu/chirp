# Lib: Network
# Contains: TCP Connection, MsgCodec (Packet Parser)

find_package(Threads REQUIRED)

if(NOT TARGET chirp_asio)
    message(FATAL_ERROR "chirp_asio target must be defined before libs/network")
endif()

file(GLOB SRCS "*.cc")
file(GLOB HEADERS "*.h")

# Exclude redis_client.cc if we need to customize the build
# But we want to include message_router.cc
set(SRCS ${SRCS})

add_library(chirp_network STATIC ${SRCS} ${HEADERS})

target_include_directories(chirp_network PUBLIC
    ${CMAKE_SOURCE_DIR}/libs
)

if(MSVC)
    target_compile_options(chirp_network PRIVATE /FS)
endif()

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
