cmake_minimum_required(VERSION 3.19)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VER_RAW)
string(STRIP ${VER_RAW} AQUAMARINE_VERSION)

add_compile_definitions(AQUAMARINE_VERSION="${AQUAMARINE_VERSION}")

project(
  aquamarine
  VERSION ${AQUAMARINE_VERSION}
  DESCRIPTION "A very light linux rendering backend library")

include(CTest)
include(CheckIncludeFile)
include(GNUInstallDirs)

set(PREFIX ${CMAKE_INSTALL_PREFIX})
set(INCLUDE ${CMAKE_INSTALL_FULL_INCLUDEDIR})
set(LIBDIR ${CMAKE_INSTALL_FULL_LIBDIR})

find_package(PkgConfig REQUIRED)
if(APPLE)
else()
  find_package(OpenGL REQUIRED COMPONENTS "GLES3")
endif()
find_package(Threads REQUIRED)
if(TARGET hyprwayland-scanner)
else()
  find_package(hyprwayland-scanner 0.4.0 REQUIRED)
endif()
if(TARGET hyprutils)
  if(APPLE)
    pkg_check_modules(
      deps
      REQUIRED
      IMPORTED_TARGET
      pixman-1
      hwdata)
  else()
    pkg_check_modules(
      deps
      REQUIRED
      IMPORTED_TARGET
      libseat>=0.8.0
      libinput>=1.26.0
      wayland-client
      wayland-protocols
      pixman-1
      libdrm
      gbm
      libudev
      libdisplay-info
      hwdata)
  endif()
else()
  if(APPLE)
    pkg_check_modules(
      deps
      REQUIRED
      IMPORTED_TARGET
      hyprutils>=0.8.0
      pixman-1
      hwdata)
  else()
    pkg_check_modules(
      deps
      REQUIRED
      IMPORTED_TARGET
      libseat>=0.8.0
      libinput>=1.26.0
      wayland-client
      wayland-protocols
      hyprutils>=0.8.0
      pixman-1
      libdrm
      gbm
      libudev
      libdisplay-info
      hwdata)
  endif()
endif()

configure_file(aquamarine.pc.in aquamarine.pc @ONLY)

set(CMAKE_CXX_STANDARD 23)
add_compile_options(
  -Wall
  -Wextra
  -Wno-unused-parameter
  -Wno-unused-value
  -Wno-missing-field-initializers
  -Wpedantic)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

if(CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES DEBUG)
  message(STATUS "Configuring aquamarine in Debug")
  add_compile_definitions(AQUAMARINE_DEBUG)
else()
  add_compile_options(-O3)
  message(STATUS "Configuring aquamarine in Release")
endif()

file(GLOB_RECURSE SRCFILES CONFIGURE_DEPENDS "src/*.cpp" "include/*.hpp")
file(GLOB_RECURSE PUBLIC_HEADERS CONFIGURE_DEPENDS "include/*.hpp")
if(APPLE)
  list(
    FILTER
    SRCFILES
    EXCLUDE
    REGEX
    "/src/backend/drm/(DRM|Props|Renderer|AsyncCommit)\\.cpp$|/src/backend/drm/impl/|/src/backend/(Session|Wayland)\\.cpp$|/src/allocator/(GBM|DRMDumb)\\.cpp$"
  )
  list(APPEND SRCFILES "${CMAKE_CURRENT_SOURCE_DIR}/../../compat/libdrm-src/xf86drm-formats.c")
endif()

add_library(aquamarine SHARED ${SRCFILES})
if(deps_libinput_VERSION VERSION_GREATER_EQUAL "1.30")
  target_compile_definitions(aquamarine PRIVATE AQUAMARINE_HAS_LIBINPUT_PLUGINS)
endif()
target_include_directories(
  aquamarine
  PUBLIC "./include"
  PRIVATE "./src" "./src/include" "./protocols" "${CMAKE_CURRENT_BINARY_DIR}")
if(APPLE)
  target_include_directories(
    aquamarine SYSTEM
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../compat"
           "${CMAKE_CURRENT_SOURCE_DIR}/../../compat/libdrm")
endif()
set_target_properties(aquamarine PROPERTIES VERSION ${AQUAMARINE_VERSION}
                                            SOVERSION 14)
if(TARGET hyprutils)
  if(APPLE)
    target_link_libraries(aquamarine PkgConfig::deps Threads::Threads hyprutils "-framework IOSurface" "-framework CoreFoundation")
  else()
    target_link_libraries(aquamarine OpenGL::EGL OpenGL::OpenGL PkgConfig::deps Threads::Threads hyprutils)
  endif()
else()
  if(APPLE)
    target_link_libraries(aquamarine PkgConfig::deps Threads::Threads "-framework IOSurface" "-framework CoreFoundation")
  else()
    target_link_libraries(aquamarine OpenGL::EGL OpenGL::OpenGL PkgConfig::deps Threads::Threads)
  endif()
endif()

check_include_file("sys/timerfd.h" HAS_TIMERFD)
pkg_check_modules(epoll IMPORTED_TARGET epoll-shim)
if(NOT HAS_TIMERFD AND epoll_FOUND)
  target_link_libraries(aquamarine PkgConfig::epoll)
endif()

# Protocols
if(APPLE)
else()
  pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
  message(STATUS "Found wayland-protocols at ${WAYLAND_PROTOCOLS_DIR}")
  pkg_get_variable(WAYLAND_SCANNER_PKGDATA_DIR wayland-scanner pkgdatadir)
  message(STATUS "Found wayland-scanner pkgdatadir at ${WAYLAND_SCANNER_PKGDATA_DIR}")
endif()

function(protocolNew protoPath protoName external)
  if(external)
    set(path ${CMAKE_CURRENT_SOURCE_DIR}/${protoPath})
  else()
    set(path ${WAYLAND_PROTOCOLS_DIR}/${protoPath})
  endif()
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${protoName}.cpp
           ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${protoName}.hpp
    COMMAND hyprwayland-scanner --client ${path}/${protoName}.xml
            ${CMAKE_CURRENT_SOURCE_DIR}/protocols/
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  target_sources(aquamarine PRIVATE protocols/${protoName}.cpp
                                    protocols/${protoName}.hpp)
endfunction()
function(protocolWayland)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wayland.cpp
           ${CMAKE_CURRENT_SOURCE_DIR}/protocols/wayland.hpp
    COMMAND hyprwayland-scanner --wayland-enums --client
            ${WAYLAND_SCANNER_PKGDATA_DIR}/wayland.xml ${CMAKE_CURRENT_SOURCE_DIR}/protocols/
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  target_sources(aquamarine PRIVATE protocols/wayland.cpp protocols/wayland.hpp)
endfunction()

if(APPLE)
else()
  protocolwayland()

  protocolnew("stable/xdg-shell" "xdg-shell" false)
  protocolnew("stable/linux-dmabuf" "linux-dmabuf-v1" false)
endif()

# Generate hwdata info
pkg_get_variable(HWDATA_DIR hwdata pkgdatadir)
message(
  STATUS "Running ${CMAKE_CURRENT_SOURCE_DIR}/data/hwdata.sh < ${HWDATA_DIR}/pnp.ids")
execute_process(
  COMMAND /bin/sh -c
          "${CMAKE_CURRENT_SOURCE_DIR}/data/hwdata.sh < ${HWDATA_DIR}/pnp.ids"
  RESULT_VARIABLE HWDATA_PNP_RESULT
  OUTPUT_VARIABLE HWDATA_PNP_IDS ENCODING UTF8)

if(NOT HWDATA_PNP_RESULT MATCHES 0)
  message(WARNING "hwdata gathering pnps failed")
endif()

configure_file(data/hwdata.hpp.in hwdata.hpp @ONLY)

# tests
if(NOT TARGET tests)
  add_custom_target(tests)
endif()

if(APPLE)
else()
  add_executable(simpleWindow "tests/SimpleWindow.cpp")
  if(TARGET hyprutils)
    target_link_libraries(simpleWindow PRIVATE PkgConfig::deps aquamarine hyprutils)
  else()
    target_link_libraries(simpleWindow PRIVATE PkgConfig::deps aquamarine)
  endif()
  add_test(
    NAME "simpleWindow"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests
    COMMAND simpleWindow "simpleWindow")
  add_dependencies(tests simpleWindow)
endif()

add_executable(attachments "tests/Attachments.cpp")
if(TARGET hyprutils)
  target_link_libraries(attachments PRIVATE PkgConfig::deps aquamarine hyprutils)
else()
  target_link_libraries(attachments PRIVATE PkgConfig::deps aquamarine)
endif()
add_test(
  NAME "attachments"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests
  COMMAND attachments "attachments")
add_dependencies(tests attachments)

add_executable(output "tests/Output.cpp")
target_include_directories(output PRIVATE "./src/include")
if(TARGET hyprutils)
  target_link_libraries(output PRIVATE PkgConfig::deps aquamarine hyprutils)
else()
  target_link_libraries(output PRIVATE PkgConfig::deps aquamarine)
endif()
add_test(
  NAME "output"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests
  COMMAND output "output")
add_dependencies(tests output)

add_executable(commitThread "tests/CommitThread.cpp")
target_include_directories(commitThread PRIVATE "./src/backend/drm")
if(TARGET hyprutils)
  target_link_libraries(commitThread PRIVATE PkgConfig::deps aquamarine hyprutils)
else()
  target_link_libraries(commitThread PRIVATE PkgConfig::deps aquamarine)
endif()
add_test(
  NAME "commitThread"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests
  COMMAND commitThread "commitThread")
add_dependencies(tests commitThread)

# Installation
install(TARGETS aquamarine)
install(DIRECTORY "include/aquamarine" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/aquamarine.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
