#ifndef NDVIDEO_MPEG4_XVID_H
#define NDVIDEO_MPEG4_XVID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool mpeg4_xvid_global_init(void *sram_base, unsigned int sram_size);
bool mpeg4_xvid_create(void **out_handle, int width, int height);
bool mpeg4_xvid_reset(void **handle, int width, int height);
void mpeg4_xvid_destroy(void *handle);
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
);
const char *mpeg4_xvid_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
