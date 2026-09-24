cmake_minimum_required(VERSION 3.30)

# Get version
file(READ "${CMAKE_SOURCE_DIR}/VERSION" VER_RAW)
string(STRIP ${VER_RAW} VER)

project(
        Hyprquartz
        DESCRIPTION "A Hyprland port to the macOS Quartz Compositor"
        VERSION ${VER}
        LANGUAGES CXX)

if(NOT APPLE)
    message(FATAL_ERROR "Hyprquartz only builds on macOS")
endif()

add_subdirectory(subprojects/hyprutils)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions(HYPRQUARTZ_VERSION="${PROJECT_VERSION}")

add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wno-keyword-macro
        -Wno-unused-result
        -Wno-unused-parameter
        -Wno-unused-value
        -Wno-missing-field-initializers
        -Wno-narrowing
        -Wno-pointer-arith
        -frtti
        -fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=)

add_executable(hyprquartz src/main.cpp)

target_include_directories(hyprquartz PRIVATE ${CMAKE_SOURCE_DIR}/compat)
target_link_libraries(hyprquartz PRIVATE hyprutils)