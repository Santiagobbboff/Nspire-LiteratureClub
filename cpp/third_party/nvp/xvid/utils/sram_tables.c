/*****************************************************************************
 *
 *  XVID MPEG-4 VIDEO CODEC
 *  - SRAM Table Management -
 *
 *  Copyright(C) 2026
 *
 *  This program is free software ; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation ; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY ; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 ****************************************************************************/

#include <string.h>
#include "sram_tables.h"
#include "mem_align.h"
#include "../bitstream/vlc_codes.h"
#include "../bitstream/zigzag.h"

/* Rounding tables are defined in motion/estimation_common.c
 * We declare them here directly to avoid including motion.h which has
 * heavy dependencies on encoder-specific types */
extern const uint32_t roundtab[16];
extern const uint32_t roundtab_76[16];
extern const uint32_t roundtab_78[8];
extern const uint32_t roundtab_79[4];

/* Table sizes */
#define MCBPC_INTRA_SIZE    64
#define MCBPC_INTER_SIZE    257
#define CBPY_SIZE           64
#define TMNMV0_SIZE         14
#define TMNMV1_SIZE         96
#define TMNMV2_SIZE         128
#define DCY_SIZE            511
#define DCC_SIZE            511
#define DC_LUM_SIZE         8
#define DC_THRESHOLD_SIZE   64
#define ROUNDTAB_SIZE       16
#define ROUNDTAB_76_SIZE    16
#define ROUNDTAB_78_SIZE    8
#define ROUNDTAB_79_SIZE    4
#define SCAN_TABLES_SIZE    (3 * 64)

/* SRAM-backed table pointers (initially NULL, point to SRAM after init) */
VLC *sram_mcbpc_intra_table = NULL;
VLC *sram_mcbpc_inter_table = NULL;
VLC *sram_cbpy_table = NULL;
VLC *sram_TMNMVtab0 = NULL;
VLC *sram_TMNMVtab1 = NULL;
VLC *sram_TMNMVtab2 = NULL;
VLC *sram_dcy_tab = NULL;
VLC *sram_dcc_tab = NULL;
VLC *sram_dc_lum_tab = NULL;
short *sram_dc_threshold = NULL;
uint32_t *sram_roundtab = NULL;
uint32_t *sram_roundtab_76 = NULL;
uint32_t *sram_roundtab_78 = NULL;
uint32_t *sram_roundtab_79 = NULL;
uint16_t *sram_scan_tables = NULL;

/* Track total SRAM usage */
static unsigned int sram_tables_bytes = 0;

unsigned int
get_sram_tables_usage(void)
{
    return sram_tables_bytes;
}

void
init_sram_tables(void)
{
    /* Already initialized? */
    if (sram_mcbpc_intra_table != NULL) {
        return;
    }

    sram_tables_bytes = 0;

    /* Priority 1: VLC decoding tables */
    
    sram_mcbpc_intra_table = (VLC*)xvid_malloc_sram(MCBPC_INTRA_SIZE * sizeof(VLC), CACHE_LINE);
    if (sram_mcbpc_intra_table) {
        memcpy(sram_mcbpc_intra_table, mcbpc_intra_table, MCBPC_INTRA_SIZE * sizeof(VLC));
        sram_tables_bytes += MCBPC_INTRA_SIZE * sizeof(VLC);
    } else {
        sram_mcbpc_intra_table = (VLC*)mcbpc_intra_table;
    }

    sram_mcbpc_inter_table = (VLC*)xvid_malloc_sram(MCBPC_INTER_SIZE * sizeof(VLC), CACHE_LINE);
    if (sram_mcbpc_inter_table) {
        memcpy(sram_mcbpc_inter_table, mcbpc_inter_table, MCBPC_INTER_SIZE * sizeof(VLC));
        sram_tables_bytes += MCBPC_INTER_SIZE * sizeof(VLC);
    } else {
        sram_mcbpc_inter_table = (VLC*)mcbpc_inter_table;
    }

    sram_cbpy_table = (VLC*)xvid_malloc_sram(CBPY_SIZE * sizeof(VLC), CACHE_LINE);
    if (sram_cbpy_table) {
        memcpy(sram_cbpy_table, cbpy_table, CBPY_SIZE * sizeof(VLC));
        sram_tables_bytes += CBPY_SIZE * sizeof(VLC);
    } else {
        sram_cbpy_table = (VLC*)cbpy_table;
    }

    sram_TMNMVtab0 = (VLC*)xvid_malloc_sram(TMNMV0_SIZE * sizeof(VLC), CACHE_LINE);
    if (sram_TMNMVtab0) {
        memcpy(sram_TMNMVtab0, TMNMVtab0, TMNMV0_SIZE * sizeof(VLC));
        sram_tables_bytes += TMNMV0_SIZE * sizeof(VLC);
    } else {
        sram_TMNMVtab0 = (VLC*)TMNMVtab0;
    }

    sram_TMNMVtab1 = (VLC*)xvid_malloc_sram(TMNMV1_SIZE * sizeof(VLC), CACHE_LINE);
    if (sram_TMNMVtab1) {
        memcpy(sram_TMNMVtab1, TMNMVtab1, TMNMV1_SIZE * sizeof(VLC));
        sram_tables_bytes += TMNMV1_SIZE * sizeof(VLC);
    } else {
        sram_TMNMVtab1 = (VLC*)TMNMVtab1;
    }

    sram_TMNMVtab2 = (VLC*)xvid_malloc_sram(TMNMV2_SIZE * sizeof(VLC), CACHE_LINE);
    if (sram_TMNMVtab2) {
        memcpy(sram_TMNMVtab2, TMNMVtab2, TMNMV2_SIZE * sizeof(VLC));
        sram_tables_bytes += TMNMV2_SIZE * sizeof(VLC);
    } else {
        sram_TMNMVtab2 = (VLC*)TMNMVtab2;
    }

    /* Priority 2: DC coefficient tables */
    
    sram_dcy_tab = (VLC*)xvid_malloc_sram(DCY_SIZE * sizeof(VLC), CACHE_LINE);
    if (sram_dcy_tab) {
        memcpy(sram_dcy_tab, dcy_tab, DCY_SIZE * sizeof(VLC));
        sram_tables_bytes += DCY_SIZE * sizeof(VLC);
    } else {
        sram_dcy_tab = (VLC*)dcy_tab;
    }

    sram_dcc_tab = (VLC*)xvid_malloc_sram(DCC_SIZE * sizeof(VLC), CACHE_LINE);
    if (sram_dcc_tab) {
        memcpy(sram_dcc_tab, dcc_tab, DCC_SIZE * sizeof(VLC));
        sram_tables_bytes += DCC_SIZE * sizeof(VLC);
    } else {
        sram_dcc_tab = (VLC*)dcc_tab;
    }

    sram_dc_lum_tab = (VLC*)xvid_malloc_sram(DC_LUM_SIZE * sizeof(VLC), CACHE_LINE);
    if (sram_dc_lum_tab) {
        memcpy(sram_dc_lum_tab, dc_lum_tab, DC_LUM_SIZE * sizeof(VLC));
        sram_tables_bytes += DC_LUM_SIZE * sizeof(VLC);
    } else {
        sram_dc_lum_tab = (VLC*)dc_lum_tab;
    }

    sram_dc_threshold = (short*)xvid_malloc_sram(DC_THRESHOLD_SIZE * sizeof(short), CACHE_LINE);
    if (sram_dc_threshold) {
        memcpy(sram_dc_threshold, dc_threshold, DC_THRESHOLD_SIZE * sizeof(short));
        sram_tables_bytes += DC_THRESHOLD_SIZE * sizeof(short);
    } else {
        sram_dc_threshold = (short*)dc_threshold;
    }

    /* Priority 3: Rounding tables */
    
    sram_roundtab = (uint32_t*)xvid_malloc_sram(ROUNDTAB_SIZE * sizeof(uint32_t), CACHE_LINE);
    if (sram_roundtab) {
        memcpy(sram_roundtab, roundtab, ROUNDTAB_SIZE * sizeof(uint32_t));
        sram_tables_bytes += ROUNDTAB_SIZE * sizeof(uint32_t);
    } else {
        sram_roundtab = (uint32_t*)roundtab;
    }

    sram_roundtab_76 = (uint32_t*)xvid_malloc_sram(ROUNDTAB_76_SIZE * sizeof(uint32_t), CACHE_LINE);
    if (sram_roundtab_76) {
        memcpy(sram_roundtab_76, roundtab_76, ROUNDTAB_76_SIZE * sizeof(uint32_t));
        sram_tables_bytes += ROUNDTAB_76_SIZE * sizeof(uint32_t);
    } else {
        sram_roundtab_76 = (uint32_t*)roundtab_76;
    }

    sram_roundtab_78 = (uint32_t*)xvid_malloc_sram(ROUNDTAB_78_SIZE * sizeof(uint32_t), CACHE_LINE);
    if (sram_roundtab_78) {
        memcpy(sram_roundtab_78, roundtab_78, ROUNDTAB_78_SIZE * sizeof(uint32_t));
        sram_tables_bytes += ROUNDTAB_78_SIZE * sizeof(uint32_t);
    } else {
        sram_roundtab_78 = (uint32_t*)roundtab_78;
    }

    sram_roundtab_79 = (uint32_t*)xvid_malloc_sram(ROUNDTAB_79_SIZE * sizeof(uint32_t), CACHE_LINE);
    if (sram_roundtab_79) {
        memcpy(sram_roundtab_79, roundtab_79, ROUNDTAB_79_SIZE * sizeof(uint32_t));
        sram_tables_bytes += ROUNDTAB_79_SIZE * sizeof(uint32_t);
    } else {
        sram_roundtab_79 = (uint32_t*)roundtab_79;
    }

    /* Priority 4: Zigzag scan tables */
    
    sram_scan_tables = (uint16_t*)xvid_malloc_sram(SCAN_TABLES_SIZE * sizeof(uint16_t), CACHE_LINE);
    if (sram_scan_tables) {
        memcpy(sram_scan_tables, scan_tables, SCAN_TABLES_SIZE * sizeof(uint16_t));
        sram_tables_bytes += SCAN_TABLES_SIZE * sizeof(uint16_t);
    } else {
        sram_scan_tables = (uint16_t*)scan_tables;
    }
}
