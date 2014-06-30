/**************************************************************************
 *
 * Copyright 2014 LunarG, Inc.  All Rights Reserved.
 * Copyright (C) 1999-2007  Brian Paul   All Rights Reserved.
 * Copyright (C) 2008  VMware, Inc.  All Rights Reserved.
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

#ifndef PXFMT_INTERNAL_H
#define PXFMT_INTERNAL_H

typedef unsigned char uint8;
typedef signed char int8;
typedef unsigned short uint16;
typedef signed short int16;
typedef unsigned int uint32;
typedef signed int int32;


void decompress_dxt(float *intermediate, const void *pSrc,
                    uint32 row_stride, int x, int y,
                    const pxfmt_sized_format fmt);

void decompress_fxt1(float *intermediate, const void *pSrc,
                     uint32 row_stride, int x, int y,
                     const pxfmt_sized_format fmt);

#ifdef PORTED_FROM_MESA
// The following macros are either copied from Mesa3D, or a new implementation
// of a Mesa3D interface is provided here in order to provide an environment
// similar to that found in Mesa3D.  This can help minimize changes to the
// texture-decompression code that was ported from Mesa3D.

#define UBYTE_TO_FLOAT(u) ((float) (u) / (float) 255.0)

/*
 * Color channel component order
 * 
 * \note Changes will almost certainly cause problems at this time.
 */
#define RCOMP 0
#define GCOMP 1
#define BCOMP 2
#define ACOMP 3

#endif // PORTED_FROM_MESA

#endif // PXFMT_INTERNAL_H
