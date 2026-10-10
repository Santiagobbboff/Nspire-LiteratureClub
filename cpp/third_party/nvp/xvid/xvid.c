/*****************************************************************************
 *
 *  XVID MPEG-4 VIDEO CODEC
 *  - Native API implementation  -
 *
 *  Copyright(C) 2001-2014 Peter Ross <pross@xvid.org>
 *               2002-2014 Michael Militzer <isibaar@xvid.org>
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program ; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * $Id: xvid.c 2190 2019-12-28 09:56:26Z Isibaar $
 *
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#if defined(__APPLE__) && defined(__MACH__) && !defined(_SC_NPROCESSORS_CONF)
#include <sys/types.h>
#include <sys/sysctl.h>
#ifdef MAX
#undef MAX
#endif
#ifdef MIN
#undef MIN
#endif
#endif

#if defined(__amigaos4__)
#include <exec/exec.h>
#include <proto/exec.h>
#endif

#include "xvid.h"
#include "decoder.h"
#ifndef XVID_DECODER_ONLY
#include "encoder.h"
#endif
#include "bitstream/cbp.h"
#include "dct/idct.h"
#include "dct/fdct.h"
#include "image/colorspace.h"
#include "image/interpolate8x8.h"
#include "utils/mem_align.h"
#include "utils/mem_transfer.h"
#include "utils/mbfunctions.h"
#include "quant/quant.h"
#include "motion/motion.h"
#include "motion/gmc.h"
#include "motion/sad.h"
#include "utils/emms.h"
#include "utils/timer.h"
#include "bitstream/mbcoding.h"
#include "image/qpel.h"
#include "image/postprocessing.h"

#if defined(_DEBUG)
unsigned int xvid_debug = 0; /* xvid debug mask */
#endif


/* detect cpu flags  */
static unsigned int
detect_cpu_flags(void)
{
	/* enable native assembly optimizations by default */
	unsigned int cpu_flags = XVID_CPU_ASM;

	return cpu_flags;
}


/*****************************************************************************
 * Xvid Init Entry point
 *
 * Well this function initialize all internal function pointers according
 * to the CPU features forced by the library client or autodetected (depending
 * on the XVID_CPU_FORCE flag). It also initializes vlc coding tables and all
 * image colorspace transformation tables.
 *
 * Returned value : XVID_ERR_OK
 *                  + API_VERSION in the input XVID_INIT_PARAM structure
 *                  + core build  "   "    "       "               "
 *
 ****************************************************************************/


static
int xvid_gbl_init(xvid_gbl_init_t * init)
{
	unsigned int cpu_flags;

	if (XVID_VERSION_MAJOR(init->version) != 1) /* v1.x.x */
		return XVID_ERR_VERSION;

	cpu_flags = (init->cpu_flags & XVID_CPU_FORCE) ? init->cpu_flags : detect_cpu_flags();

	if (init->sram_base && init->sram_size > 0) {
		xvid_init_sram(init->sram_base, init->sram_size);
	}

	/* Initialize the function pointers */
	init_vlc_tables();

	/* Fixed Point Forward/Inverse DCT transformations */
	fdct = fdct_int32;
	// idct = idct_int32;
	idct = simple_idct_c;

	/* Only needed on PPC Altivec archs */
	sadInit = NULL;

	/* Restore FPU context : emms_c is a nop functions */
	emms = emms_c;

	/* Qpel stuff */
	xvid_QP_Funcs = &xvid_QP_Funcs_C;
	xvid_QP_Add_Funcs = &xvid_QP_Add_Funcs_C;
	xvid_Init_QP();

	/* Quantization functions */
	quant_h263_intra   = quant_h263_intra_c;
	quant_h263_inter   = quant_h263_inter_c;
	dequant_h263_intra = dequant_h263_intra_c;
	dequant_h263_inter = dequant_h263_inter_c;

	quant_mpeg_intra   = quant_mpeg_intra_c;
	quant_mpeg_inter   = quant_mpeg_inter_c;
	dequant_mpeg_intra = dequant_mpeg_intra_c;
	dequant_mpeg_inter = dequant_mpeg_inter_c;

	/* Block transfer related functions */
	transfer_8to16copy = transfer_8to16copy_c;
	transfer_16to8copy = transfer_16to8copy_c;
	transfer_8to16sub  = transfer_8to16sub_c;
	transfer_8to16subro  = transfer_8to16subro_c;
	transfer_8to16sub2 = transfer_8to16sub2_c;
	transfer_8to16sub2ro = transfer_8to16sub2ro_c;
	transfer_16to8add  = transfer_16to8add_c;
	transfer8x8_copy   = transfer8x8_copy_c;
	transfer8x4_copy   = transfer8x4_copy_c;

	/* Interlacing functions */
	MBFieldTest = MBFieldTest_c;

	/* Image interpolation related functions */
	interpolate8x8_halfpel_h  = interpolate8x8_halfpel_h_c;
	interpolate8x8_halfpel_v  = interpolate8x8_halfpel_v_c;
	interpolate8x8_halfpel_hv = interpolate8x8_halfpel_hv_c;

	interpolate8x4_halfpel_h  = interpolate8x4_halfpel_h_c;
	interpolate8x4_halfpel_v  = interpolate8x4_halfpel_v_c;
	interpolate8x4_halfpel_hv = interpolate8x4_halfpel_hv_c;

	interpolate8x8_halfpel_add = interpolate8x8_halfpel_add_c;
	interpolate8x8_halfpel_h_add = interpolate8x8_halfpel_h_add_c;
	interpolate8x8_halfpel_v_add = interpolate8x8_halfpel_v_add_c;
	interpolate8x8_halfpel_hv_add = interpolate8x8_halfpel_hv_add_c;

	interpolate16x16_lowpass_h = interpolate16x16_lowpass_h_c;
	interpolate16x16_lowpass_v = interpolate16x16_lowpass_v_c;
	interpolate16x16_lowpass_hv = interpolate16x16_lowpass_hv_c;

	interpolate8x8_lowpass_h = interpolate8x8_lowpass_h_c;
	interpolate8x8_lowpass_v = interpolate8x8_lowpass_v_c;
	interpolate8x8_lowpass_hv = interpolate8x8_lowpass_hv_c;

	interpolate8x8_6tap_lowpass_h = interpolate8x8_6tap_lowpass_h_c;
	interpolate8x8_6tap_lowpass_v = interpolate8x8_6tap_lowpass_v_c;

	interpolate8x8_avg2 = interpolate8x8_avg2_c;
	interpolate8x8_avg4 = interpolate8x8_avg4_c;

	/* postprocessing */
	image_brightness = image_brightness_c;

	/* Initialize internal colorspace transformation tables */
	colorspace_init();

	/* All colorspace transformation functions User Format->YV12 */
	yv12_to_yv12    = yv12_to_yv12_c;
	rgb555_to_yv12  = rgb555_to_yv12_c;
	rgb565_to_yv12  = rgb565_to_yv12_c;
	rgb_to_yv12     = rgb_to_yv12_c;
	bgr_to_yv12     = bgr_to_yv12_c;
	bgra_to_yv12    = bgra_to_yv12_c;
	abgr_to_yv12    = abgr_to_yv12_c;
	rgba_to_yv12    = rgba_to_yv12_c;
	argb_to_yv12    = argb_to_yv12_c;
	yuyv_to_yv12    = yuyv_to_yv12_c;
	uyvy_to_yv12    = uyvy_to_yv12_c;

	rgb555i_to_yv12 = rgb555i_to_yv12_c;
	rgb565i_to_yv12 = rgb565i_to_yv12_c;
	bgri_to_yv12    = bgri_to_yv12_c;
	bgrai_to_yv12   = bgrai_to_yv12_c;
	abgri_to_yv12   = abgri_to_yv12_c;
	rgbai_to_yv12   = rgbai_to_yv12_c;
	argbi_to_yv12   = argbi_to_yv12_c;
	yuyvi_to_yv12   = yuyvi_to_yv12_c;
	uyvyi_to_yv12   = uyvyi_to_yv12_c;

	/* All colorspace transformation functions YV12->User format */
	yv12_to_rgb555  = yv12_to_rgb555_c;
	//yv12_to_rgb565  = yv12_to_rgb565_c;
	yv12_to_rgb565  = yv12_to_rgb565_concept;
	yv12_to_rgb     = yv12_to_rgb_c;
	yv12_to_bgr     = yv12_to_bgr_c;
	yv12_to_bgra    = yv12_to_bgra_c;
	yv12_to_abgr    = yv12_to_abgr_c;
	yv12_to_rgba    = yv12_to_rgba_c;
	yv12_to_argb    = yv12_to_argb_c;
	yv12_to_yuyv    = yv12_to_yuyv_c;
	yv12_to_uyvy    = yv12_to_uyvy_c;

	yv12_to_rgb555i = yv12_to_rgb555i_c;
	yv12_to_rgb565i = yv12_to_rgb565i_c;
	yv12_to_bgri    = yv12_to_bgri_c;
	yv12_to_bgrai   = yv12_to_bgrai_c;
	yv12_to_abgri   = yv12_to_abgri_c;
	yv12_to_rgbai   = yv12_to_rgbai_c;
	yv12_to_argbi   = yv12_to_argbi_c;
	yv12_to_yuyvi   = yv12_to_yuyvi_c;
	yv12_to_uyvyi   = yv12_to_uyvyi_c;

	/* Functions used in motion estimation algorithms */
	calc_cbp      = calc_cbp_c;
	sad16         = sad16_c;
	sad8          = sad8_c;
	sad16bi       = sad16bi_c;
	sad8bi        = sad8bi_c;
	dev16         = dev16_c;
	sad16v        = sad16v_c;
	sse8_16bit    = sse8_16bit_c;
	sse8_8bit     = sse8_8bit_c;

	sseh8_16bit   = sseh8_16bit_c;
	coeff8_energy = coeff8_energy_c;
	blocksum8     = blocksum8_c;

	init_GMC(cpu_flags);

	#if defined(ARCH_IS_ARM) || defined(__arm__)
		if (cpu_flags & XVID_CPU_ASM) {
			
		}
	#endif

#if defined(_DEBUG)
    xvid_debug = init->debug;
#endif

    return(0);
}


static int
xvid_gbl_info(xvid_gbl_info_t * info)
{
	if (XVID_VERSION_MAJOR(info->version) != 1) /* v1.x.x */
		return XVID_ERR_VERSION;

	info->actual_version = XVID_VERSION;
	info->build = "xvid-1.3.7";
	info->cpu_flags = detect_cpu_flags();
	info->num_threads = 0; /* single-thread */

#if defined(_TINSPIRE)

  info->num_threads = 1;

#elif defined(_WIN32)

  {
	SYSTEM_INFO siSysInfo;
	GetSystemInfo(&siSysInfo);
	info->num_threads = siSysInfo.dwNumberOfProcessors; /* number of _logical_ cores */
  }

#elif defined(_SC_NPROCESSORS_CONF) /* should be available on Apple too actually */

  info->num_threads = sysconf(_SC_NPROCESSORS_CONF);	

#elif defined(__APPLE__) && defined(__MACH__)

  {
    size_t len;
    int    mib[2], ncpu;

    mib[0] = CTL_HW;
    mib[1] = HW_NCPU;
    len    = sizeof(ncpu);
    if (sysctl(mib, 2, &ncpu, &len, NULL, 0) == 0)
      info -> num_threads = ncpu;
    else
      info -> num_threads = 1;
  }

#elif defined(__amigaos4__)

  {
    uint32_t num_threads = 1;
    IExec->GetCPUInfoTags(GCIT_NumberOfCPUs, &num_threads, TAG_END);
    info->num_threads = num_threads;
  }

#endif

	return 0;
}


static int
xvid_gbl_convert(xvid_gbl_convert_t* convert)
{
	int width;
	int height;
	int width2;
	int height2;
	IMAGE img;

	if (XVID_VERSION_MAJOR(convert->version) != 1)   /* v1.x.x */
	      return XVID_ERR_VERSION;

#if 0
	const int flip1 = (convert->input.colorspace & XVID_CSP_VFLIP) ^ (convert->output.colorspace & XVID_CSP_VFLIP);
#endif
	width = convert->width;
	height = convert->height;
	width2 = convert->width/2;
	height2 = convert->height/2;

	switch (convert->input.csp & ~XVID_CSP_VFLIP)
	{
		case XVID_CSP_YV12 :
			img.y = convert->input.plane[0];
			img.v = (uint8_t*)convert->input.plane[0] + convert->input.stride[0]*height;
			img.u = (uint8_t*)convert->input.plane[0] + convert->input.stride[0]*height + (convert->input.stride[0]/2)*height2;
			image_output(&img, width, height, width,
						(uint8_t**)convert->output.plane, convert->output.stride,
						convert->output.csp, convert->interlacing);
			break;
		
		case XVID_CSP_INTERNAL :
			img.y = (uint8_t*)convert->input.plane[0];
			img.u = (uint8_t*)convert->input.plane[1];
			img.v = (uint8_t*)convert->input.plane[2];
			image_output(&img, width, height, convert->input.stride[0],
						(uint8_t**)convert->output.plane, convert->output.stride,
						convert->output.csp, convert->interlacing);
			break;

		default :
			return XVID_ERR_FORMAT;
	}


	emms();
	return 0;
}

/*****************************************************************************
 * Xvid Global Entry point
 *
 * Well this function initialize all internal function pointers according
 * to the CPU features forced by the library client or autodetected (depending
 * on the XVID_CPU_FORCE flag). It also initializes vlc coding tables and all
 * image colorspace transformation tables.
 *
 ****************************************************************************/


int
xvid_global(void *handle,
		  int opt,
		  void *param1,
		  void *param2)
{
	switch(opt)
	{
		case XVID_GBL_INIT :
			return xvid_gbl_init((xvid_gbl_init_t*)param1);

        case XVID_GBL_INFO :
            return xvid_gbl_info((xvid_gbl_info_t*)param1);

		case XVID_GBL_CONVERT :
			return xvid_gbl_convert((xvid_gbl_convert_t*)param1);

		default :
			return XVID_ERR_FAIL;
	}
}

/*****************************************************************************
 * Xvid Native decoder entry point
 *
 * This function is just a wrapper to all the option cases.
 *
 * Returned values : XVID_ERR_FAIL when opt is invalid
 *                   else returns the wrapped function result
 *
 ****************************************************************************/

int
xvid_decore(void *handle,
			int opt,
			void *param1,
			void *param2)
{
	switch (opt) {
	case XVID_DEC_CREATE:
		return decoder_create((xvid_dec_create_t *) param1);

	case XVID_DEC_DESTROY:
		return decoder_destroy((DECODER *) handle);

	case XVID_DEC_DECODE:
		return decoder_decode((DECODER *) handle, (xvid_dec_frame_t *) param1, (xvid_dec_stats_t*) param2);

	default:
		return XVID_ERR_FAIL;
	}
}


/*****************************************************************************
 * Xvid Native encoder entry point
 *
 * This function is just a wrapper to all the option cases.
 *
 * Returned values : XVID_ERR_FAIL when opt is invalid
 *                   else returns the wrapped function result
 *
 ****************************************************************************/

int
xvid_encore(void *handle,
			int opt,
			void *param1,
			void *param2)
{
#ifdef XVID_DECODER_ONLY
	(void)handle;
	(void)opt;
	(void)param1;
	(void)param2;
	return XVID_ERR_FAIL;
#else
	switch (opt) {
	case XVID_ENC_ENCODE:

		return enc_encode((Encoder *) handle,
							  (xvid_enc_frame_t *) param1,
							  (xvid_enc_stats_t *) param2);

	case XVID_ENC_CREATE:
		return enc_create((xvid_enc_create_t *) param1);

	case XVID_ENC_DESTROY:
		return enc_destroy((Encoder *) handle);

	default:
		return XVID_ERR_FAIL;
	}
#endif
}
