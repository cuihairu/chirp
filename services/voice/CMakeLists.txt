# Voice Service

find_package(Threads REQUIRED)

file(GLOB_RECURSE SERVICE_SRCS "src/*.cc")

# The whisper.cpp provider is an opt-in extra (CHIRP_ASR_WHISPER=ON pulls
# in the whisper headers/library); the default build stays dependency-free
# so CI never needs an ASR runtime. See src/asr/whisper_cpp_provider.h.
option(CHIRP_ASR_WHISPER "Build the whisper.cpp ASR provider" OFF)
if(NOT CHIRP_ASR_WHISPER)
    list(FILTER SERVICE_SRCS EXCLUDE REGEX ".*/asr/whisper_cpp_provider\\.cc")
endif()

# The moderation chain (VOICE-ASR-FILTER) reuses the shared chat lexicon
# filter - the true source, not a copy (same file chirp_chat compiles).
list(APPEND SERVICE_SRCS ${CMAKE_SOURCE_DIR}/services/shared/chat/src/word_filter.cc)

add_executable(chirp_voice
    ${SERVICE_SRCS}
)

target_link_libraries(chirp_voice
    PRIVATE
    chirp_network
    chirp_common
    chirp_protos
    ${absl_pkg_LIBRARIES}
    Threads::Threads
)

if(CHIRP_ASR_WHISPER)
    find_path(WHISPER_INCLUDE_DIR whisper.h REQUIRED)
    find_library(WHISPER_LIBRARY NAMES whisper REQUIRED)
    target_include_directories(chirp_voice PRIVATE ${WHISPER_INCLUDE_DIR})
    target_link_libraries(chirp_voice PRIVATE ${WHISPER_LIBRARY})
    target_compile_definitions(chirp_voice PRIVATE CHIRP_ASR_WHISPER=1)
endif()

target_include_directories(chirp_voice PRIVATE src ${CMAKE_SOURCE_DIR}/services/shared/chat/src)
