project(texcompress)
cmake_minimum_required(VERSION 2.8)

include("${SRC_DIR}/build_options.cmake")

require_pthreads()

include_directories(
    "${SRC_DIR}/extlib/include"
    )

set(SRC_LIST
    ${SRC_LIST}
    texcompress.c
    texcompress.h
    texcompress_cpal.c
    texcompress_cpal.h
    texcompress_etc.c
    texcompress_etc.h
    texcompress_etc_tmp.h
    texcompress_fxt1.c
    texcompress_fxt1.h
    texcompress_rgtc.c
    texcompress_rgtc.h
    texcompress_rgtc_tmp.h
    texcompress_s3tc.c
    texcompress_s3tc.h
    glheader.h
    imports.h
    colormac.h
    context.h
    formats.h
    mtypes.h
    macros.h
    compiler.h
    texstore.h
    errors.h
    math/m_matrix.h
    c99_compat.h
    glapi/c11/threads.h
    glapi/c11/c99_compat.h
    glapi/c99_compat.h
    glapi/glapi.h
    glapi/u_compiler.h
    glapi/u_thread.h
    main/config.h
    main/formats.h
    main/glheader.h
    main/simple_list.h
)

if (NOT MSVC)
add_compiler_flag("-fPIC")
endif()

add_library(${PROJECT_NAME} ${SRC_LIST})

build_options_finalize()

