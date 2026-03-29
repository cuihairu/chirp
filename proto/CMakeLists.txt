# Protocol Buffers

file(GLOB PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.proto")

set(PROTO_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cpp/proto")
file(MAKE_DIRECTORY "${PROTO_CPP_DIR}")

set(PROTO_GENERATED_SRCS)
set(PROTO_GENERATED_HDRS)
set(PROTO_REL_FILES)

set(PROTOC_BIN "")
if(TARGET protobuf::protoc)
  get_target_property(PROTOC_BIN protobuf::protoc IMPORTED_LOCATION)
endif()
if(NOT PROTOC_BIN AND DEFINED Protobuf_PROTOC_EXECUTABLE)
  set(PROTOC_BIN "${Protobuf_PROTOC_EXECUTABLE}")
endif()
if(NOT PROTOC_BIN)
  find_program(PROTOC_BIN protoc)
endif()

if(PROTOC_BIN)
  foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME "${PROTO_FILE}" NAME_WE)
    list(APPEND PROTO_GENERATED_SRCS "${PROTO_CPP_DIR}/${PROTO_NAME}.pb.cc")
    list(APPEND PROTO_GENERATED_HDRS "${PROTO_CPP_DIR}/${PROTO_NAME}.pb.h")
    list(APPEND PROTO_REL_FILES "proto/${PROTO_NAME}.proto")
  endforeach()

  add_custom_command(
    OUTPUT ${PROTO_GENERATED_SRCS} ${PROTO_GENERATED_HDRS}
    COMMAND ${PROTOC_BIN} --proto_path=${CMAKE_SOURCE_DIR} --cpp_out=${CMAKE_CURRENT_SOURCE_DIR}/cpp ${PROTO_REL_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating C++ protobuf sources"
    VERBATIM
  )

  add_custom_target(chirp_proto_codegen DEPENDS ${PROTO_GENERATED_SRCS} ${PROTO_GENERATED_HDRS})
  add_library(chirp_protos STATIC ${PROTO_GENERATED_SRCS})
  add_dependencies(chirp_protos chirp_proto_codegen)
else()
  message(WARNING "protoc not found; falling back to checked-in protobuf C++ sources")
  file(GLOB PROTO_GENERATED_SRCS "${PROTO_CPP_DIR}/*.pb.cc")
  file(GLOB PROTO_GENERATED_HDRS "${PROTO_CPP_DIR}/*.pb.h")
  add_library(chirp_protos STATIC ${PROTO_GENERATED_SRCS})
endif()

target_include_directories(chirp_protos PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  "${PROTO_CPP_DIR}"
)

if(TARGET protobuf::libprotobuf)
  target_link_libraries(chirp_protos PUBLIC protobuf::libprotobuf)
elseif(DEFINED PROTOBUF_LIBRARIES)
  target_link_libraries(chirp_protos PUBLIC ${PROTOBUF_LIBRARIES})
endif()

if(WIN32)
  target_link_libraries(chirp_protos PUBLIC ws2_32)
endif()

if(MSVC)
  target_compile_options(chirp_protos PRIVATE /wd4244 /wd4267)
else()
  target_compile_options(chirp_protos PRIVATE -Wno-sign-compare)
endif()
