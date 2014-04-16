PROJECT(VoglChroot)
cmake_minimum_required(VERSION 2.8)

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vogl/src/build_options.cmake")
    message(FATAL_ERROR "ERROR: vogl source directory not found. Please clone vogl source project next to samples directory.")
endif()

add_subdirectory(vogl)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vogl/src")

add_subdirectory(samples/gltests)
add_subdirectory(samples/OGLSamples_GTruc)
add_subdirectory(samples/OGLSuperBible)

