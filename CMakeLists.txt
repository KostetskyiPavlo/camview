cmake_minimum_required(VERSION 3.22)

project(camview
    VERSION 0.1.0
    DESCRIPTION "Low-latency USB camera viewer for Linux (V4L2 + SDL2)"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Project options
# ---------------------------------------------------------------------------
option(CAMVIEW_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CAMVIEW_SANITIZER "" CACHE STRING
    "Sanitizer to enable: address, address,undefined, thread, undefined or empty")
set(CAMVIEW_ALLOWED_SANITIZERS "" address "address,undefined" thread undefined)
set_property(CACHE CAMVIEW_SANITIZER PROPERTY STRINGS ${CAMVIEW_ALLOWED_SANITIZERS})

if(NOT CAMVIEW_SANITIZER IN_LIST CAMVIEW_ALLOWED_SANITIZERS)
    message(FATAL_ERROR
        "Unknown CAMVIEW_SANITIZER='${CAMVIEW_SANITIZER}'. "
        "Allowed: address, address,undefined, thread, undefined")
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)
find_package(Threads REQUIRED)

message(STATUS "SDL2 version: ${SDL2_VERSION}")
message(STATUS "libturbojpeg version: ${TURBOJPEG_VERSION}")

# ---------------------------------------------------------------------------
# Common compile settings shared by every camview target
# ---------------------------------------------------------------------------
add_library(camview_options INTERFACE)

target_compile_options(camview_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Woverloaded-virtual)

if(CAMVIEW_WARNINGS_AS_ERRORS)
    target_compile_options(camview_options INTERFACE -Werror)
endif()

if(CAMVIEW_SANITIZER)
    target_compile_options(camview_options INTERFACE
        -fsanitize=${CAMVIEW_SANITIZER}
        -fno-omit-frame-pointer)
    target_link_options(camview_options INTERFACE
        -fsanitize=${CAMVIEW_SANITIZER})
endif()

# ---------------------------------------------------------------------------
# Core library: everything except main(). Tests link against it too.
# ---------------------------------------------------------------------------
add_library(camview_core STATIC
    src/app/App.cpp
    src/app/CliParser.cpp
    src/camera/CameraMode.cpp
    src/camera/TestPatternSource.cpp
    src/camera/V4l2Camera.cpp
    src/decode/MjpegDecoder.cpp
    src/frame/PixelFormat.cpp
    src/render/Renderer.cpp
    src/util/Log.cpp)

target_include_directories(camview_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(camview_core
    PUBLIC Threads::Threads
    PRIVATE camview_options PkgConfig::SDL2 PkgConfig::TURBOJPEG)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(camview src/main.cpp)

target_link_libraries(camview PRIVATE camview_core camview_options)
target_compile_definitions(camview PRIVATE CAMVIEW_VERSION="${PROJECT_VERSION}")