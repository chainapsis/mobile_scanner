# The Flutter tooling requires CMake 3.14 or later on Windows.
cmake_minimum_required(VERSION 3.14)

set(PROJECT_NAME "mobile_scanner")
project(${PROJECT_NAME} LANGUAGES CXX)

cmake_policy(VERSION 3.14...3.25)

set(PLUGIN_NAME "mobile_scanner_plugin")

include(FetchContent)

set(ZXING_READERS ON CACHE BOOL "" FORCE)
set(ZXING_WRITERS OFF CACHE STRING "" FORCE)
set(ZXING_USE_BUNDLED_ZINT OFF CACHE BOOL "" FORCE)
set(ZXING_C_API OFF CACHE BOOL "" FORCE)
set(ZXING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZXING_BLACKBOX_TESTS OFF CACHE BOOL "" FORCE)
set(ZXING_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(ZXING_PYTHON_MODULE OFF CACHE BOOL "" FORCE)
set(ZXING_DEPENDENCIES AUTO CACHE STRING "" FORCE)
set(CMAKE_INSTALL_BINDIR "${CMAKE_CURRENT_BINARY_DIR}/zxing-install/bin")
set(CMAKE_INSTALL_LIBDIR "${CMAKE_CURRENT_BINARY_DIR}/zxing-install/lib")
set(CMAKE_INSTALL_INCLUDEDIR "${CMAKE_CURRENT_BINARY_DIR}/zxing-install/include")

FetchContent_Declare(
  zxing-cpp
  GIT_REPOSITORY https://github.com/zxing-cpp/zxing-cpp.git
  GIT_TAG v2.3.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(zxing-cpp)

list(APPEND PLUGIN_SOURCES
  "mobile_scanner_plugin.cpp"
  "mobile_scanner_plugin.h"
)

add_library(${PLUGIN_NAME} SHARED
  "include/mobile_scanner/mobile_scanner_plugin_c_api.h"
  "mobile_scanner_plugin_c_api.cpp"
  ${PLUGIN_SOURCES}
)

apply_standard_settings(${PLUGIN_NAME})

set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE
  FLUTTER_PLUGIN_IMPL
  NOMINMAX
  WIN32_LEAN_AND_MEAN)

target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(${PLUGIN_NAME} PRIVATE
  flutter
  flutter_wrapper_plugin
  ZXing::ZXing
  mf.lib
  mfplat.lib
  mfreadwrite.lib
  mfuuid.lib
  ole32.lib)

set(mobile_scanner_bundled_libraries
  ""
  PARENT_SCOPE
)
