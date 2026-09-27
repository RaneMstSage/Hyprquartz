cmake_minimum_required(VERSION 3.19)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VER_RAW)
string(STRIP ${VER_RAW} HYPRGRAPHICS_VERSION)
add_compile_definitions(HYPRGRAPHICS_VERSION="${HYPRGRAPHICS_VERSION}")

project(
  hyprgraphics
  VERSION ${HYPRGRAPHICS_VERSION}
  DESCRIPTION
    "Small C++ library for graphics / resource utilities used across the Hypr* ecosystem"
)

include(CTest)
include(GNUInstallDirs)

set(PREFIX ${CMAKE_INSTALL_PREFIX})
set(INCLUDE ${CMAKE_INSTALL_FULL_INCLUDEDIR})
set(LIBDIR ${CMAKE_INSTALL_FULL_LIBDIR})

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
# -Wno-missing-braces for clang
add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  -Wno-unused-parameter
  -Wno-unused-value
  -Wno-missing-field-initializers
  -Wno-missing-braces)

if(CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES DEBUG)
  message(STATUS "Configuring hyprgraphics in Debug")
  add_compile_definitions(HYPRGRAPHICS_DEBUG)
else()
  add_compile_options(-O3)
  message(STATUS "Configuring hyprgraphics in Release")
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_definitions(__cpp_concepts=202002L)
endif()

file(GLOB_RECURSE SRCFILES CONFIGURE_DEPENDS "src/*.cpp" "include/*.hpp")
file(GLOB_RECURSE PUBLIC_HEADERS CONFIGURE_DEPENDS "include/*.hpp")

if(APPLE)
else()
  set(GLES_VERSION "GLES3")
  find_package(OpenGL REQUIRED COMPONENTS ${GLES_VERSION})
endif()

find_package(PkgConfig REQUIRED)
if(TARGET hyprutils)
  if(APPLE)
    pkg_check_modules(
      deps
      REQUIRED
      IMPORTED_TARGET
      pixman-1
      cairo
      pangocairo
      libjpeg
      libwebp
      libmagic
      libpng
      librsvg-2.0)
  else()
    pkg_check_modules(
      deps
      REQUIRED
      IMPORTED_TARGET
      libdrm
      pixman-1
      cairo
      pangocairo
      libjpeg
      libwebp
      libmagic
      libpng
      librsvg-2.0)
  endif()
else()
  if(APPLE)
    pkg_check_modules(
      deps
      REQUIRED
      IMPORTED_TARGET
      pixman-1
      cairo
      pangocairo
      hyprutils
      libjpeg
      libwebp
      libmagic
      libpng
      librsvg-2.0)
  else()
    pkg_check_modules(
      deps
      REQUIRED
      IMPORTED_TARGET
      libdrm
      pixman-1
      cairo
      pangocairo
      hyprutils
      libjpeg
      libwebp
      libmagic
      libpng
      librsvg-2.0)
  endif()
endif()

pkg_check_modules(
  JXL
  IMPORTED_TARGET
  libjxl
  libjxl_cms
  libjxl_threads
)
if(NOT JXL_FOUND)
  file(GLOB_RECURSE JPEGXLFILES CONFIGURE_DEPENDS "src/*JpegXL.cpp")
  list(REMOVE_ITEM SRCFILES ${JPEGXLFILES})
else()
  add_compile_definitions(JXL_FOUND)
endif()

pkg_check_modules(
  HEIF
  IMPORTED_TARGET
  libheif
)

if(NOT HEIF_FOUND)
  file(GLOB_RECURSE HEIFFILES CONFIGURE_DEPENDS "src/*Avif.cpp")
  list(REMOVE_ITEM SRCFILES ${HEIFFILES})
else()
  add_compile_definitions(HEIF_FOUND)
endif()

# Build the pkg-config Requires.private list, only advertising optional deps
# that were actually found and linked, so downstream pkg-config resolution
# does not fail on systems where e.g. libheif / libjxl are absent.
set(HYPRGRAPHICS_PRIVATE_REQUIRES "libdrm pixman-1 libpng libjpeg libwebp librsvg-2.0 libmagic")
if(JXL_FOUND)
  string(APPEND HYPRGRAPHICS_PRIVATE_REQUIRES " libjxl libjxl_cms libjxl_threads")
endif()
if(HEIF_FOUND)
  string(APPEND HYPRGRAPHICS_PRIVATE_REQUIRES " libheif")
endif()

configure_file(hyprgraphics.pc.in hyprgraphics.pc @ONLY)

add_library(hyprgraphics SHARED ${SRCFILES})
target_include_directories(
  hyprgraphics
  PUBLIC "./include"
  PRIVATE "./src")
if(APPLE)
  target_include_directories(
    hyprgraphics
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../compat"
           "${CMAKE_CURRENT_SOURCE_DIR}/../../compat/libdrm")
endif()
set_target_properties(hyprgraphics PROPERTIES VERSION ${HYPRGRAPHICS_VERSION}
                                              SOVERSION 4)
if(TARGET hyprutils)
  target_link_libraries(hyprgraphics PkgConfig::deps hyprutils)
else()
  target_link_libraries(hyprgraphics PkgConfig::deps)
endif()
if(JXL_FOUND)
  target_link_libraries(hyprgraphics PkgConfig::JXL)
endif()

if(HEIF_FOUND)
  target_link_libraries(hyprgraphics PkgConfig::HEIF)
endif()

# tests
if(NOT TARGET tests)
  add_custom_target(tests)
endif()

add_executable(hyprgraphics_image "tests/image.cpp")
if(TARGET hyprutils)
  target_link_libraries(hyprgraphics_image PRIVATE hyprgraphics PkgConfig::deps hyprutils)
else()
  target_link_libraries(hyprgraphics_image PRIVATE hyprgraphics PkgConfig::deps)
endif()
add_test(
  NAME "Image"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests
  COMMAND hyprgraphics_image "image")
add_dependencies(tests hyprgraphics_image)
add_executable(hyprgraphics_arg "tests/arg.cpp")
if(TARGET hyprutils)
  target_link_libraries(hyprgraphics_arg PRIVATE hyprgraphics PkgConfig::deps hyprutils)
else()
  target_link_libraries(hyprgraphics_arg PRIVATE hyprgraphics PkgConfig::deps)
endif()
add_test(
  NAME "ARG"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests
  COMMAND hyprgraphics_arg "image")
add_dependencies(tests hyprgraphics_arg)

# Installation
install(TARGETS hyprgraphics)
install(DIRECTORY "include/hyprgraphics"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/hyprgraphics.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
