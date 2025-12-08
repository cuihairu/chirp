# Lib: Network
# Contains: TCP Connection, MsgCodec (Packet Parser)

file(GLOB SRCS "*.cc")
file(GLOB HEADERS "*.h")

add_library(chirp_network STATIC ${SRCS} ${HEADERS})

target_link_libraries(chirp_network PUBLIC
    chirp_common
    ${PROTOBUF_LIBRARIES}
    pthread
)
