#include "mpeg4_xvid.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "decoder.h"
#include "xvid.h"

static bool g_xvid_initialized = false;
static char g_xvid_error[160];

static void mpeg4_xvid_set_error(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsnprintf(g_xvid_error, sizeof(g_xvid_error), fmt, args);
    va_end(args);
    g_xvid_error[sizeof(g_xvid_error) - 1U] = '\0';
}

static const char *mpeg4_xvid_error_name(int error_code)
{
    switch (error_code) {
    case XVID_ERR_FAIL:
        return "generic failure";
    case XVID_ERR_MEMORY:
        return "allocation failed";
    case XVID_ERR_FORMAT:
        return "invalid bitstream format";
    case XVID_ERR_VERSION:
        return "version mismatch";
    case XVID_ERR_END:
        return "end of stream";
    default:
        return "unknown error";
    }
}

static bool mpeg4_xvid_reject_unsupported_vol_features(void *handle)
{
    const DECODER *decoder = (const DECODER *) handle;

    if (!decoder) {
        mpeg4_xvid_set_error("xvid decode failed: decoder missing");
        return false;
    }
    if (decoder->quarterpel) {
        mpeg4_xvid_set_error("unsupported MPEG-4 VOL feature: quarterpel/qpel");
        return false;
    }
    if (decoder->sprite_enable != 0) {
        mpeg4_xvid_set_error("unsupported MPEG-4 VOL feature: sprites/GMC");
        return false;
    }
    return true;
}

bool mpeg4_xvid_global_init(void *sram_base, unsigned int sram_size)
{
    xvid_gbl_init_t init;
    int result;

    if (g_xvid_initialized) {
        return true;
    }

    memset(&init, 0, sizeof(init));
    init.version = XVID_VERSION;
    init.sram_base = sram_base;
    init.sram_size = sram_size;

    result = xvid_global(NULL, XVID_GBL_INIT, &init, NULL);
    if (result < 0) {
        mpeg4_xvid_set_error("xvid global init failed: %s (%d)", mpeg4_xvid_error_name(result), result);
        return false;
    }

    g_xvid_initialized = true;
    g_xvid_error[0] = '\0';
    return true;
}

bool mpeg4_xvid_create(void **out_handle, int width, int height)
{
    xvid_dec_create_t create;
    int result;

    if (!out_handle) {
        mpeg4_xvid_set_error("xvid create failed: output handle missing");
        return false;
    }

    *out_handle = NULL;
    memset(&create, 0, sizeof(create));
    create.version = XVID_VERSION;
    create.width = width;
    create.height = height;

    result = xvid_decore(NULL, XVID_DEC_CREATE, &create, NULL);
    if (result < 0 || !create.handle) {
        mpeg4_xvid_set_error("xvid decoder create failed: %s (%d)", mpeg4_xvid_error_name(result), result);
        return false;
    }

    *out_handle = create.handle;
    g_xvid_error[0] = '\0';
    return true;
}

void mpeg4_xvid_destroy(void *handle)
{
    if (handle) {
        xvid_decore(handle, XVID_DEC_DESTROY, NULL, NULL);
    }
}

bool mpeg4_xvid_reset(void **handle, int width, int height)
{
    if (!handle) {
        mpeg4_xvid_set_error("xvid reset failed: handle missing");
        return false;
    }
    if (*handle) {
        mpeg4_xvid_destroy(*handle);
        *handle = NULL;
    }
    return mpeg4_xvid_create(handle, width, height);
}

static bool mpeg4_xvid_decode_iteration(
    void *handle,
    const uint8_t *data,
    size_t data_size,
    uint16_t *rgb565,
    int width,
    int height,
    int output_stride,
    bool output,
    bool discontinuity,
    int *out_consumed,
    int *out_type
)
{
    xvid_dec_frame_t frame;
    xvid_dec_stats_t stats;
    int result;

    if (!handle || !data || data_size == 0 || !out_consumed || !out_type) {
        mpeg4_xvid_set_error("xvid decode failed: invalid input");
        return false;
    }

    memset(&frame, 0, sizeof(frame));
    memset(&stats, 0, sizeof(stats));
    frame.version = XVID_VERSION;
    stats.version = XVID_VERSION;
    frame.general = XVID_LOWDELAY | XVID_DEC_FAST | (discontinuity ? XVID_DISCONTINUITY : 0);
    frame.bitstream = (void *) data;
    frame.length = data_size > (size_t) INT32_MAX ? INT32_MAX : (int) data_size;
    if (output) {
        if (!rgb565) {
            mpeg4_xvid_set_error("xvid decode failed: RGB565 output missing");
            return false;
        }
        frame.output.csp = XVID_CSP_RGB565;
        frame.output.plane[0] = rgb565;
        frame.output.stride[0] = output_stride;
    } else {
        frame.output.csp = XVID_CSP_NULL;
    }

    result = xvid_decore(handle, XVID_DEC_DECODE, &frame, &stats);
    if (result < 0) {
        mpeg4_xvid_set_error("xvid decode failed: %s (%d)", mpeg4_xvid_error_name(result), result);
        return false;
    }
    if (result == 0) {
        mpeg4_xvid_set_error("xvid decode stalled: no bytes consumed");
        return false;
    }
    if ((size_t) result > data_size) {
        mpeg4_xvid_set_error("xvid decode overread: consumed %d of %lu", result, (unsigned long) data_size);
        return false;
    }
    if (stats.type == XVID_TYPE_VOL &&
        ((stats.data.vol.width > 0 && stats.data.vol.width != width) ||
         (stats.data.vol.height > 0 && stats.data.vol.height != height))) {
        mpeg4_xvid_set_error(
            "xvid VOL size mismatch: got %dx%d expected %dx%d",
            stats.data.vol.width,
            stats.data.vol.height,
            width,
            height
        );
        return false;
    }
    if (!mpeg4_xvid_reject_unsupported_vol_features(handle)) {
        return false;
    }

    *out_consumed = result;
    *out_type = stats.type;
    return true;
}

bool mpeg4_xvid_decode_frame(
    void *handle,
    const uint8_t *data,
    size_t data_size,
    uint16_t *rgb565,
    int width,
    int height,
    int output_stride,
    bool output,
    bool discontinuity
)
{
    size_t offset = 0;
    bool saw_header = false;

    while (offset < data_size) {
        int consumed = 0;
        int type = XVID_TYPE_NOTHING;

        if (!mpeg4_xvid_decode_iteration(
                handle,
                data + offset,
                data_size - offset,
                rgb565,
                width,
                height,
                output_stride,
                output,
                discontinuity && offset == 0,
                &consumed,
                &type)) {
            return false;
        }
        offset += (size_t) consumed;

        if (type == XVID_TYPE_VOL) {
            saw_header = true;
            continue;
        }
        if (type == XVID_TYPE_IVOP || type == XVID_TYPE_PVOP) {
            g_xvid_error[0] = '\0';
            return true;
        }
        if (type == XVID_TYPE_BVOP || type == XVID_TYPE_SVOP) {
            mpeg4_xvid_set_error("unsupported MPEG-4 VOP type %d; encode with B-frames/GMC disabled", type);
            return false;
        }
        if (type == XVID_TYPE_NOTHING || type == 5) {
            continue;
        }

        mpeg4_xvid_set_error("unexpected MPEG-4 decode unit type %d%s", type, saw_header ? " after VOL" : "");
        return false;
    }

    mpeg4_xvid_set_error("MPEG-4 frame payload ended before a VOP was decoded");
    return false;
}

const char *mpeg4_xvid_last_error(void)
{
    return g_xvid_error[0] ? g_xvid_error : "xvid error";
}
