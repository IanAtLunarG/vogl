project(test-benchmark_sphere)
cmake_minimum_required(VERSION 2.8)

include("${SRC_DIR}/build_options.cmake")

find_package(X11 REQUIRED)

aux_source_directory(. SRC_LIST)

add_definitions("-Wno-unused-variable")

add_executable(${PROJECT_NAME} ${SRC_LIST})

target_link_libraries(${PROJECT_NAME}
    ${OPENGL_LIBRARIES}
    ${X11_X11_LIB}
    )

# copy our stone.tga file to pyramid_ch8_stone.tga, etc.
set(outfile "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/mars.bmp")
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/mars.bmp" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/mars.bmp"
    MAIN_DEPENDENCY mars.bmp)


build_options_finalize()

