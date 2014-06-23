/**************************************************************************
 *
 * Copyright 2014 LunarG, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * This file was originally authored by Ian Elliott (ian@lunarg.com).
 *
 **************************************************************************/

#if defined(_WIN32)
#include <windows.h>
#endif

#include <algorithm> // For std::max()
#include <assert.h>

#include <GL/gl.h>
#include <GL/glext.h>
#include "pxfmt.h"
#include <cmath>  // For std::floor()

#ifdef DECOMPRESS_DEBUG
#include <stdio.h>
#endif // DECOMPRESS_DEBUG

#include "pxfmt_internal.h"


static bool external_dxt_library_initialized = false;
static bool external_dxt_functions_loaded = false;

typedef void (*ext_dxt_decomp_func)(uint32 src_row_stride,
                                    const uint8 *pSrc, int32 x, GLint y,
                                    void *pDst);

static ext_dxt_decomp_func ext_decomp_rgb_dxt1 = NULL;
static ext_dxt_decomp_func ext_decomp_rgba_dxt1 = NULL;
static ext_dxt_decomp_func ext_decomp_rgba_dxt3 = NULL;
static ext_dxt_decomp_func ext_decomp_rgba_dxt5 = NULL;

static void *dxtlibhandle = NULL;

#if defined(_WIN32)
#else
#include <dlfcn.h>
#define HAVE_DLOPEN 1
#endif




typedef void (*GenericFunc)(void);

/**
 * Wrapper for dlopen().
 * Note that 'flags' isn't used at this time.
 */
static inline void *
_mesa_dlopen(const char *libname, int flags)
{
#if defined(__blrts)
   return NULL;
#elif defined(HAVE_DLOPEN)
   flags = RTLD_LAZY | RTLD_GLOBAL; /* Overriding flags at this time */
   return dlopen(libname, flags);
#elif defined(__MINGW32__)
   return LoadLibraryA(libname);
#else
   return NULL;
#endif
}

/**
 * Wrapper for dlsym() that does a cast to a generic function type,
 * rather than a void *.  This reduces the number of warnings that are
 * generated.
 */
static inline GenericFunc
_mesa_dlsym(void *handle, const char *fname)
{
   union {
      void *v;
      GenericFunc f;
   } u;
#if defined(__blrts)
   u.v = NULL;
#elif defined(__DJGPP__)
   /* need '_' prefix on symbol names */
   char fname2[1000];
   fname2[0] = '_';
   strncpy(fname2 + 1, fname, 998);
   fname2[999] = 0;
   u.v = dlsym(handle, fname2);
#elif defined(HAVE_DLOPEN)
   u.v = dlsym(handle, fname);
#elif defined(__MINGW32__)
   u.v = (void *) GetProcAddress(handle, fname);
#else
   u.v = NULL;
#endif
   return u.f;
}

/**
 * Wrapper for dlclose().
 */
static inline void
_mesa_dlclose(void *handle)
{
#if defined(__blrts)
   (void) handle;
#elif defined(HAVE_DLOPEN)
   dlclose(handle);
#elif defined(__MINGW32__)
   FreeLibrary(handle);
#else
   (void) handle;
#endif
}




#if defined(_WIN32) || defined(WIN32)
#define DXTN_LIBNAME "dxtn.dll"
#define RTLD_LAZY 0
#define RTLD_GLOBAL 0
#elif defined(__DJGPP__)
#define DXTN_LIBNAME "dxtn.dxe"
#else
#define DXTN_LIBNAME "libtxc_dxtn.so"
#endif







void
init_external_dxt_library()
{
    external_dxt_library_initialized = true;
    if (!dxtlibhandle)
    {
#if 1
        dxtlibhandle = _mesa_dlopen(DXTN_LIBNAME, 0);
        if (!dxtlibhandle)
        {
//            _mesa_warning(ctx, "couldn't open " DXTN_LIBNAME ", software DXTn "
//                          "compression/decompression unavailable");
        }
        else
        {
            ext_decomp_rgb_dxt1 = (ext_dxt_decomp_func)
                _mesa_dlsym(dxtlibhandle, "fetch_2d_texel_rgb_dxt1");
            ext_decomp_rgba_dxt1 = (ext_dxt_decomp_func)
                _mesa_dlsym(dxtlibhandle, "fetch_2d_texel_rgba_dxt1");
            ext_decomp_rgba_dxt3 = (ext_dxt_decomp_func)
                _mesa_dlsym(dxtlibhandle, "fetch_2d_texel_rgba_dxt3");
            ext_decomp_rgba_dxt5 = (ext_dxt_decomp_func)
                _mesa_dlsym(dxtlibhandle, "fetch_2d_texel_rgba_dxt5");

            if (!ext_decomp_rgb_dxt1 ||
                !ext_decomp_rgba_dxt1 ||
                !ext_decomp_rgba_dxt3 ||
                !ext_decomp_rgba_dxt5)
            {
//                _mesa_warning(ctx, "couldn't reference all symbols in "
//                              DXTN_LIBNAME ", software DXTn compression/decompression "
//                              "unavailable");
#endif
                ext_decomp_rgb_dxt1 = NULL;
                ext_decomp_rgba_dxt1 = NULL;
                ext_decomp_rgba_dxt3 = NULL;
                ext_decomp_rgba_dxt5 = NULL;
#if 1
                _mesa_dlclose(dxtlibhandle);
            }
            else
            {
                external_dxt_functions_loaded = true;
            }
        }
#endif
    }
}


void decompress_dxt(float *intermediate, const void *pSrc,
                    uint32 row_stride, int x, int y,
                    const pxfmt_sized_format fmt)
{
    if (!external_dxt_library_initialized)
    {
        init_external_dxt_library();
    }
    if (external_dxt_functions_loaded)
    {
        uint8 tex[4];
        switch (fmt)
        {
        case PXFMT_COMPRESSED_RGB_DXT1:
            ext_decomp_rgb_dxt1(row_stride, (const uint8 *) pSrc, x, y, tex);
            break;
        case PXFMT_COMPRESSED_RGBA_DXT1:
            ext_decomp_rgba_dxt1(row_stride, (const uint8 *) pSrc, x, y, tex);
            break;
        case PXFMT_COMPRESSED_RGBA_DXT3:
            ext_decomp_rgba_dxt3(row_stride, (const uint8 *) pSrc, x, y, tex);
            break;
        case PXFMT_COMPRESSED_RGBA_DXT5:
            ext_decomp_rgba_dxt5(row_stride, (const uint8 *) pSrc, x, y, tex);
            break;
        default:
            break;
        }
#ifdef DECOMPRESS_DEBUG
        printf("decompress_dxt(stride=%d, x=%d, y=%d) = {%d, %d, %d, %d}\n",
               row_stride, x, y, tex[0], tex[1], tex[2], tex[3]);
#endif // DECOMPRESS_DEBUG
#define UBYTE_TO_FLOAT(u) ((float) (u) / (float) 255.0)
        intermediate[0] = UBYTE_TO_FLOAT(tex[0]);
        intermediate[1] = UBYTE_TO_FLOAT(tex[1]);
        intermediate[2] = UBYTE_TO_FLOAT(tex[2]);
        intermediate[3] = UBYTE_TO_FLOAT(tex[3]);
#ifdef DECOMPRESS_DEBUG
        printf("  intermediate[] = {%f, %f, %f, %f}\n",
               intermediate[0], intermediate[1],
               intermediate[2], intermediate[3]);
#endif // DECOMPRESS_DEBUG
    }
}
