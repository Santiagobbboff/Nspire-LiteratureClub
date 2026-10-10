#include "../../global.h"
#include "../colorspace.h"
#include "../../utils/mem_align.h"

#if defined(_MSC_VER)
#define restrict __restrict
#define __attribute__(x)
#endif

#if defined(__GNUC__) || defined(__clang__)
  typedef uint32_t u32_alias __attribute__((__may_alias__));
  typedef uint16_t u16_alias __attribute__((__may_alias__));
#else
  typedef uint32_t u32_alias;
  typedef uint16_t u16_alias;
#endif

/*
 * RGB565 is the dominant output cost on ARM9.  Quantize each chroma channel
 * to five bits and trade 512 KiB of zero-initialized memory for a single
 * lookup per output pixel.  The maximum chroma error is four source levels,
 * below the useful precision of the final RGB565 display.
 */
enum { CHROMA_BITS = 5, CHROMA_LEVELS = 1 << CHROMA_BITS, CHROMA_SHIFT = 8 - CHROMA_BITS };
/* Keep every 512-byte chroma slab cache-line aligned on ARM9/ARM11. */
static uint16_t g_YuvToRgb565[CHROMA_LEVELS * CHROMA_LEVELS * 256]
    __attribute__((aligned(32)));

__attribute__((hot))
static uint16_t pack_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    // r:8 -> 5, g:8 -> 6, b:8 -> 5
    // const uint16_t r5 = r & 0xF8; // top 5 bits
    // const uint16_t g6 = g & 0xFC; // top 6 bits
    // // const uint16_t b5 = b & 0xF8; // b doesnt need a mask since we shift right

    // return (uint16_t)((r5 << 8) | (g6 << 3) | (b >> 3));

    return (uint16_t)(((uint16_t)(r >> 3) << 11) |
                      ((uint16_t)(g >> 2) << 5)  |
                      ((uint16_t)(b >> 3) << 0));
}

static uint8_t clamp_u8(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

void init_yv12_to_rgb565_tables(void) {
    int uq, vq, y;
    for (uq = 0; uq < CHROMA_LEVELS; ++uq) {
        const int u = (uq << CHROMA_SHIFT) + (1 << (CHROMA_SHIFT - 1));
        for (vq = 0; vq < CHROMA_LEVELS; ++vq) {
            const int v = (vq << CHROMA_SHIFT) + (1 << (CHROMA_SHIFT - 1));
            uint16_t* table = g_YuvToRgb565 + ((uq * CHROMA_LEVELS + vq) << 8);
            for (y = 0; y < 256; ++y) {
                const int luma = 298 * (y - 16) + 128;
                const uint8_t r = clamp_u8((luma + 409 * (v - 128)) >> 8);
                const uint8_t g = clamp_u8((luma - 100 * (u - 128) - 208 * (v - 128)) >> 8);
                const uint8_t b = clamp_u8((luma + 516 * (u - 128)) >> 8);
                table[y] = pack_rgb565(r, g, b);
            }
        }
    }
}

__attribute__((hot))
void yv12_to_rgb565_concept(
    uint8_t *restrict x_ptr,
    int x_stride,
    uint8_t *restrict y_src,
    uint8_t *restrict u_src,
    uint8_t *restrict v_src,
    int y_stride,
    int uv_stride,
    int width,
    int height,
    int vflip
) {

    // Output setup (word-based stores)
    int dst_stride_words = x_stride >> 2;
    u32_alias* dst_row = (u32_alias*)x_ptr;

    if (vflip) {
        dst_row = (u32_alias*)(x_ptr + (height - 1) * x_stride);
        dst_stride_words = -dst_stride_words;
    }

    // Input row pointers
    const uint8_t* y_row = y_src;
    const uint8_t* u_row = u_src;
    const uint8_t* v_row = v_src;

    // Process 2 rows at a time (YV12 4:2:0 => one UV row per two Y rows)
    for (int y = 0; y < height; y += 2) {
        const uint8_t* y0 = y_row;
        const uint8_t* y1 = y_row + y_stride;

        // UV row pointers (byte), but we'll read 16-bit chunks in the hot loop
        const u16_alias* u16p = (const u16_alias*)u_row;
        const u16_alias* v16p = (const u16_alias*)v_row;

        // Y row pointers as 32-bit (4 pixels per load)
        const u32_alias* y0_32 = (const u32_alias*)y0;
        const u32_alias* y1_32 = (const u32_alias*)y1;

        // Destination: each u32 store writes 2 RGB565 pixels
        u32_alias* dst0 = dst_row;
        u32_alias* dst1 = dst_row + dst_stride_words;

        // 4 columns per iter (2 UV samples per iter)
        int n4 = width >> 2;     // number of 4-pixel groups
        int rem2 = width & 2;    // remaining 2 pixels? (0 or 2)

        for (int i = 0; i < n4; ++i) {
            // Two chroma samples packed into one halfword each
            uint16_t u01 = *u16p++;
            uint16_t v01 = *v16p++;

            // Four Ys per row
            uint32_t y0_4 = *y0_32++;
            uint32_t y1_4 = *y1_32++;

            // ---- Sample 0 (columns x..x+1) ----
            uint8_t u0 = (uint8_t)(u01);
            uint8_t v0 = (uint8_t)(v01);

            const uint16_t* table0 = g_YuvToRgb565 +
                ((((unsigned int)u0 >> CHROMA_SHIFT) * CHROMA_LEVELS +
                  ((unsigned int)v0 >> CHROMA_SHIFT)) << 8);

            uint8_t y00 = (uint8_t)(y0_4);
            uint8_t y01 = (uint8_t)(y0_4 >> 8);
            uint8_t y10 = (uint8_t)(y1_4);
            uint8_t y11 = (uint8_t)(y1_4 >> 8);

            uint16_t p00 = table0[y00];
            uint16_t p01 = table0[y01];
            uint16_t p10 = table0[y10];
            uint16_t p11 = table0[y11];

            // ---- Sample 1 (columns x+2..x+3) ----
            uint8_t u1 = (uint8_t)(u01 >> 8);
            uint8_t v1 = (uint8_t)(v01 >> 8);

            const uint16_t* table1 = g_YuvToRgb565 +
                ((((unsigned int)u1 >> CHROMA_SHIFT) * CHROMA_LEVELS +
                  ((unsigned int)v1 >> CHROMA_SHIFT)) << 8);

            uint8_t y02 = (uint8_t)(y0_4 >> 16);
            uint8_t y03 = (uint8_t)(y0_4 >> 24);
            uint8_t y12 = (uint8_t)(y1_4 >> 16);
            uint8_t y13 = (uint8_t)(y1_4 >> 24);

            uint16_t p02 = table1[y02];
            uint16_t p03 = table1[y03];
            uint16_t p12 = table1[y12];
            uint16_t p13 = table1[y13];

            // Store: 2 pixels per word
            dst0[0] = (uint32_t)p00 | ((uint32_t)p01 << 16);
            dst0[1] = (uint32_t)p02 | ((uint32_t)p03 << 16);
            dst1[0] = (uint32_t)p10 | ((uint32_t)p11 << 16);
            dst1[1] = (uint32_t)p12 | ((uint32_t)p13 << 16);

            dst0 += 2;
            dst1 += 2;
        }

        if (rem2) {
            const uint8_t* y0_tail = (const uint8_t*) y0_32;
            const uint8_t* y1_tail = (const uint8_t*) y1_32;
            const uint8_t* u_tail = (const uint8_t*) u16p;
            const uint8_t* v_tail = (const uint8_t*) v16p;
            uint8_t u0 = u_tail[0];
            uint8_t v0 = v_tail[0];
            const uint16_t* table0 = g_YuvToRgb565 +
                ((((unsigned int)u0 >> CHROMA_SHIFT) * CHROMA_LEVELS +
                  ((unsigned int)v0 >> CHROMA_SHIFT)) << 8);
            uint16_t p00 = table0[y0_tail[0]];
            uint16_t p01 = table0[y0_tail[1]];
            uint16_t p10 = table0[y1_tail[0]];
            uint16_t p11 = table0[y1_tail[1]];

            dst0[0] = (uint32_t) p00 | ((uint32_t) p01 << 16);
            dst1[0] = (uint32_t) p10 | ((uint32_t) p11 << 16);
        }

        // Advance to next 2-row block
        y_row  += 2 * y_stride;
        u_row  += uv_stride;
        v_row  += uv_stride;
        dst_row += 2 * dst_stride_words;
    }
}
