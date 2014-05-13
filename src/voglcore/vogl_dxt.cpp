/**************************************************************************
 *
 * Copyright 2013-2014 RAD Game Tools and Valve Software
 * Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC
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
 **************************************************************************/

// File: vogl_dxt.cpp
#include "vogl_core.h"
#include "vogl_dxt.h"
#include "vogl_dxt1.h"
#include "vogl_ryg_dxt.hpp"
#include "vogl_dxt_fast.h"
#include "vogl_intersect.h"

namespace vogl
{
    const uint8_t g_dxt5_from_linear[cDXT5SelectorValues] = { 0U, 2U, 3U, 4U, 5U, 6U, 7U, 1U };
    const uint8_t g_dxt5_to_linear[cDXT5SelectorValues] = { 0U, 7U, 1U, 2U, 3U, 4U, 5U, 6U };

    const uint8_t g_dxt5_alpha6_to_linear[cDXT5SelectorValues] = { 0U, 5U, 1U, 2U, 3U, 4U, 0U, 0U };

    const uint8_t g_dxt1_from_linear[cDXT1SelectorValues] = { 0U, 2U, 3U, 1U };
    const uint8_t g_dxt1_to_linear[cDXT1SelectorValues] = { 0U, 3U, 1U, 2U };

    const uint8_t g_six_alpha_invert_table[cDXT5SelectorValues] = { 1, 0, 5, 4, 3, 2, 6, 7 };
    const uint8_t g_eight_alpha_invert_table[cDXT5SelectorValues] = { 1, 0, 7, 6, 5, 4, 3, 2 };

    const char *get_dxt_format_string(dxt_format fmt)
    {
        switch (fmt)
        {
            case cDXT1:
                return "DXT1";
            case cDXT1A:
                return "DXT1A";
            case cDXT3:
                return "DXT3";
            case cDXT5:
                return "DXT5";
            case cDXT5A:
                return "DXT5A";
            case cDXN_XY:
                return "DXN_XY";
            case cDXN_YX:
                return "DXN_YX";
            case cETC1:
                return "ETC1";
            default:
                break;
        }
        VOGL_ASSERT(false);
        return "?";
    }

    const char *get_dxt_compressor_name(vogl_dxt_compressor_type c)
    {
        switch (c)
        {
            case cCRNDXTCompressorCRN:
                return "CRN";
            case cCRNDXTCompressorCRNF:
                return "CRNF";
            case cCRNDXTCompressorRYG:
                return "RYG";
#if VOGL_SUPPORT_ATI_COMPRESS
            case cCRNDXTCompressorATI:
                return "ATI";
#endif
            default:
                break;
        }
        VOGL_ASSERT(false);
        return "?";
    }

    uint32_t get_dxt_format_bits_per_pixel(dxt_format fmt)
    {
        switch (fmt)
        {
            case cDXT1:
            case cDXT1A:
            case cDXT5A:
            case cETC1:
                return 4;
            case cDXT3:
            case cDXT5:
            case cDXN_XY:
            case cDXN_YX:
                return 8;
            default:
                break;
        }
        VOGL_ASSERT(false);
        return 0;
    }

    bool get_dxt_format_has_alpha(dxt_format fmt)
    {
        switch (fmt)
        {
            case cDXT1A:
            case cDXT3:
            case cDXT5:
            case cDXT5A:
                return true;
            default:
                break;
        }
        return false;
    }

    uint16_t dxt1_block::pack_color(const color_quad_u8 &color, bool scaled, uint32_t bias)
    {
        uint32_t r = color.r;
        uint32_t g = color.g;
        uint32_t b = color.b;

        if (scaled)
        {
            r = (r * 31U + bias) / 255U;
            g = (g * 63U + bias) / 255U;
            b = (b * 31U + bias) / 255U;
        }

        r = math::minimum(r, 31U);
        g = math::minimum(g, 63U);
        b = math::minimum(b, 31U);

        return static_cast<uint16_t>(b | (g << 5U) | (r << 11U));
    }

    uint16_t dxt1_block::pack_color(uint32_t r, uint32_t g, uint32_t b, bool scaled, uint32_t bias)
    {
        return pack_color(color_quad_u8(r, g, b, 0), scaled, bias);
    }

    color_quad_u8 dxt1_block::unpack_color(uint16_t packed_color, bool scaled, uint32_t alpha)
    {
        uint32_t b = packed_color & 31U;
        uint32_t g = (packed_color >> 5U) & 63U;
        uint32_t r = (packed_color >> 11U) & 31U;

        if (scaled)
        {
            b = (b << 3U) | (b >> 2U);
            g = (g << 2U) | (g >> 4U);
            r = (r << 3U) | (r >> 2U);
        }

        return color_quad_u8(cNoClamp, r, g, b, math::minimum(alpha, 255U));
    }

    void dxt1_block::unpack_color(uint32_t &r, uint32_t &g, uint32_t &b, uint16_t packed_color, bool scaled)
    {
        color_quad_u8 c(unpack_color(packed_color, scaled, 0));
        r = c.r;
        g = c.g;
        b = c.b;
    }

    void dxt1_block::get_block_colors_NV5x(color_quad_u8 *pDst, uint16_t packed_col0, uint16_t packed_col1, bool color4)
    {
        color_quad_u8 col0(unpack_color(packed_col0, false));
        color_quad_u8 col1(unpack_color(packed_col1, false));

        pDst[0].r = (3 * col0.r * 22) / 8;
        pDst[0].b = (3 * col0.b * 22) / 8;
        pDst[0].g = (col0.g << 2) | (col0.g >> 4);
        pDst[0].a = 0xFF;

        pDst[1].r = (3 * col1.r * 22) / 8;
        pDst[1].g = (col1.g << 2) | (col1.g >> 4);
        pDst[1].b = (3 * col1.b * 22) / 8;
        pDst[1].a = 0xFF;

        int gdiff = pDst[1].g - pDst[0].g;

        if (color4) //(packed_col0 > packed_col1)
        {
            pDst[2].r = static_cast<uint8_t>(((2 * col0.r + col1.r) * 22) / 8);
            pDst[2].g = static_cast<uint8_t>((256 * pDst[0].g + gdiff / 4 + 128 + gdiff * 80) / 256);
            pDst[2].b = static_cast<uint8_t>(((2 * col0.b + col1.b) * 22) / 8);
            pDst[2].a = 0xFF;

            pDst[3].r = static_cast<uint8_t>(((2 * col1.r + col0.r) * 22) / 8);
            pDst[3].g = static_cast<uint8_t>((256 * pDst[1].g - gdiff / 4 + 128 - gdiff * 80) / 256);
            pDst[3].b = static_cast<uint8_t>(((2 * col1.b + col0.b) * 22) / 8);
            pDst[3].a = 0xFF;
        }
        else
        {
            pDst[2].r = static_cast<uint8_t>(((col0.r + col1.r) * 33) / 8);
            pDst[2].g = static_cast<uint8_t>((256 * pDst[0].g + gdiff / 4 + 128 + gdiff * 128) / 256);
            pDst[2].b = static_cast<uint8_t>(((col0.b + col1.b) * 33) / 8);
            pDst[2].a = 0xFF;

            pDst[3].r = 0x00;
            pDst[3].g = 0x00;
            pDst[3].b = 0x00;
            pDst[3].a = 0x00;
        }
    }

    uint32_t dxt1_block::get_block_colors3(color_quad_u8 *pDst, uint16_t color0, uint16_t color1)
    {
        color_quad_u8 c0(unpack_color(color0, true));
        color_quad_u8 c1(unpack_color(color1, true));

        pDst[0] = c0;
        pDst[1] = c1;
        pDst[2].set_noclamp_rgba((c0.r + c1.r) >> 1U, (c0.g + c1.g) >> 1U, (c0.b + c1.b) >> 1U, 255U);
        pDst[3].set_noclamp_rgba(0, 0, 0, 0);

        return 3;
    }

    uint32_t dxt1_block::get_block_colors4(color_quad_u8 *pDst, uint16_t color0, uint16_t color1)
    {
        color_quad_u8 c0(unpack_color(color0, true));
        color_quad_u8 c1(unpack_color(color1, true));

        pDst[0] = c0;
        pDst[1] = c1;

        // The compiler changes the div3 into a mul by recip+shift.
        pDst[2].set_noclamp_rgba((c0.r * 2 + c1.r) / 3, (c0.g * 2 + c1.g) / 3, (c0.b * 2 + c1.b) / 3, 255U);
        pDst[3].set_noclamp_rgba((c1.r * 2 + c0.r) / 3, (c1.g * 2 + c0.g) / 3, (c1.b * 2 + c0.b) / 3, 255U);

        return 4;
    }

    uint32_t dxt1_block::get_block_colors3_round(color_quad_u8 *pDst, uint16_t color0, uint16_t color1)
    {
        color_quad_u8 c0(unpack_color(color0, true));
        color_quad_u8 c1(unpack_color(color1, true));

        pDst[0] = c0;
        pDst[1] = c1;
        pDst[2].set_noclamp_rgba((c0.r + c1.r + 1) >> 1U, (c0.g + c1.g + 1) >> 1U, (c0.b + c1.b + 1) >> 1U, 255U);
        pDst[3].set_noclamp_rgba(0, 0, 0, 0);

        return 3;
    }

    uint32_t dxt1_block::get_block_colors4_round(color_quad_u8 *pDst, uint16_t color0, uint16_t color1)
    {
        color_quad_u8 c0(unpack_color(color0, true));
        color_quad_u8 c1(unpack_color(color1, true));

        pDst[0] = c0;
        pDst[1] = c1;

        // 12/14/08 - Supposed to round according to DX docs, but this conflicts with the OpenGL S3TC spec. ?
        // The compiler changes the div3 into a mul by recip+shift.
        pDst[2].set_noclamp_rgba((c0.r * 2 + c1.r + 1) / 3, (c0.g * 2 + c1.g + 1) / 3, (c0.b * 2 + c1.b + 1) / 3, 255U);
        pDst[3].set_noclamp_rgba((c1.r * 2 + c0.r + 1) / 3, (c1.g * 2 + c0.g + 1) / 3, (c1.b * 2 + c0.b + 1) / 3, 255U);

        return 4;
    }

    uint32_t dxt1_block::get_block_colors(color_quad_u8 *pDst, uint16_t color0, uint16_t color1)
    {
        if (color0 > color1)
            return get_block_colors4(pDst, color0, color1);
        else
            return get_block_colors3(pDst, color0, color1);
    }

    uint32_t dxt1_block::get_block_colors_round(color_quad_u8 *pDst, uint16_t color0, uint16_t color1)
    {
        if (color0 > color1)
            return get_block_colors4_round(pDst, color0, color1);
        else
            return get_block_colors3_round(pDst, color0, color1);
    }

    color_quad_u8 dxt1_block::unpack_endpoint(uint32_t endpoints, uint32_t index, bool scaled, uint32_t alpha)
    {
        VOGL_ASSERT(index < 2);
        return unpack_color(static_cast<uint16_t>((endpoints >> (index * 16U)) & 0xFFFFU), scaled, alpha);
    }

    uint32_t dxt1_block::pack_endpoints(uint32_t lo, uint32_t hi)
    {
        VOGL_ASSERT((lo <= 0xFFFFU) && (hi <= 0xFFFFU));
        return lo | (hi << 16U);
    }

    void dxt3_block::set_alpha(uint32_t x, uint32_t y, uint32_t value, bool scaled)
    {
        VOGL_ASSERT((x < cDXTBlockSize) && (y < cDXTBlockSize));

        if (scaled)
        {
            VOGL_ASSERT(value <= 0xFF);
            value = (value * 15U + 128U) / 255U;
        }
        else
        {
            VOGL_ASSERT(value <= 0xF);
        }

        uint32_t ofs = (y << 1U) + (x >> 1U);
        uint32_t c = m_alpha[ofs];

        c &= ~(0xF << ((x & 1U) << 2U));
        c |= (value << ((x & 1U) << 2U));

        m_alpha[ofs] = static_cast<uint8_t>(c);
    }

    uint32_t dxt3_block::get_alpha(uint32_t x, uint32_t y, bool scaled) const
    {
        VOGL_ASSERT((x < cDXTBlockSize) && (y < cDXTBlockSize));

        uint32_t value = m_alpha[(y << 1U) + (x >> 1U)];
        if (x & 1)
            value >>= 4;
        value &= 0xF;

        if (scaled)
            value = (value << 4U) | value;

        return value;
    }

    uint32_t dxt5_block::get_block_values6(color_quad_u8 *pDst, uint32_t l, uint32_t h)
    {
        pDst[0].a = static_cast<uint8_t>(l);
        pDst[1].a = static_cast<uint8_t>(h);
        pDst[2].a = static_cast<uint8_t>((l * 4 + h) / 5);
        pDst[3].a = static_cast<uint8_t>((l * 3 + h * 2) / 5);
        pDst[4].a = static_cast<uint8_t>((l * 2 + h * 3) / 5);
        pDst[5].a = static_cast<uint8_t>((l + h * 4) / 5);
        pDst[6].a = 0;
        pDst[7].a = 255;
        return 6;
    }

    uint32_t dxt5_block::get_block_values8(color_quad_u8 *pDst, uint32_t l, uint32_t h)
    {
        pDst[0].a = static_cast<uint8_t>(l);
        pDst[1].a = static_cast<uint8_t>(h);
        pDst[2].a = static_cast<uint8_t>((l * 6 + h) / 7);
        pDst[3].a = static_cast<uint8_t>((l * 5 + h * 2) / 7);
        pDst[4].a = static_cast<uint8_t>((l * 4 + h * 3) / 7);
        pDst[5].a = static_cast<uint8_t>((l * 3 + h * 4) / 7);
        pDst[6].a = static_cast<uint8_t>((l * 2 + h * 5) / 7);
        pDst[7].a = static_cast<uint8_t>((l + h * 6) / 7);
        return 8;
    }

    uint32_t dxt5_block::get_block_values(color_quad_u8 *pDst, uint32_t l, uint32_t h)
    {
        if (l > h)
            return get_block_values8(pDst, l, h);
        else
            return get_block_values6(pDst, l, h);
    }

    uint32_t dxt5_block::get_block_values6(uint32_t *pDst, uint32_t l, uint32_t h)
    {
        pDst[0] = l;
        pDst[1] = h;
        pDst[2] = (l * 4 + h) / 5;
        pDst[3] = (l * 3 + h * 2) / 5;
        pDst[4] = (l * 2 + h * 3) / 5;
        pDst[5] = (l + h * 4) / 5;
        pDst[6] = 0;
        pDst[7] = 255;
        return 6;
    }

    uint32_t dxt5_block::get_block_values8(uint32_t *pDst, uint32_t l, uint32_t h)
    {
        pDst[0] = l;
        pDst[1] = h;
        pDst[2] = (l * 6 + h) / 7;
        pDst[3] = (l * 5 + h * 2) / 7;
        pDst[4] = (l * 4 + h * 3) / 7;
        pDst[5] = (l * 3 + h * 4) / 7;
        pDst[6] = (l * 2 + h * 5) / 7;
        pDst[7] = (l + h * 6) / 7;
        return 8;
    }

    uint32_t dxt5_block::unpack_endpoint(uint32_t packed, uint32_t index)
    {
        VOGL_ASSERT(index < 2);
        return (packed >> (8 * index)) & 0xFF;
    }

    uint32_t dxt5_block::pack_endpoints(uint32_t lo, uint32_t hi)
    {
        VOGL_ASSERT((lo <= 0xFF) && (hi <= 0xFF));
        return lo | (hi << 8U);
    }

    uint32_t dxt5_block::get_block_values(uint32_t *pDst, uint32_t l, uint32_t h)
    {
        if (l > h)
            return get_block_values8(pDst, l, h);
        else
            return get_block_values6(pDst, l, h);
    }

} // namespace vogl