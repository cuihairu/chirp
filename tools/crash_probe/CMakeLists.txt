# Crashpad 接入验收探针:初始化 + 故意崩溃开关(见 docs/design-notes/CRASH_COLLECTION.md)。
add_executable(crash_probe crash_probe.cc)
target_compile_features(crash_probe PRIVATE cxx_std_23)
target_link_libraries(crash_probe PRIVATE chirp_common)
target_include_directories(crash_probe PRIVATE ${CMAKE_SOURCE_DIR}/libs)
