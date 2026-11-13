cmake_minimum_required(VERSION 3.18)
add_executable(${BINARY_NAME} WIN32 "main.cpp" "native.cpp" "Runner.rc" "${FLUTTER_MANAGED_DIR}/generated_plugin_registrant.cc" "runner.exe.manifest")
apply_standard_settings(${BINARY_NAME})
target_link_libraries(${BINARY_NAME} PRIVATE flutter flutter_wrapper_app ole32 shell32 shlwapi windowscodecs dwrite comctl32)
target_include_directories(${BINARY_NAME} PRIVATE "${CMAKE_SOURCE_DIR}")
add_dependencies(${BINARY_NAME} flutter_assemble)

target_link_options(${BINARY_NAME} PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/runner.exe.manifest")
