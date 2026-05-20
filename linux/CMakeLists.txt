cmake_minimum_required(VERSION 3.13)

set(PROJECT_NAME "mobile_scanner")
project(${PROJECT_NAME} LANGUAGES CXX)

set(PLUGIN_NAME "mobile_scanner_plugin")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO_UNIX REQUIRED IMPORTED_TARGET gio-unix-2.0)

option(
  MOBILE_SCANNER_USE_SYSTEM_ZXING
  "Use a system-installed ZXing package instead of building the bundled source"
  OFF
)

set(ZXING_READERS ON CACHE BOOL "" FORCE)
set(ZXING_WRITERS OFF CACHE STRING "" FORCE)
set(ZXING_USE_BUNDLED_ZINT OFF CACHE BOOL "" FORCE)
set(ZXING_C_API OFF CACHE BOOL "" FORCE)
set(ZXING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZXING_BLACKBOX_TESTS OFF CACHE BOOL "" FORCE)
set(ZXING_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(ZXING_PYTHON_MODULE OFF CACHE BOOL "" FORCE)
set(ZXING_DEPENDENCIES AUTO CACHE STRING "" FORCE)

set(MOBILE_SCANNER_ZXING_INCLUDE_DIRS "")
if(MOBILE_SCANNER_USE_SYSTEM_ZXING)
  find_package(ZXing CONFIG REQUIRED)

  get_target_property(_zxing_include_dirs ZXing::ZXing INTERFACE_INCLUDE_DIRECTORIES)
  if(_zxing_include_dirs)
    foreach(_zxing_include_dir IN LISTS _zxing_include_dirs)
      if(EXISTS "${_zxing_include_dir}/ZXing")
        list(APPEND MOBILE_SCANNER_ZXING_INCLUDE_DIRS "${_zxing_include_dir}/ZXing")
      endif()
    endforeach()
  endif()
else()
  include(FetchContent)
  FetchContent_Declare(
    zxing-cpp
    GIT_REPOSITORY https://github.com/zxing-cpp/zxing-cpp.git
    # v2.3.0. Keep this pinned to the full commit SHA so release builds do not
    # depend on a mutable tag reference.
    GIT_TAG d6068bcebeb8fd9f0d35a99b00d202be86a14dbe
  )
  FetchContent_MakeAvailable(zxing-cpp)
  target_compile_definitions(ZXing PRIVATE NDEBUG)
endif()

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
target_include_directories(${PLUGIN_NAME} PRIVATE
  ${MOBILE_SCANNER_ZXING_INCLUDE_DIRS})

target_link_libraries(${PLUGIN_NAME} PRIVATE
  flutter
  PkgConfig::GTK
  PkgConfig::GIO_UNIX
  ZXing::ZXing
  dl)

set(mobile_scanner_bundled_libraries
  ""
  PARENT_SCOPE
)
