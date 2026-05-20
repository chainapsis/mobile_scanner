cmake_minimum_required(VERSION 3.13)

set(PROJECT_NAME "mobile_scanner")
project(${PROJECT_NAME} LANGUAGES CXX)

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

FetchContent_Declare(
  zxing-cpp
  GIT_REPOSITORY https://github.com/zxing-cpp/zxing-cpp.git
  GIT_TAG v2.3.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(zxing-cpp)
target_compile_definitions(ZXing PRIVATE NDEBUG)

list(APPEND PLUGIN_SOURCES
  "mobile_scanner_plugin.cc"
)

add_library(${PLUGIN_NAME} SHARED
  "include/mobile_scanner/mobile_scanner_plugin.h"
  ${PLUGIN_SOURCES}
)

apply_standard_settings(${PLUGIN_NAME})

set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(${PLUGIN_NAME} PRIVATE
  flutter
  PkgConfig::GTK
  ZXing::ZXing
  dl)

set(mobile_scanner_bundled_libraries
  ""
  PARENT_SCOPE
)
