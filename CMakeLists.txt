cmake_minimum_required(VERSION 3.21)
project(KwrtStudio VERSION 1.3.3 LANGUAGES CXX RC)
if(NOT MSVC OR NOT WIN32)
  message(FATAL_ERROR "Build with Visual Studio on Windows x64.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_executable(KwrtStudio WIN32
  src/main.cpp src/common.cpp src/formats.cpp src/converter.cpp
  src/adapters.cpp src/adapter_ax6000.cpp src/selftest.cpp src/customization.cpp src/network_mode.cpp src/packages.cpp src/package_view.cpp src/app.rc)
target_include_directories(KwrtStudio PRIVATE vendor src)
target_compile_definitions(KwrtStudio PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(KwrtStudio PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/W4>)
target_link_options(KwrtStudio PRIVATE /MANIFEST:NO /DYNAMICBASE /NXCOMPAT)
target_link_libraries(KwrtStudio PRIVATE user32 gdi32 bcrypt comctl32 comdlg32 ole32 shell32 uxtheme uuid)
add_custom_command(TARGET KwrtStudio POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/tools" "$<TARGET_FILE_DIR:KwrtStudio>/tools")
enable_testing()
add_test(NAME native_selftest COMMAND KwrtStudio --self-test)

install(TARGETS KwrtStudio RUNTIME DESTINATION .)
install(DIRECTORY tools licenses src vendor docs third-party-source DESTINATION .)
install(FILES README.md CHANGELOG.md LICENSE THIRD-PARTY.md CMakeLists.txt profile.example.json DESTINATION .)
