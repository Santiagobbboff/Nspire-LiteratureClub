/*****************************************************************************
 *
 *  XVID MPEG-4 VIDEO CODEC
 *  - SRAM Table Management Header -
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

#ifndef _SRAM_TABLES_H_
#define _SRAM_TABLES_H_

#include "../portab.h"
#include "../bitstream/vlc_codes.h"

/**
 * Initialize all SRAM-backed lookup tables.
 * Must be called after xvid_init_sram() and before decoding.
 * Tables are copied from ROM/SDRAM to fast SRAM.
 */
void init_sram_tables(void);

/**
 * Get current SRAM usage in bytes.
 */
unsigned int get_sram_tables_usage(void);

/* SRAM-backed VLC decoding tables */
extern VLC *sram_mcbpc_intra_table;   /* [64] */
extern VLC *sram_mcbpc_inter_table;   /* [257] */
extern VLC *sram_cbpy_table;          /* [64] */
extern VLC *sram_TMNMVtab0;           /* [14] */
extern VLC *sram_TMNMVtab1;           /* [96] */
extern VLC *sram_TMNMVtab2;           /* [128] */

/* SRAM-backed DC coefficient tables */
extern VLC *sram_dcy_tab;             /* [511] */
extern VLC *sram_dcc_tab;             /* [511] */
extern VLC *sram_dc_lum_tab;          /* [8] */
extern short *sram_dc_threshold;      /* [64] */

/* SRAM-backed rounding tables */
extern uint32_t *sram_roundtab;       /* [16] */
extern uint32_t *sram_roundtab_76;    /* [16] */
extern uint32_t *sram_roundtab_78;    /* [8] */
extern uint32_t *sram_roundtab_79;    /* [4] */

/* SRAM-backed zigzag scan tables */
extern uint16_t *sram_scan_tables;    /* [3][64] = 192 */

#endif /* _SRAM_TABLES_H_ */
