# Pre-generated Protocol Buffers

# Collect all pre-generated .pb.cc files
file(GLOB PROTO_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/proto/*.pb.cc
)

# Create a static library for pre-generated protobuf code
add_library(chirp_protos STATIC ${PROTO_SRCS})

# Set up include directories
target_include_directories(chirp_protos PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/proto
)

# Link with system protobuf library if available
if(TARGET protobuf::libprotobuf)
  target_link_libraries(chirp_protos PUBLIC protobuf::libprotobuf)
endif()

# On Windows, link with ws2_32 for winsock
if(WIN32)
  target_link_libraries(chirp_protos PUBLIC ws2_32)
endif()

# Suppress warnings in generated code
if(MSVC)
  target_compile_options(chirp_protos PRIVATE /wd4244 /wd4267)
else()
  target_compile_options(chirp_protos PRIVATE -Wno-sign-compare)
endif()
