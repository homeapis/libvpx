/*
 * Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <math.h>
#include <stdio.h>
#include <limits.h>

#include "./vpx_config.h"

#include "vp10/common/alloccommon.h"
#include "vp10/common/filter.h"
#include "vp10/common/idct.h"
#if CONFIG_VP9_POSTPROC
#include "vp10/common/postproc.h"
#endif
#include "vp10/common/reconinter.h"
#include "vp10/common/reconintra.h"
#include "vp10/common/tile_common.h"

#include "vp10/encoder/aq_complexity.h"
#include "vp10/encoder/aq_cyclicrefresh.h"
#include "vp10/encoder/aq_variance.h"
#include "vp10/encoder/bitstream.h"
#include "vp10/encoder/context_tree.h"
#include "vp10/encoder/encodeframe.h"
#include "vp10/encoder/encodemv.h"
#include "vp10/encoder/encoder.h"
#include "vp10/encoder/ethread.h"
#include "vp10/encoder/firstpass.h"
#include "vp10/encoder/mbgraph.h"
#include "vp10/encoder/picklpf.h"
#if CONFIG_LOOP_RESTORATION
#include "vp10/encoder/pickrst.h"
#endif  // CONFIG_LOOP_RESTORATION
#include "vp10/encoder/ratectrl.h"
#include "vp10/encoder/rd.h"
#include "vp10/encoder/resize.h"
#include "vp10/encoder/segmentation.h"
#include "vp10/encoder/skin_detection.h"
#include "vp10/encoder/speed_features.h"
#include "vp10/encoder/temporal_filter.h"

#include "./vp10_rtcd.h"
#include "./vpx_dsp_rtcd.h"
#include "./vpx_scale_rtcd.h"
#include "vpx_dsp/psnr.h"
#if CONFIG_INTERNAL_STATS
#include "vpx_dsp/ssim.h"
#endif
#include "vpx_dsp/vpx_dsp_common.h"
#include "vpx_dsp/vpx_filter.h"
#include "vpx_ports/mem.h"
#include "vpx_ports/system_state.h"
#include "vpx_ports/vpx_timer.h"
#include "vpx_scale/vpx_scale.h"

#define AM_SEGMENT_ID_INACTIVE 7
#define AM_SEGMENT_ID_ACTIVE 0

#define SHARP_FILTER_QTHRESH 0          /* Q threshold for 8-tap sharp filter */

#define ALTREF_HIGH_PRECISION_MV 1      // Whether to use high precision mv
                                         //  for altref computation.
#define HIGH_PRECISION_MV_QTHRESH 200   // Q threshold for high precision
                                         // mv. Choose a very high value for
                                         // now so that HIGH_PRECISION is always
                                         // chosen.
// #define OUTPUT_YUV_REC

#ifdef OUTPUT_YUV_DENOISED
FILE *yuv_denoised_file = NULL;
#endif
#ifdef OUTPUT_YUV_SKINMAP
FILE *yuv_skinmap_file = NULL;
#endif
#ifdef OUTPUT_YUV_REC
FILE *yuv_rec_file;
#endif

#if 0
FILE *framepsnr;
FILE *kf_list;
FILE *keyfile;
#endif

static INLINE void Scale2Ratio(VPX_SCALING mode, int *hr, int *hs) {
  switch (mode) {
    case NORMAL:
      *hr = 1;
      *hs = 1;
      break;
    case FOURFIVE:
      *hr = 4;
      *hs = 5;
      break;
    case THREEFIVE:
      *hr = 3;
      *hs = 5;
    break;
    case ONETWO:
      *hr = 1;
      *hs = 2;
    break;
    default:
      *hr = 1;
      *hs = 1;
       assert(0);
      break;
  }
}

// Mark all inactive blocks as active. Other segmentation features may be set
// so memset cannot be used, instead only inactive blocks should be reset.
static void suppress_active_map(VP10_COMP *cpi) {
  unsigned char *const seg_map = cpi->segmentation_map;
  int i;
  if (cpi->active_map.enabled || cpi->active_map.update)
    for (i = 0; i < cpi->common.mi_rows * cpi->common.mi_cols; ++i)
      if (seg_map[i] == AM_SEGMENT_ID_INACTIVE)
        seg_map[i] = AM_SEGMENT_ID_ACTIVE;
}

static void apply_active_map(VP10_COMP *cpi) {
  struct segmentation *const seg = &cpi->common.seg;
  unsigned char *const seg_map = cpi->segmentation_map;
  const unsigned char *const active_map = cpi->active_map.map;
  int i;

  assert(AM_SEGMENT_ID_ACTIVE == CR_SEGMENT_ID_BASE);

  if (frame_is_intra_only(&cpi->common)) {
    cpi->active_map.enabled = 0;
    cpi->active_map.update = 1;
  }

  if (cpi->active_map.update) {
    if (cpi->active_map.enabled) {
      for (i = 0; i < cpi->common.mi_rows * cpi->common.mi_cols; ++i)
        if (seg_map[i] == AM_SEGMENT_ID_ACTIVE) seg_map[i] = active_map[i];
      vp10_enable_segmentation(seg);
      vp10_enable_segfeature(seg, AM_SEGMENT_ID_INACTIVE, SEG_LVL_SKIP);
      vp10_enable_segfeature(seg, AM_SEGMENT_ID_INACTIVE, SEG_LVL_ALT_LF);
      // Setting the data to -MAX_LOOP_FILTER will result in the computed loop
      // filter level being zero regardless of the value of seg->abs_delta.
      vp10_set_segdata(seg, AM_SEGMENT_ID_INACTIVE,
                      SEG_LVL_ALT_LF, -MAX_LOOP_FILTER);
    } else {
      vp10_disable_segfeature(seg, AM_SEGMENT_ID_INACTIVE, SEG_LVL_SKIP);
      vp10_disable_segfeature(seg, AM_SEGMENT_ID_INACTIVE, SEG_LVL_ALT_LF);
      if (seg->enabled) {
        seg->update_data = 1;
        seg->update_map = 1;
      }
    }
    cpi->active_map.update = 0;
  }
}

int vp10_set_active_map(VP10_COMP* cpi,
                       unsigned char* new_map_16x16,
                       int rows,
                       int cols) {
  if (rows == cpi->common.mb_rows && cols == cpi->common.mb_cols) {
    unsigned char *const active_map_8x8 = cpi->active_map.map;
    const int mi_rows = cpi->common.mi_rows;
    const int mi_cols = cpi->common.mi_cols;
    cpi->active_map.update = 1;
    if (new_map_16x16) {
      int r, c;
      for (r = 0; r < mi_rows; ++r) {
        for (c = 0; c < mi_cols; ++c) {
          active_map_8x8[r * mi_cols + c] =
              new_map_16x16[(r >> 1) * cols + (c >> 1)]
                  ? AM_SEGMENT_ID_ACTIVE
                  : AM_SEGMENT_ID_INACTIVE;
        }
      }
      cpi->active_map.enabled = 1;
    } else {
      cpi->active_map.enabled = 0;
    }
    return 0;
  } else {
    return -1;
  }
}

int vp10_get_active_map(VP10_COMP* cpi,
                       unsigned char* new_map_16x16,
                       int rows,
                       int cols) {
  if (rows == cpi->common.mb_rows && cols == cpi->common.mb_cols &&
      new_map_16x16) {
    unsigned char* const seg_map_8x8 = cpi->segmentation_map;
    const int mi_rows = cpi->common.mi_rows;
    const int mi_cols = cpi->common.mi_cols;
    memset(new_map_16x16, !cpi->active_map.enabled, rows * cols);
    if (cpi->active_map.enabled) {
      int r, c;
      for (r = 0; r < mi_rows; ++r) {
        for (c = 0; c < mi_cols; ++c) {
          // Cyclic refresh segments are considered active despite not having
          // AM_SEGMENT_ID_ACTIVE
          new_map_16x16[(r >> 1) * cols + (c >> 1)] |=
              seg_map_8x8[r * mi_cols + c] != AM_SEGMENT_ID_INACTIVE;
        }
      }
    }
    return 0;
  } else {
    return -1;
  }
}

void vp10_set_high_precision_mv(VP10_COMP *cpi, int allow_high_precision_mv) {
  MACROBLOCK *const mb = &cpi->td.mb;
  cpi->common.allow_high_precision_mv = allow_high_precision_mv;

#if CONFIG_REF_MV
  if (cpi->common.allow_high_precision_mv) {
    int i;
    for (i = 0; i < NMV_CONTEXTS; ++i) {
      mb->mv_cost_stack[i] = mb->nmvcost_hp[i];
      mb->mvsadcost = mb->nmvsadcost_hp;
    }
  } else {
    int i;
    for (i = 0; i < NMV_CONTEXTS; ++i) {
      mb->mv_cost_stack[i] = mb->nmvcost[i];
      mb->mvsadcost = mb->nmvsadcost;
    }
  }
#else
  if (cpi->common.allow_high_precision_mv) {
    mb->mvcost = mb->nmvcost_hp;
    mb->mvsadcost = mb->nmvsadcost_hp;
  } else {
    mb->mvcost = mb->nmvcost;
    mb->mvsadcost = mb->nmvsadcost;
  }
#endif
}

static void setup_frame(VP10_COMP *cpi) {
  VP10_COMMON *const cm = &cpi->common;
  // Set up entropy context depending on frame type. The decoder mandates
  // the use of the default context, index 0, for keyframes and inter
  // frames where the error_resilient_mode or intra_only flag is set. For
  // other inter-frames the encoder currently uses only two contexts;
  // context 1 for ALTREF frames and context 0 for the others.
  if (frame_is_intra_only(cm) || cm->error_resilient_mode) {
    vp10_setup_past_independence(cm);
  } else {
    cm->frame_context_idx = cpi->refresh_alt_ref_frame;
  }

  if (cm->frame_type == KEY_FRAME) {
    cpi->refresh_golden_frame = 1;
    cpi->refresh_alt_ref_frame = 1;
    vp10_zero(cpi->interp_filter_selected);
  } else {
    *cm->fc = cm->frame_contexts[cm->frame_context_idx];
    vp10_zero(cpi->interp_filter_selected[0]);
  }
}

static void vp10_enc_setup_mi(VP10_COMMON *cm) {
  int i;
  cm->mi = cm->mip + cm->mi_stride + 1;
  memset(cm->mip, 0, cm->mi_stride * (cm->mi_rows + 1) * sizeof(*cm->mip));
  cm->prev_mi = cm->prev_mip + cm->mi_stride + 1;
  // Clear top border row
  memset(cm->prev_mip, 0, sizeof(*cm->prev_mip) * cm->mi_stride);
  // Clear left border column
  for (i = 1; i < cm->mi_rows + 1; ++i)
    memset(&cm->prev_mip[i * cm->mi_stride], 0, sizeof(*cm->prev_mip));

  cm->mi_grid_visible = cm->mi_grid_base + cm->mi_stride + 1;
  cm->prev_mi_grid_visible = cm->prev_mi_grid_base + cm->mi_stride + 1;

  memset(cm->mi_grid_base, 0,
         cm->mi_stride * (cm->mi_rows + 1) * sizeof(*cm->mi_grid_base));
}

static int vp10_enc_alloc_mi(VP10_COMMON *cm, int mi_size) {
  cm->mip = vpx_calloc(mi_size, sizeof(*cm->mip));
  if (!cm->mip)
    return 1;
  cm->prev_mip = vpx_calloc(mi_size, sizeof(*cm->prev_mip));
  if (!cm->prev_mip)
    return 1;
  cm->mi_alloc_size = mi_size;

  cm->mi_grid_base = (MODE_INFO **)vpx_calloc(mi_size, sizeof(MODE_INFO*));
  if (!cm->mi_grid_base)
    return 1;
  cm->prev_mi_grid_base = (MODE_INFO **)vpx_calloc(mi_size, sizeof(MODE_INFO*));
  if (!cm->prev_mi_grid_base)
    return 1;

  return 0;
}

static void vp10_enc_free_mi(VP10_COMMON *cm) {
  vpx_free(cm->mip);
  cm->mip = NULL;
  vpx_free(cm->prev_mip);
  cm->prev_mip = NULL;
  vpx_free(cm->mi_grid_base);
  cm->mi_grid_base = NULL;
  vpx_free(cm->prev_mi_grid_base);
  cm->prev_mi_grid_base = NULL;
}

static void vp10_swap_mi_and_prev_mi(VP10_COMMON *cm) {
  // Current mip will be the prev_mip for the next frame.
  MODE_INFO **temp_base = cm->prev_mi_grid_base;
  MODE_INFO *temp = cm->prev_mip;
  cm->prev_mip = cm->mip;
  cm->mip = temp;

  // Update the upper left visible macroblock ptrs.
  cm->mi = cm->mip + cm->mi_stride + 1;
  cm->prev_mi = cm->prev_mip + cm->mi_stride + 1;

  cm->prev_mi_grid_base = cm->mi_grid_base;
  cm->mi_grid_base = temp_base;
  cm->mi_grid_visible = cm->mi_grid_base + cm->mi_stride + 1;
  cm->prev_mi_grid_visible = cm->prev_mi_grid_base + cm->mi_stride + 1;
}

void vp10_initialize_enc(void) {
  static volatile int init_done = 0;

  if (!init_done) {
    vp10_rtcd();
    vpx_dsp_rtcd();
    vpx_scale_rtcd();
    vp10_init_intra_predictors();
    vp10_init_me_luts();
    vp10_rc_init_minq_luts();
    vp10_entropy_mv_init();
    vp10_temporal_filter_init();
    vp10_encode_token_init();
#if CONFIG_EXT_INTER
    vp10_init_wedge_masks();
#endif
    init_done = 1;
  }
}

static void dealloc_compressor_data(VP10_COMP *cpi) {
  VP10_COMMON *const cm = &cpi->common;
  int i;

  vpx_free(cpi->mbmi_ext_base);
  cpi->mbmi_ext_base = NULL;

  vpx_free(cpi->tile_data);
  cpi->tile_data = NULL;

  // Delete sementation map
  vpx_free(cpi->segmentation_map);
  cpi->segmentation_map = NULL;
  vpx_free(cpi->coding_context.last_frame_seg_map_copy);
  cpi->coding_context.last_frame_seg_map_copy = NULL;

#if CONFIG_REF_MV
  for (i = 0; i < NMV_CONTEXTS; ++i) {
    vpx_free(cpi->nmv_costs[i][0]);
    vpx_free(cpi->nmv_costs[i][1]);
    vpx_free(cpi->nmv_costs_hp[i][0]);
    vpx_free(cpi->nmv_costs_hp[i][1]);
    cpi->nmv_costs[i][0] = NULL;
    cpi->nmv_costs[i][1] = NULL;
    cpi->nmv_costs_hp[i][0] = NULL;
    cpi->nmv_costs_hp[i][1] = NULL;
  }
#endif

  vpx_free(cpi->nmvcosts[0]);
  vpx_free(cpi->nmvcosts[1]);
  cpi->nmvcosts[0] = NULL;
  cpi->nmvcosts[1] = NULL;

  vpx_free(cpi->nmvcosts_hp[0]);
  vpx_free(cpi->nmvcosts_hp[1]);
  cpi->nmvcosts_hp[0] = NULL;
  cpi->nmvcosts_hp[1] = NULL;

  vpx_free(cpi->nmvsadcosts[0]);
  vpx_free(cpi->nmvsadcosts[1]);
  cpi->nmvsadcosts[0] = NULL;
  cpi->nmvsadcosts[1] = NULL;

  vpx_free(cpi->nmvsadcosts_hp[0]);
  vpx_free(cpi->nmvsadcosts_hp[1]);
  cpi->nmvsadcosts_hp[0] = NULL;
  cpi->nmvsadcosts_hp[1] = NULL;

  vp10_cyclic_refresh_free(cpi->cyclic_refresh);
  cpi->cyclic_refresh = NULL;

  vpx_free(cpi->active_map.map);
  cpi->active_map.map = NULL;

  // Free up-sampled reference buffers.
  for (i = 0; i < MAX_REF_FRAMES; i++)
    vpx_free_frame_buffer(&cpi->upsampled_ref_bufs[i].buf);

  vp10_free_ref_frame_buffers(cm->buffer_pool);
#if CONFIG_VP9_POSTPROC
  vp10_free_postproc_buffers(cm);
#endif  // CONFIG_VP9_POSTPROC
#if CONFIG_LOOP_RESTORATION
  vp10_free_restoration_buffers(cm);
#endif  // CONFIG_LOOP_RESTORATION
  vp10_free_context_buffers(cm);

  vpx_free_frame_buffer(&cpi->last_frame_uf);
#if CONFIG_LOOP_RESTORATION
  vpx_free_frame_buffer(&cpi->last_frame_db);
#endif  // CONFIG_LOOP_RESTORATION
  vpx_free_frame_buffer(&cpi->scaled_source);
  vpx_free_frame_buffer(&cpi->scaled_last_source);
  vpx_free_frame_buffer(&cpi->alt_ref_buffer);
  vp10_lookahead_destroy(cpi->lookahead);

  vpx_free(cpi->tile_tok[0][0]);
  cpi->tile_tok[0][0] = 0;

  vp10_free_pc_tree(&cpi->td);

  if (cpi->common.allow_screen_content_tools)
    vpx_free(cpi->td.mb.palette_buffer);

  if (cpi->source_diff_var != NULL) {
    vpx_free(cpi->source_diff_var);
    cpi->source_diff_var = NULL;
  }
}

static void save_coding_context(VP10_COMP *cpi) {
  CODING_CONTEXT *const cc = &cpi->coding_context;
  VP10_COMMON *cm = &cpi->common;
#if CONFIG_REF_MV
  int i;
#endif

  // Stores a snapshot of key state variables which can subsequently be
  // restored with a call to vp10_restore_coding_context. These functions are
  // intended for use in a re-code loop in vp10_compress_frame where the
  // quantizer value is adjusted between loop iterations.
#if CONFIG_REF_MV
  for (i = 0; i < NMV_CONTEXTS; ++i) {
    vp10_copy(cc->nmv_vec_cost[i], cpi->td.mb.nmv_vec_cost[i]);
    memcpy(cc->nmv_costs[i][0], cpi->nmv_costs[i][0],
           MV_VALS * sizeof(*cpi->nmv_costs[i][0]));
    memcpy(cc->nmv_costs[i][1], cpi->nmv_costs[i][1],
           MV_VALS * sizeof(*cpi->nmv_costs[i][1]));
    memcpy(cc->nmv_costs_hp[i][0], cpi->nmv_costs_hp[i][0],
           MV_VALS * sizeof(*cpi->nmv_costs_hp[i][0]));
    memcpy(cc->nmv_costs_hp[i][1], cpi->nmv_costs_hp[i][1],
           MV_VALS * sizeof(*cpi->nmv_costs_hp[i][1]));
  }
#else
  vp10_copy(cc->nmvjointcost,  cpi->td.mb.nmvjointcost);
#endif

  memcpy(cc->nmvcosts[0], cpi->nmvcosts[0],
         MV_VALS * sizeof(*cpi->nmvcosts[0]));
  memcpy(cc->nmvcosts[1], cpi->nmvcosts[1],
         MV_VALS * sizeof(*cpi->nmvcosts[1]));
  memcpy(cc->nmvcosts_hp[0], cpi->nmvcosts_hp[0],
         MV_VALS * sizeof(*cpi->nmvcosts_hp[0]));
  memcpy(cc->nmvcosts_hp[1], cpi->nmvcosts_hp[1],
         MV_VALS * sizeof(*cpi->nmvcosts_hp[1]));

  memcpy(cpi->coding_context.last_frame_seg_map_copy,
         cm->last_frame_seg_map, (cm->mi_rows * cm->mi_cols));

  vp10_copy(cc->last_ref_lf_deltas, cm->lf.last_ref_deltas);
  vp10_copy(cc->last_mode_lf_deltas, cm->lf.last_mode_deltas);

  cc->fc = *cm->fc;
}

static void restore_coding_context(VP10_COMP *cpi) {
  CODING_CONTEXT *const cc = &cpi->coding_context;
  VP10_COMMON *cm = &cpi->common;
#if CONFIG_REF_MV
  int i;
#endif

  // Restore key state variables to the snapshot state stored in the
  // previous call to vp10_save_coding_context.
#if CONFIG_REF_MV
  for (i = 0; i < NMV_CONTEXTS; ++i) {
    vp10_copy(cpi->td.mb.nmv_vec_cost[i], cc->nmv_vec_cost[i]);
    memcpy(cpi->nmv_costs[i][0], cc->nmv_costs[i][0],
           MV_VALS * sizeof(*cc->nmv_costs[i][0]));
    memcpy(cpi->nmv_costs[i][1], cc->nmv_costs[i][1],
           MV_VALS * sizeof(*cc->nmv_costs[i][1]));
    memcpy(cpi->nmv_costs_hp[i][0], cc->nmv_costs_hp[i][0],
           MV_VALS * sizeof(*cc->nmv_costs_hp[i][0]));
    memcpy(cpi->nmv_costs_hp[i][1], cc->nmv_costs_hp[i][1],
           MV_VALS * sizeof(*cc->nmv_costs_hp[i][1]));
  }
#else
  vp10_copy(cpi->td.mb.nmvjointcost, cc->nmvjointcost);
#endif

  memcpy(cpi->nmvcosts[0], cc->nmvcosts[0], MV_VALS * sizeof(*cc->nmvcosts[0]));
  memcpy(cpi->nmvcosts[1], cc->nmvcosts[1], MV_VALS * sizeof(*cc->nmvcosts[1]));
  memcpy(cpi->nmvcosts_hp[0], cc->nmvcosts_hp[0],
         MV_VALS * sizeof(*cc->nmvcosts_hp[0]));
  memcpy(cpi->nmvcosts_hp[1], cc->nmvcosts_hp[1],
         MV_VALS * sizeof(*cc->nmvcosts_hp[1]));

  memcpy(cm->last_frame_seg_map,
         cpi->coding_context.last_frame_seg_map_copy,
         (cm->mi_rows * cm->mi_cols));

  vp10_copy(cm->lf.last_ref_deltas, cc->last_ref_lf_deltas);
  vp10_copy(cm->lf.last_mode_deltas, cc->last_mode_lf_deltas);

  *cm->fc = cc->fc;
}

static void configure_static_seg_features(VP10_COMP *cpi) {
  VP10_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *const rc = &cpi->rc;
  struct segmentation *const seg = &cm->seg;

  int high_q = (int)(rc->avg_q > 48.0);
  int qi_delta;

  // Disable and clear down for KF
  if (cm->frame_type == KEY_FRAME) {
    // Clear down the global segmentation map
    memset(cpi->segmentation_map, 0, cm->mi_rows * cm->mi_cols);
    seg->update_map = 0;
    seg->update_data = 0;
    cpi->static_mb_pct = 0;

    // Disable segmentation
    vp10_disable_segmentation(seg);

    // Clear down the segment features.
    vp10_clearall_segfeatures(seg);
  } else if (cpi->refresh_alt_ref_frame) {
    // If this is an alt ref frame
    // Clear down the global segmentation map
    memset(cpi->segmentation_map, 0, cm->mi_rows * cm->mi_cols);
    seg->update_map = 0;
    seg->update_data = 0;
    cpi->static_mb_pct = 0;

    // Disable segmentation and individual segment features by default
    vp10_disable_segmentation(seg);
    vp10_clearall_segfeatures(seg);

    // Scan frames from current to arf frame.
    // This function re-enables segmentation if appropriate.
    vp10_update_mbgraph_stats(cpi);

    // If segmentation was enabled set those features needed for the
    // arf itself.
    if (seg->enabled) {
      seg->update_map = 1;
      seg->update_data = 1;

      qi_delta = vp10_compute_qdelta(rc, rc->avg_q, rc->avg_q * 0.875,
                                    cm->bit_depth);
      vp10_set_segdata(seg, 1, SEG_LVL_ALT_Q, qi_delta - 2);
      vp10_set_segdata(seg, 1, SEG_LVL_ALT_LF, -2);

      vp10_enable_segfeature(seg, 1, SEG_LVL_ALT_Q);
      vp10_enable_segfeature(seg, 1, SEG_LVL_ALT_LF);

      // Where relevant assume segment data is delta data
      seg->abs_delta = SEGMENT_DELTADATA;
    }
  } else if (seg->enabled) {
    // All other frames if segmentation has been enabled

    // First normal frame in a valid gf or alt ref group
    if (rc->frames_since_golden == 0) {
      // Set up segment features for normal frames in an arf group
      if (rc->source_alt_ref_active) {
        seg->update_map = 0;
        seg->update_data = 1;
        seg->abs_delta = SEGMENT_DELTADATA;

        qi_delta = vp10_compute_qdelta(rc, rc->avg_q, rc->avg_q * 1.125,
                                      cm->bit_depth);
        vp10_set_segdata(seg, 1, SEG_LVL_ALT_Q, qi_delta + 2);
        vp10_enable_segfeature(seg, 1, SEG_LVL_ALT_Q);

        vp10_set_segdata(seg, 1, SEG_LVL_ALT_LF, -2);
        vp10_enable_segfeature(seg, 1, SEG_LVL_ALT_LF);

        // Segment coding disabled for compred testing
        if (high_q || (cpi->static_mb_pct == 100)) {
          vp10_set_segdata(seg, 1, SEG_LVL_REF_FRAME, ALTREF_FRAME);
          vp10_enable_segfeature(seg, 1, SEG_LVL_REF_FRAME);
          vp10_enable_segfeature(seg, 1, SEG_LVL_SKIP);
        }
      } else {
        // Disable segmentation and clear down features if alt ref
        // is not active for this group

        vp10_disable_segmentation(seg);

        memset(cpi->segmentation_map, 0, cm->mi_rows * cm->mi_cols);

        seg->update_map = 0;
        seg->update_data = 0;

        vp10_clearall_segfeatures(seg);
      }
    } else if (rc->is_src_frame_alt_ref) {
      // Special case where we are coding over the top of a previous
      // alt ref frame.
      // Segment coding disabled for compred testing

      // Enable ref frame features for segment 0 as well
      vp10_enable_segfeature(seg, 0, SEG_LVL_REF_FRAME);
      vp10_enable_segfeature(seg, 1, SEG_LVL_REF_FRAME);

      // All mbs should use ALTREF_FRAME
      vp10_clear_segdata(seg, 0, SEG_LVL_REF_FRAME);
      vp10_set_segdata(seg, 0, SEG_LVL_REF_FRAME, ALTREF_FRAME);
      vp10_clear_segdata(seg, 1, SEG_LVL_REF_FRAME);
      vp10_set_segdata(seg, 1, SEG_LVL_REF_FRAME, ALTREF_FRAME);

      // Skip all MBs if high Q (0,0 mv and skip coeffs)
      if (high_q) {
        vp10_enable_segfeature(seg, 0, SEG_LVL_SKIP);
        vp10_enable_segfeature(seg, 1, SEG_LVL_SKIP);
      }
      // Enable data update
      seg->update_data = 1;
    } else {
      // All other frames.

      // No updates.. leave things as they are.
      seg->update_map = 0;
      seg->update_data = 0;
    }
  }
}

static void update_reference_segmentation_map(VP10_COMP *cpi) {
  VP10_COMMON *const cm = &cpi->common;
  MODE_INFO **mi_8x8_ptr = cm->mi_grid_visible;
  uint8_t *cache_ptr = cm->last_frame_seg_map;
  int row, col;

  for (row = 0; row < cm->mi_rows; row++) {
    MODE_INFO **mi_8x8 = mi_8x8_ptr;
    uint8_t *cache = cache_ptr;
    for (col = 0; col < cm->mi_cols; col++, mi_8x8++, cache++)
      cache[0] = mi_8x8[0]->mbmi.segment_id;
    mi_8x8_ptr += cm->mi_stride;
    cache_ptr += cm->mi_cols;
  }
}

static void alloc_raw_frame_buffers(VP10_COMP *cpi) {
  VP10_COMMON *cm = &cpi->common;
  const VP10EncoderConfig *oxcf = &cpi->oxcf;

  if (!cpi->lookahead)
    cpi->lookahead = vp10_lookahead_init(oxcf->width, oxcf->height,
                                        cm->subsampling_x, cm->subsampling_y,
#if CONFIG_VP9_HIGHBITDEPTH
                                      cm->use_highbitdepth,
#endif
                                      oxcf->lag_in_frames);
  if (!cpi->lookahead)
    vpx_internal_error(&cm->error, VPX_CODEC_MEM_ERROR,
                       "Failed to allocate lag buffers");

  // TODO(agrange) Check if ARF is enabled and skip allocation if not.
  if (vpx_realloc_frame_buffer(&cpi->alt_ref_buffer,
                               oxcf->width, oxcf->height,
                               cm->subsampling_x, cm->subsampling_y,
#if CONFIG_VP9_HIGHBITDEPTH
                               cm->use_highbitdepth,
#endif
                               VP9_ENC_BORDER_IN_PIXELS, cm->byte_alignment,
                               NULL, NULL, NULL))
    vpx_internal_error(&cm->error, VPX_CODEC_MEM_ERROR,
                       "Failed to allocate altref buffer");
}

static void alloc_util_frame_buffers(VP10_COMP *cpi) {
  VP10_COMMON *const cm = &cpi->common;
  if (vpx_realloc_frame_buffer(&cpi->last_frame_uf,
                               cm->width, cm->height,
                               cm->subsampling_x, cm->subsampling_y,
#if CONFIG_VP9_HIGHBITDEPTH
                               cm->use_highbitdepth,
#endif
                               VP9_ENC_BORDER_IN_PIXELS, cm->byte_alignment,
                               NULL, NULL, NULL))
    vpx_internal_error(&cm->error, VPX_CODEC_MEM_ERROR,
                       "Failed to allocate last frame buffer");

#if CONFIG_LOOP_RESTORATION
  if (vpx_realloc_frame_buffer(&cpi->last_frame_db,
                               cm->width, cm->height,
                               cm->subsampling_x, cm->subsampling_y,
#if CONFIG_VP9_HIGHBITDEPTH
                               cm->use_highbitdepth,
#endif
                               VP9_ENC_BORDER_IN_PIXELS, cm->byte_alignment,
                               NULL, NULL, NULL))
    vpx_internal_error(&cm->error, VPX_CODEC_MEM_ERROR,
                       "Failed to allocate last frame deblocked buffer");
#endif  // CONFIG_LOOP_RESTORATION

  if (vpx_realloc_frame_buffer(&cpi->scaled_source,
                               cm->width, cm->height,
                               cm->subsampling_x, cm->subsampling_y,
#if CONFIG_VP9_HIGHBITDEPTH
                               cm->use_highbitdepth,
#endif
                               VP9_ENC_BORDER_IN_PIXELS, cm->byte_alignment,
                               NULL, NULL, NULL))
    vpx_internal_error(&cm->error, VPX_CODEC_MEM_ERROR,
                       "Failed to allocate scaled source buffer");

  if (vpx_realloc_frame_buffer(&cpi->scaled_last_source,
                               cm->width, cm->height,
                               cm->subsampling_x, cm->subsampling_y,
#if CONFIG_VP9_HIGHBITDEPTH
                               cm->use_highbitdepth,
#endif
                               VP9_ENC_BORDER_IN_PIXELS, cm->byte_alignment,
                               NULL, NULL, NULL))
    vpx_internal_error(&cm->error, VPX_CODEC_MEM_ERROR,
                       "Failed to allocate scaled last source buffer");
}


static int alloc_context_buffers_ext(VP10_COMP *cpi) {
  VP10_COMMON *cm = &cpi->common;
  int mi_size = cm->mi_cols * cm->mi_rows;

  cpi->mbmi_ext_base = vpx_calloc(mi_size, sizeof(*cpi->mbmi_ext_base));
  if (!cpi->mbmi_ext_base)
    return 1;

  return 0;
}

void vp10_alloc_compressor_data(VP10_COMP *cpi) {
  VP10_COMMON *cm = &cpi->common;

  vp10_alloc_context_buffers(cm, cm->width, cm->height);

  alloc_context_buffers_ext(cpi);

  vpx_free(cpi->tile_tok[0][0]);

  {
    unsigned int tokens = get_token_alloc(cm->mb_rows, cm->mb_cols);
    CHECK_MEM_ERROR(cm, cpi->tile_tok[0][0],
        vpx_calloc(tokens, sizeof(*cpi->tile_tok[0][0])));
  }

  vp10_setup_pc_tree(&cpi->common, &cpi->td);
}

void vp10_new_framerate(VP10_COMP *cpi, double framerate) {
  cpi->framerate = framerate < 0.1 ? 30 : framerate;
  vp10_rc_update_framerate(cpi);
}

static void set_tile_limits(VP10_COMP *cpi) {
  VP10_COMMON *const cm = &cpi->common;

  int min_log2_tile_cols, max_log2_tile_cols;
  vp10_get_tile_n_bits(cm->mi_cols, &min_log2_tile_cols, &max_log2_tile_cols);

  cm->log2_tile_cols = clamp(cpi->oxcf.tile_columns,
                             min_log2_tile_cols, max_log2_tile_cols);
  cm->log2_tile_rows = cpi->oxcf.tile_rows;
}

static void update_frame_size(VP10_COMP *cpi) {
  VP10_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;

  vp10_set_mb_mi(cm, cm->width, cm->height);
  vp10_init_context_buffers(cm);
  vp10_init_macroblockd(cm, xd, NULL);
  memset(cpi->mbmi_ext_base, 0,
         cm->mi_rows * cm->mi_cols * sizeof(*cpi->mbmi_ext_base));

  set_tile_limits(cpi);
}

static void init_buffer_indices(VP10_COMP *cpi) {
#if CONFIG_EXT_REFS
  int fb_idx;
  for (fb_idx = 0; fb_idx < LAST_REF_FRAMES; ++fb_idx)
    cpi->lst_fb_idxes[fb_idx] = fb_idx;
  cpi->gld_fb_idx = LAST_REF_FRAMES;
  cpi->alt_fb_idx = cpi->gld_fb_idx + 1;
#else
  cpi->lst_fb_idx = 0;
  cpi->gld_fb_idx = 1;
  cpi->alt_fb_idx = 2;
#endif  // CONFIG_EXT_REFS
}

static void init_config(struct VP10_COMP *cpi, VP10EncoderConfig *oxcf) {
  VP10_COMMON *const cm = &cpi->common;

  cpi->oxcf = *oxcf;
  cpi->framerate = oxcf->init_framerate;

  cm->profile = oxcf->profile;
  cm->bit_depth = oxcf->bit_depth;
#if CONFIG_VP9_HIGHBITDEPTH
  cm->use_highbitdepth = oxcf->use_highbitdepth;
#endif
  cm->color_space = oxcf->color_space;
  cm->color_range = oxcf->color_range;

  cm->width = oxcf->width;
  cm->height = oxcf->height;
  vp10_alloc_compressor_data(cpi);

  // Single thread case: use counts in common.
  cpi->td.counts = &cm->counts;

  // change includes all joint functionality
#if CONFIG_EXT_REFS
  cpi->last_ref_to_refresh = LAST_FRAME;
#endif  // CONFIG_EXT_REFS

  vp10_change_config(cpi, oxcf);

  cpi->static_mb_pct = 0;
  cpi->ref_frame_flags = 0;

  init_buffer_indices(cpi);
}

static void set_rc_buffer_sizes(RATE_CONTROL *rc,
                                const VP10EncoderConfig *oxcf) {
  const int64_t bandwidth = oxcf->target_bandwidth;
  const int64_t starting = oxcf->starting_buffer_level_ms;
  const int64_t optimal = oxcf->optimal_buffer_level_ms;
  const int64_t maximum = oxcf->maximum_buffer_size_ms;

  rc->starting_buffer_level = starting * bandwidth / 1000;
  rc->optimal_buffer_level = (optimal == 0) ? bandwidth / 8
                                            : optimal * bandwidth / 1000;
  rc->maximum_buffer_size = (maximum == 0) ? bandwidth / 8
                                           : maximum * bandwidth / 1000;
}

#if CONFIG_VP9_HIGHBITDEPTH
#define HIGHBD_BFP(BT, SDF, SDAF, VF, SVF, SVAF, SDX3F, SDX8F, SDX4DF) \
    cpi->fn_ptr[BT].sdf = SDF; \
    cpi->fn_ptr[BT].sdaf = SDAF; \
    cpi->fn_ptr[BT].vf = VF; \
    cpi->fn_ptr[BT].svf = SVF; \
    cpi->fn_ptr[BT].svaf = SVAF; \
    cpi->fn_ptr[BT].sdx3f = SDX3F; \
    cpi->fn_ptr[BT].sdx8f = SDX8F; \
    cpi->fn_ptr[BT].sdx4df = SDX4DF;

#define MAKE_BFP_SAD_WRAPPER(fnname) \
static unsigned int fnname##_bits8(const uint8_t *src_ptr, \
                                   int source_stride, \
                                   const uint8_t *ref_ptr, \
                                   int ref_stride) {  \
  return fnname(src_ptr, source_stride, ref_ptr, ref_stride); \
} \
static unsigned int fnname##_bits10(const uint8_t *src_ptr, \
                                    int source_stride, \
                                    const uint8_t *ref_ptr, \
                                    int ref_stride) {  \
  return fnname(src_ptr, source_stride, ref_ptr, ref_stride) >> 2; \
} \
static unsigned int fnname##_bits12(const uint8_t *src_ptr, \
                                    int source_stride, \
                                    const uint8_t *ref_ptr, \
                                    int ref_stride) {  \
  return fnname(src_ptr, source_stride, ref_ptr, ref_stride) >> 4; \
}

#define MAKE_BFP_SADAVG_WRAPPER(fnname) static unsigned int \
fnname##_bits8(const uint8_t *src_ptr, \
               int source_stride, \
               const uint8_t *ref_ptr, \
               int ref_stride, \
               const uint8_t *second_pred) {  \
  return fnname(src_ptr, source_stride, ref_ptr, ref_stride, second_pred); \
} \
static unsigned int fnname##_bits10(const uint8_t *src_ptr, \
                                    int source_stride, \
                                    const uint8_t *ref_ptr, \
                                    int ref_stride, \
                                    const uint8_t *second_pred) {  \
  return fnname(src_ptr, source_stride, ref_ptr, ref_stride, \
                second_pred) >> 2; \
} \
static unsigned int fnname##_bits12(const uint8_t *src_ptr, \
                                    int source_stride, \
                                    const uint8_t *ref_ptr, \
                                    int ref_stride, \
                                    const uint8_t *second_pred) {  \
  return fnname(src_ptr, source_stride, ref_ptr, ref_stride, \
                second_pred) >> 4; \
}

#define MAKE_BFP_SAD3_WRAPPER(fnname) \
static void fnname##_bits8(const uint8_t *src_ptr, \
                           int source_stride, \
                           const uint8_t *ref_ptr, \
                           int  ref_stride, \
                           unsigned int *sad_array) {  \
  fnname(src_ptr, source_stride, ref_ptr, ref_stride, sad_array); \
} \
static void fnname##_bits10(const uint8_t *src_ptr, \
                            int source_stride, \
                            const uint8_t *ref_ptr, \
                            int  ref_stride, \
                            unsigned int *sad_array) {  \
  int i; \
  fnname(src_ptr, source_stride, ref_ptr, ref_stride, sad_array); \
  for (i = 0; i < 3; i++) \
    sad_array[i] >>= 2; \
} \
static void fnname##_bits12(const uint8_t *src_ptr, \
                            int source_stride, \
                            const uint8_t *ref_ptr, \
                            int  ref_stride, \
                            unsigned int *sad_array) {  \
  int i; \
  fnname(src_ptr, source_stride, ref_ptr, ref_stride, sad_array); \
  for (i = 0; i < 3; i++) \
    sad_array[i] >>= 4; \
}

#define MAKE_BFP_SAD8_WRAPPER(fnname) \
static void fnname##_bits8(const uint8_t *src_ptr, \
                           int source_stride, \
                           const uint8_t *ref_ptr, \
                           int  ref_stride, \
                           unsigned int *sad_array) {  \
  fnname(src_ptr, source_stride, ref_ptr, ref_stride, sad_array); \
} \
static void fnname##_bits10(const uint8_t *src_ptr, \
                            int source_stride, \
                            const uint8_t *ref_ptr, \
                            int  ref_stride, \
                            unsigned int *sad_array) {  \
  int i; \
  fnname(src_ptr, source_stride, ref_ptr, ref_stride, sad_array); \
  for (i = 0; i < 8; i++) \
    sad_array[i] >>= 2; \
} \
static void fnname##_bits12(const uint8_t *src_ptr, \
                            int source_stride, \
                            const uint8_t *ref_ptr, \
                            int  ref_stride, \
                            unsigned int *sad_array) {  \
  int i; \
  fnname(src_ptr, source_stride, ref_ptr, ref_stride, sad_array); \
  for (i = 0; i < 8; i++) \
    sad_array[i] >>= 4; \
}
#define MAKE_BFP_SAD4D_WRAPPER(fnname) \
static void fnname##_bits8(const uint8_t *src_ptr, \
                           int source_stride, \
                           const uint8_t* const ref_ptr[], \
                           int  ref_stride, \
                           unsigned int *sad_array) {  \
  fnname(src_ptr, source_stride, ref_ptr, ref_stride, sad_array); \
} \
static void fnname##_bits10(const uint8_t *src_ptr, \
                            int source_stride, \
                            const uint8_t* const ref_ptr[], \
                            int  ref_stride, \
                            unsigned int *sad_array) {  \
  int i; \
  fnname(src_ptr, source_stride, ref_ptr, ref_stride, sad_array); \
  for (i = 0; i < 4; i++) \
  sad_array[i] >>= 2; \
} \
static void fnname##_bits12(const uint8_t *src_ptr, \
                            int source_stride, \
                            const uint8_t* const ref_ptr[], \
                            int  ref_stride, \
                            unsigned int *sad_array) {  \
  int i; \
  fnname(src_ptr, source_stride, ref_ptr, ref_stride, sad_array); \
  for (i = 0; i < 4; i++) \
  sad_array[i] >>= 4; \
}

#if CONFIG_EXT_PARTITION
MAKE_BFP_SAD_WRAPPER(vpx_highbd_sad128x128)
MAKE_BFP_SADAVG_WRAPPER(vpx_highbd_sad128x128_avg)
MAKE_BFP_SAD3_WRAPPER(vpx_highbd_sad128x128x3)
MAKE_BFP_SAD8_WRAPPER(vpx_highbd_sad128x128x8)
MAKE_BFP_SAD4D_WRAPPER(vpx_highbd_sad128x128x4d)
MAKE_BFP_SAD_WRAPPER(vpx_highbd_sad128x64)
MAKE_BFP_SADAVG_WRAPPER(vpx_highbd_sad128x64_avg)
MAKE_BFP_SAD4D_WRAPPER(vpx_highbd_sad128x64x4d)
MAKE_BFP_SAD_WRAPPER(vpx_highbd_sad64x128)
MAKE_BFP_SADAVG_WRAPPER(vpx_highbd_sad64x128_avg)
MAKE_BFP_SAD4D_WRAPPER(vpx_highbd_sad64x128x4d)
#endif  // CONFIG_EXT_PARTITION
MAKE_BFP_SAD_WRAPPER(vpx_highbd_sad32x16)
MAKE_BFP_SADAVG_WRAPPER(vpx_highbd_sad32x16_avg)
MAKE_BFP_SAD4D_WRAPPER(vpx_highbd_sad32x16x4d)
MAKE_BFP_SAD_WRAPPER(vpx_highbd_sad16x32)
MAKE_BFP_SADAVG_WRAPPER(vpx_highbd_sad16x32_avg)
MAKE_BFP_SAD4D_WRAPPER(vpx_highbd_sad16x32x4d)
MAKE_BFP_SAD_WRAPPER(vpx_highbd_sad64x32)
MAKE_BFP_SADAVG_WRAPPER(vpx_highbd_sad64x32_avg)
MAKE_BFP_SAD4D_WRAPPER(vpx_highbd_sad64x32x4d)
MAKE_BFP_SAD_WRAPPER(vpx_highbd_sad32x64)
MAKE_BFP_SADAVG_WRAPPER(vpx_highbd_sad32x64_avg)
MAKE_BFP_SAD4D_WRAPPER(vpx_highbd_sad32x64x4d)
MAKE_BFP_SAD_WRAPPER(vpx_highbd_sad32x32)
MAKE_BFP_SADAVG_WRAPPER(vpx_highbd_sad32x32_avg)
MAKE_BFP_SAD3_WRAPPER(vpx_highbd_sad32x32x3)
MAKE_BFP_SAD8_WRAPPER(vpx_highbd_sad32x32x8)
MAKE_BFP_SAD4D_WRAPPER(vpx_highbd_sad32x32x4d)
MAKE_BFP_SAD_WRAPPER(vpx_highbd_sad64x64)
MAKE_BFP_SADAVG_WRAPPER(vpx_highbd_sad64x64_avg)
MAKE_BFP_SAD3_WRAPPER(vpx_highbd_sad64x64x3)
MAKE_BFP_SAD8_WRAPPER(vpx_highbd_sad64x64x8)
MAKE_BFP_SAD4D_WRAPPER(vpx_highbd_sad64x64x4d)
MAKE_BFP_SAD_WRAPPER(vpx_highbd_sad16x16)
MAKE_BFP_SADAVG_WRAPPER(vpx_highbd_sad16x16_avg)
MAKE_BFP_SAD3_WRAPPER(vpx_highbd_sad16x16x3)
MAKE_BFP_SAD8_WRAPPER(vpx_highbd_sad16x16x8)
MAKE_BFP_SAD4D_WRAPPER(vpx_highbd_sad16x16x4d)
MAKE_BFP_SAD_WRAPPER(vpx_highbd_sad16x8)
MAKE_BFP_SADAVG_WRAPPER(vpx_highbd_sad16x8_avg)
MAKE_BFP_SAD3_WRAPPER(vpx_highbd_sad16x8x3)
MAKE_BFP_SAD8_WRAPPER(vpx_highbd_sad16x8x8)
MAKE_BFP_SAD4D_WRAPPER(vpx_highbd_sad16x8x4d)
MAKE_BFP_SAD_WRAPPER(vpx_highbd_sad8x16)
MAKE_BFP_SADAVG_WRAPPER(vpx_highbd_sad8x16_avg)
MAKE_BFP_SAD3_WRAPPER(vpx_highbd_sad8x16x3)
MAKE_BFP_SAD8_WRAPPER(vpx_highbd_sad8x16x8)
MAKE_BFP_SAD4D_WRAPPER(vpx_highbd_sad8x16x4d)
MAKE_BFP_SAD_WRAPPER(vpx_highbd_sad8x8)
MAKE_BFP_SADAVG_WRAPPER(vpx_highbd_sad8x8_avg)
MAKE_BFP_SAD3_WRAPPER(vpx_highbd_sad8x8x3)
MAKE_BFP_SAD8_WRAPPER(vpx_highbd_sad8x8x8)
MAKE_BFP_SAD4D_WRAPPER(vpx_highbd_sad8x8x4d)
MAKE_BFP_SAD_WRAPPER(vpx_highbd_sad8x4)
MAKE_BFP_SADAVG_WRAPPER(vpx_highbd_sad8x4_avg)
MAKE_BFP_SAD8_WRAPPER(vpx_highbd_sad8x4x8)
MAKE_BFP_SAD4D_WRAPPER(vpx_highbd_sad8x4x4d)
MAKE_BFP_SAD_WRAPPER(vpx_highbd_sad4x8)
MAKE_BFP_SADAVG_WRAPPER(vpx_highbd_sad4x8_avg)
MAKE_BFP_SAD8_WRAPPER(vpx_highbd_sad4x8x8)
MAKE_BFP_SAD4D_WRAPPER(vpx_highbd_sad4x8x4d)
MAKE_BFP_SAD_WRAPPER(vpx_highbd_sad4x4)
MAKE_BFP_SADAVG_WRAPPER(vpx_highbd_sad4x4_avg)
MAKE_BFP_SAD3_WRAPPER(vpx_highbd_sad4x4x3)
MAKE_BFP_SAD8_WRAPPER(vpx_highbd_sad4x4x8)
MAKE_BFP_SAD4D_WRAPPER(vpx_highbd_sad4x4x4d)

#if CONFIG_EXT_INTER
#define HIGHBD_MBFP(BT, MSDF, MVF, MSVF)         \
  cpi->fn_ptr[BT].msdf            = MSDF; \
  cpi->fn_ptr[BT].mvf             = MVF;  \
  cpi->fn_ptr[BT].msvf            = MSVF;

#define MAKE_MBFP_SAD_WRAPPER(fnname) \
static unsigned int fnname##_bits8(const uint8_t *src_ptr, \
                                   int source_stride, \
                                   const uint8_t *ref_ptr, \
                                   int ref_stride, \
                                   const uint8_t *m, \
                                   int m_stride) {  \
  return fnname(src_ptr, source_stride, ref_ptr, ref_stride, \
                m, m_stride); \
} \
static unsigned int fnname##_bits10(const uint8_t *src_ptr, \
                                    int source_stride, \
                                    const uint8_t *ref_ptr, \
                                    int ref_stride, \
                                    const uint8_t *m, \
                                    int m_stride) {  \
  return fnname(src_ptr, source_stride, ref_ptr, ref_stride, \
                m, m_stride) >> 2; \
} \
static unsigned int fnname##_bits12(const uint8_t *src_ptr, \
                                    int source_stride, \
                                    const uint8_t *ref_ptr, \
                                    int ref_stride, \
                                    const uint8_t *m, \
                                    int m_stride) {  \
  return fnname(src_ptr, source_stride, ref_ptr, ref_stride, \
                m, m_stride) >> 4; \
}

#if CONFIG_EXT_PARTITION
MAKE_MBFP_SAD_WRAPPER(vpx_highbd_masked_sad128x128)
MAKE_MBFP_SAD_WRAPPER(vpx_highbd_masked_sad128x64)
MAKE_MBFP_SAD_WRAPPER(vpx_highbd_masked_sad64x128)
#endif  // CONFIG_EXT_PARTITION
MAKE_MBFP_SAD_WRAPPER(vpx_highbd_masked_sad64x64)
MAKE_MBFP_SAD_WRAPPER(vpx_highbd_masked_sad64x32)
MAKE_MBFP_SAD_WRAPPER(vpx_highbd_masked_sad32x64)
MAKE_MBFP_SAD_WRAPPER(vpx_highbd_masked_sad32x32)
MAKE_MBFP_SAD_WRAPPER(vpx_highbd_masked_sad32x16)
MAKE_MBFP_SAD_WRAPPER(vpx_highbd_masked_sad16x32)
MAKE_MBFP_SAD_WRAPPER(vpx_highbd_masked_sad16x16)
MAKE_MBFP_SAD_WRAPPER(vpx_highbd_masked_sad16x8)
MAKE_MBFP_SAD_WRAPPER(vpx_highbd_masked_sad8x16)
MAKE_MBFP_SAD_WRAPPER(vpx_highbd_masked_sad8x8)
MAKE_MBFP_SAD_WRAPPER(vpx_highbd_masked_sad8x4)
MAKE_MBFP_SAD_WRAPPER(vpx_highbd_masked_sad4x8)
MAKE_MBFP_SAD_WRAPPER(vpx_highbd_masked_sad4x4)
#endif  // CONFIG_EXT_INTER

static void  highbd_set_var_fns(VP10_COMP *const cpi) {
  VP10_COMMON *const cm = &cpi->common;
  if (cm->use_highbitdepth) {
    switch (cm->bit_depth) {
      case VPX_BITS_8:
        HIGHBD_BFP(BLOCK_32X16,
                   vpx_highbd_sad32x16_bits8,
                   vpx_highbd_sad32x16_avg_bits8,
                   vpx_highbd_8_variance32x16,
                   vpx_highbd_8_sub_pixel_variance32x16,
                   vpx_highbd_8_sub_pixel_avg_variance32x16,
                   NULL,
                   NULL,
                   vpx_highbd_sad32x16x4d_bits8)

        HIGHBD_BFP(BLOCK_16X32,
                   vpx_highbd_sad16x32_bits8,
                   vpx_highbd_sad16x32_avg_bits8,
                   vpx_highbd_8_variance16x32,
                   vpx_highbd_8_sub_pixel_variance16x32,
                   vpx_highbd_8_sub_pixel_avg_variance16x32,
                   NULL,
                   NULL,
                   vpx_highbd_sad16x32x4d_bits8)

        HIGHBD_BFP(BLOCK_64X32,
                   vpx_highbd_sad64x32_bits8,
                   vpx_highbd_sad64x32_avg_bits8,
                   vpx_highbd_8_variance64x32,
                   vpx_highbd_8_sub_pixel_variance64x32,
                   vpx_highbd_8_sub_pixel_avg_variance64x32,
                   NULL,
                   NULL,
                   vpx_highbd_sad64x32x4d_bits8)

        HIGHBD_BFP(BLOCK_32X64,
                   vpx_highbd_sad32x64_bits8,
                   vpx_highbd_sad32x64_avg_bits8,
                   vpx_highbd_8_variance32x64,
                   vpx_highbd_8_sub_pixel_variance32x64,
                   vpx_highbd_8_sub_pixel_avg_variance32x64,
                   NULL,
                   NULL,
                   vpx_highbd_sad32x64x4d_bits8)

        HIGHBD_BFP(BLOCK_32X32,
                   vpx_highbd_sad32x32_bits8,
                   vpx_highbd_sad32x32_avg_bits8,
                   vpx_highbd_8_variance32x32,
                   vpx_highbd_8_sub_pixel_variance32x32,
                   vpx_highbd_8_sub_pixel_avg_variance32x32,
                   vpx_highbd_sad32x32x3_bits8,
                   vpx_highbd_sad32x32x8_bits8,
                   vpx_highbd_sad32x32x4d_bits8)

        HIGHBD_BFP(BLOCK_64X64,
                   vpx_highbd_sad64x64_bits8,
                   vpx_highbd_sad64x64_avg_bits8,
                   vpx_highbd_8_variance64x64,
                   vpx_highbd_8_sub_pixel_variance64x64,
                   vpx_highbd_8_sub_pixel_avg_variance64x64,
                   vpx_highbd_sad64x64x3_bits8,
                   vpx_highbd_sad64x64x8_bits8,
                   vpx_highbd_sad64x64x4d_bits8)

        HIGHBD_BFP(BLOCK_16X16,
                   vpx_highbd_sad16x16_bits8,
                   vpx_highbd_sad16x16_avg_bits8,
                   vpx_highbd_8_variance16x16,
                   vpx_highbd_8_sub_pixel_variance16x16,
                   vpx_highbd_8_sub_pixel_avg_variance16x16,
                   vpx_highbd_sad16x16x3_bits8,
                   vpx_highbd_sad16x16x8_bits8,
                   vpx_highbd_sad16x16x4d_bits8)

        HIGHBD_BFP(BLOCK_16X8,
                   vpx_highbd_sad16x8_bits8,
                   vpx_highbd_sad16x8_avg_bits8,
                   vpx_highbd_8_variance16x8,
                   vpx_highbd_8_sub_pixel_variance16x8,
                   vpx_highbd_8_sub_pixel_avg_variance16x8,
                   vpx_highbd_sad16x8x3_bits8,
                   vpx_highbd_sad16x8x8_bits8,
                   vpx_highbd_sad16x8x4d_bits8)

        HIGHBD_BFP(BLOCK_8X16,
                   vpx_highbd_sad8x16_bits8,
                   vpx_highbd_sad8x16_avg_bits8,
                   vpx_highbd_8_variance8x16,
                   vpx_highbd_8_sub_pixel_variance8x16,
                   vpx_highbd_8_sub_pixel_avg_variance8x16,
                   vpx_highbd_sad8x16x3_bits8,
                   vpx_highbd_sad8x16x8_bits8,
                   vpx_highbd_sad8x16x4d_bits8)

        HIGHBD_BFP(BLOCK_8X8,
                   vpx_highbd_sad8x8_bits8,
                   vpx_highbd_sad8x8_avg_bits8,
                   vpx_highbd_8_variance8x8,
                   vpx_highbd_8_sub_pixel_variance8x8,
                   vpx_highbd_8_sub_pixel_avg_variance8x8,
                   vpx_highbd_sad8x8x3_bits8,
                   vpx_highbd_sad8x8x8_bits8,
                   vpx_highbd_sad8x8x4d_bits8)

        HIGHBD_BFP(BLOCK_8X4,
                   vpx_highbd_sad8x4_bits8,
                   vpx_highbd_sad8x4_avg_bits8,
                   vpx_highbd_8_variance8x4,
                   vpx_highbd_8_sub_pixel_variance8x4,
                   vpx_highbd_8_sub_pixel_avg_variance8x4,
                   NULL,
                   vpx_highbd_sad8x4x8_bits8,
                   vpx_highbd_sad8x4x4d_bits8)

        HIGHBD_BFP(BLOCK_4X8,
                   vpx_highbd_sad4x8_bits8,
                   vpx_highbd_sad4x8_avg_bits8,
                   vpx_highbd_8_variance4x8,
                   vpx_highbd_8_sub_pixel_variance4x8,
                   vpx_highbd_8_sub_pixel_avg_variance4x8,
                   NULL,
                   vpx_highbd_sad4x8x8_bits8,
                   vpx_highbd_sad4x8x4d_bits8)

        HIGHBD_BFP(BLOCK_4X4,
                   vpx_highbd_sad4x4_bits8,
                   vpx_highbd_sad4x4_avg_bits8,
                   vpx_highbd_8_variance4x4,
                   vpx_highbd_8_sub_pixel_variance4x4,
                   vpx_highbd_8_sub_pixel_avg_variance4x4,
                   vpx_highbd_sad4x4x3_bits8,
                   vpx_highbd_sad4x4x8_bits8,
                   vpx_highbd_sad4x4x4d_bits8)

#if CONFIG_EXT_PARTITION
        HIGHBD_BFP(BLOCK_128X128,
                   vpx_highbd_sad128x128_bits8,
                   vpx_highbd_sad128x128_avg_bits8,
                   vpx_highbd_8_variance128x128,
                   vpx_highbd_8_sub_pixel_variance128x128,
                   vpx_highbd_8_sub_pixel_avg_variance128x128,
                   vpx_highbd_sad128x128x3_bits8,
                   vpx_highbd_sad128x128x8_bits8,
                   vpx_highbd_sad128x128x4d_bits8)

        HIGHBD_BFP(BLOCK_128X64,
                   vpx_highbd_sad128x64_bits8,
                   vpx_highbd_sad128x64_avg_bits8,
                   vpx_highbd_8_variance128x64,
                   vpx_highbd_8_sub_pixel_variance128x64,
                   vpx_highbd_8_sub_pixel_avg_variance128x64,
                   NULL,
                   NULL,
                   vpx_highbd_sad128x64x4d_bits8)

        HIGHBD_BFP(BLOCK_64X128,
                   vpx_highbd_sad64x128_bits8,
                   vpx_highbd_sad64x128_avg_bits8,
                   vpx_highbd_8_variance64x128,
                   vpx_highbd_8_sub_pixel_variance64x128,
                   vpx_highbd_8_sub_pixel_avg_variance64x128,
                   NULL,
                   NULL,
                   vpx_highbd_sad64x128x4d_bits8)
#endif  // CONFIG_EXT_PARTITION

#if CONFIG_EXT_INTER
#if CONFIG_EXT_PARTITION
        HIGHBD_MBFP(BLOCK_128X128,
                    vpx_highbd_masked_sad128x128_bits8,
                    vpx_highbd_masked_variance128x128,
                    vpx_highbd_masked_sub_pixel_variance128x128)
        HIGHBD_MBFP(BLOCK_128X64,
                    vpx_highbd_masked_sad128x64_bits8,
                    vpx_highbd_masked_variance128x64,
                    vpx_highbd_masked_sub_pixel_variance128x64)
        HIGHBD_MBFP(BLOCK_64X128,
                    vpx_highbd_masked_sad64x128_bits8,
                    vpx_highbd_masked_variance64x128,
                    vpx_highbd_masked_sub_pixel_variance64x128)
#endif  // CONFIG_EXT_PARTITION
        HIGHBD_MBFP(BLOCK_64X64,
                    vpx_highbd_masked_sad64x64_bits8,
                    vpx_highbd_masked_variance64x64,
                    vpx_highbd_masked_sub_pixel_variance64x64)
        HIGHBD_MBFP(BLOCK_64X32,
                    vpx_highbd_masked_sad64x32_bits8,
                    vpx_highbd_masked_variance64x32,
                    vpx_highbd_masked_sub_pixel_variance64x32)
        HIGHBD_MBFP(BLOCK_32X64,
                    vpx_highbd_masked_sad32x64_bits8,
                    vpx_highbd_masked_variance32x64,
                    vpx_highbd_masked_sub_pixel_variance32x64)
        HIGHBD_MBFP(BLOCK_32X32,
                    vpx_highbd_masked_sad32x32_bits8,
                    vpx_highbd_masked_variance32x32,
                    vpx_highbd_masked_sub_pixel_variance32x32)
        HIGHBD_MBFP(BLOCK_32X16,
                    vpx_highbd_masked_sad32x16_bits8,
                    vpx_highbd_masked_variance32x16,
                    vpx_highbd_masked_sub_pixel_variance32x16)
        HIGHBD_MBFP(BLOCK_16X32,
                    vpx_highbd_masked_sad16x32_bits8,
                    vpx_highbd_masked_variance16x32,
                    vpx_highbd_masked_sub_pixel_variance16x32)
        HIGHBD_MBFP(BLOCK_16X16,
                    vpx_highbd_masked_sad16x16_bits8,
                    vpx_highbd_masked_variance16x16,
                    vpx_highbd_masked_sub_pixel_variance16x16)
        HIGHBD_MBFP(BLOCK_8X16,
                    vpx_highbd_masked_sad8x16_bits8,
                    vpx_highbd_masked_variance8x16,
                    vpx_highbd_masked_sub_pixel_variance8x16)
        HIGHBD_MBFP(BLOCK_16X8,
                    vpx_highbd_masked_sad16x8_bits8,
                    vpx_highbd_masked_variance16x8,
                    vpx_highbd_masked_sub_pixel_variance16x8)
        HIGHBD_MBFP(BLOCK_8X8,
                    vpx_highbd_masked_sad8x8_bits8,
                    vpx_highbd_masked_variance8x8,
                    vpx_highbd_masked_sub_pixel_variance8x8)
        HIGHBD_MBFP(BLOCK_4X8,
                    vpx_highbd_masked_sad4x8_bits8,
                    vpx_highbd_masked_variance4x8,
                    vpx_highbd_masked_sub_pixel_variance4x8)
        HIGHBD_MBFP(BLOCK_8X4,
                    vpx_highbd_masked_sad8x4_bits8,
                    vpx_highbd_masked_variance8x4,
                    vpx_highbd_masked_sub_pixel_variance8x4)
        HIGHBD_MBFP(BLOCK_4X4,
                    vpx_highbd_masked_sad4x4_bits8,
                    vpx_highbd_masked_variance4x4,
                    vpx_highbd_masked_sub_pixel_variance4x4)
#endif  // CONFIG_EXT_INTER
        break;

      case VPX_BITS_10:
        HIGHBD_BFP(BLOCK_32X16,
                   vpx_highbd_sad32x16_bits10,
                   vpx_highbd_sad32x16_avg_bits10,
                   vpx_highbd_10_variance32x16,
                   vpx_highbd_10_sub_pixel_variance32x16,
                   vpx_highbd_10_sub_pixel_avg_variance32x16,
                   NULL,
                   NULL,
                   vpx_highbd_sad32x16x4d_bits10)

        HIGHBD_BFP(BLOCK_16X32,
                   vpx_highbd_sad16x32_bits10,
                   vpx_highbd_sad16x32_avg_bits10,
                   vpx_highbd_10_variance16x32,
                   vpx_highbd_10_sub_pixel_variance16x32,
                   vpx_highbd_10_sub_pixel_avg_variance16x32,
                   NULL,
                   NULL,
                   vpx_highbd_sad16x32x4d_bits10)

        HIGHBD_BFP(BLOCK_64X32,
                   vpx_highbd_sad64x32_bits10,
                   vpx_highbd_sad64x32_avg_bits10,
                   vpx_highbd_10_variance64x32,
                   vpx_highbd_10_sub_pixel_variance64x32,
                   vpx_highbd_10_sub_pixel_avg_variance64x32,
                   NULL,
                   NULL,
                   vpx_highbd_sad64x32x4d_bits10)

        HIGHBD_BFP(BLOCK_32X64,
                   vpx_highbd_sad32x64_bits10,
                   vpx_highbd_sad32x64_avg_bits10,
                   vpx_highbd_10_variance32x64,
                   vpx_highbd_10_sub_pixel_variance32x64,
                   vpx_highbd_10_sub_pixel_avg_variance32x64,
                   NULL,
                   NULL,
                   vpx_highbd_sad32x64x4d_bits10)

        HIGHBD_BFP(BLOCK_32X32,
                   vpx_highbd_sad32x32_bits10,
                   vpx_highbd_sad32x32_avg_bits10,
                   vpx_highbd_10_variance32x32,
                   vpx_highbd_10_sub_pixel_variance32x32,
                   vpx_highbd_10_sub_pixel_avg_variance32x32,
                   vpx_highbd_sad32x32x3_bits10,
                   vpx_highbd_sad32x32x8_bits10,
                   vpx_highbd_sad32x32x4d_bits10)

        HIGHBD_BFP(BLOCK_64X64,
                   vpx_highbd_sad64x64_bits10,
                   vpx_highbd_sad64x64_avg_bits10,
                   vpx_highbd_10_variance64x64,
                   vpx_highbd_10_sub_pixel_variance64x64,
                   vpx_highbd_10_sub_pixel_avg_variance64x64,
                   vpx_highbd_sad64x64x3_bits10,
                   vpx_highbd_sad64x64x8_bits10,
                   vpx_highbd_sad64x64x4d_bits10)

        HIGHBD_BFP(BLOCK_16X16,
                   vpx_highbd_sad16x16_bits10,
                   vpx_highbd_sad16x16_avg_bits10,
                   vpx_highbd_10_variance16x16,
                   vpx_highbd_10_sub_pixel_variance16x16,
                   vpx_highbd_10_sub_pixel_avg_variance16x16,
                   vpx_highbd_sad16x16x3_bits10,
                   vpx_highbd_sad16x16x8_bits10,
                   vpx_highbd_sad16x16x4d_bits10)

        HIGHBD_BFP(BLOCK_16X8,
                   vpx_highbd_sad16x8_bits10,
                   vpx_highbd_sad16x8_avg_bits10,
                   vpx_highbd_10_variance16x8,
                   vpx_highbd_10_sub_pixel_variance16x8,
                   vpx_highbd_10_sub_pixel_avg_variance16x8,
                   vpx_highbd_sad16x8x3_bits10,
                   vpx_highbd_sad16x8x8_bits10,
                   vpx_highbd_sad16x8x4d_bits10)

        HIGHBD_BFP(BLOCK_8X16,
                   vpx_highbd_sad8x16_bits10,
                   vpx_highbd_sad8x16_avg_bits10,
                   vpx_highbd_10_variance8x16,
                   vpx_highbd_10_sub_pixel_variance8x16,
                   vpx_highbd_10_sub_pixel_avg_variance8x16,
                   vpx_highbd_sad8x16x3_bits10,
                   vpx_highbd_sad8x16x8_bits10,
                   vpx_highbd_sad8x16x4d_bits10)

        HIGHBD_BFP(BLOCK_8X8,
                   vpx_highbd_sad8x8_bits10,
                   vpx_highbd_sad8x8_avg_bits10,
                   vpx_highbd_10_variance8x8,
                   vpx_highbd_10_sub_pixel_variance8x8,
                   vpx_highbd_10_sub_pixel_avg_variance8x8,
                   vpx_highbd_sad8x8x3_bits10,
                   vpx_highbd_sad8x8x8_bits10,
                   vpx_highbd_sad8x8x4d_bits10)

        HIGHBD_BFP(BLOCK_8X4,
                   vpx_highbd_sad8x4_bits10,
                   vpx_highbd_sad8x4_avg_bits10,
                   vpx_highbd_10_variance8x4,
                   vpx_highbd_10_sub_pixel_variance8x4,
                   vpx_highbd_10_sub_pixel_avg_variance8x4,
                   NULL,
                   vpx_highbd_sad8x4x8_bits10,
                   vpx_highbd_sad8x4x4d_bits10)

        HIGHBD_BFP(BLOCK_4X8,
                   vpx_highbd_sad4x8_bits10,
                   vpx_highbd_sad4x8_avg_bits10,
                   vpx_highbd_10_variance4x8,
                   vpx_highbd_10_sub_pixel_variance4x8,
                   vpx_highbd_10_sub_pixel_avg_variance4x8,
                   NULL,
                   vpx_highbd_sad4x8x8_bits10,
                   vpx_highbd_sad4x8x4d_bits10)

        HIGHBD_BFP(BLOCK_4X4,
                   vpx_highbd_sad4x4_bits10,
                   vpx_highbd_sad4x4_avg_bits10,
                   vpx_highbd_10_variance4x4,
                   vpx_highbd_10_sub_pixel_variance4x4,
                   vpx_highbd_10_sub_pixel_avg_variance4x4,
                   vpx_highbd_sad4x4x3_bits10,
                   vpx_highbd_sad4x4x8_bits10,
                   vpx_highbd_sad4x4x4d_bits10)

#if CONFIG_EXT_PARTITION
        HIGHBD_BFP(BLOCK_128X128,
                   vpx_highbd_sad128x128_bits10,
                   vpx_highbd_sad128x128_avg_bits10,
                   vpx_highbd_10_variance128x128,
                   vpx_highbd_10_sub_pixel_variance128x128,
                   vpx_highbd_10_sub_pixel_avg_variance128x128,
                   vpx_highbd_sad128x128x3_bits10,
                   vpx_highbd_sad128x128x8_bits10,
                   vpx_highbd_sad128x128x4d_bits10)

        HIGHBD_BFP(BLOCK_128X64,
                   vpx_highbd_sad128x64_bits10,
                   vpx_highbd_sad128x64_avg_bits10,
                   vpx_highbd_10_variance128x64,
                   vpx_highbd_10_sub_pixel_variance128x64,
                   vpx_highbd_10_sub_pixel_avg_variance128x64,
                   NULL,
                   NULL,
                   vpx_highbd_sad128x64x4d_bits10)

        HIGHBD_BFP(BLOCK_64X128,
                   vpx_highbd_sad64x128_bits10,
                   vpx_highbd_sad64x128_avg_bits10,
                   vpx_highbd_10_variance64x128,
                   vpx_highbd_10_sub_pixel_variance64x128,
                   vpx_highbd_10_sub_pixel_avg_variance64x128,
                   NULL,
                   NULL,
                   vpx_highbd_sad64x128x4d_bits10)
#endif  // CONFIG_EXT_PARTITION

#if CONFIG_EXT_INTER
#if CONFIG_EXT_PARTITION
        HIGHBD_MBFP(BLOCK_128X128,
                    vpx_highbd_masked_sad128x128_bits10,
                    vpx_highbd_10_masked_variance128x128,
                    vpx_highbd_10_masked_sub_pixel_variance128x128)
        HIGHBD_MBFP(BLOCK_128X64,
                    vpx_highbd_masked_sad128x64_bits10,
                    vpx_highbd_10_masked_variance128x64,
                    vpx_highbd_10_masked_sub_pixel_variance128x64)
        HIGHBD_MBFP(BLOCK_64X128,
                    vpx_highbd_masked_sad64x128_bits10,
                    vpx_highbd_10_masked_variance64x128,
                    vpx_highbd_10_masked_sub_pixel_variance64x128)
#endif  // CONFIG_EXT_PARTITION
        HIGHBD_MBFP(BLOCK_64X64,
                    vpx_highbd_masked_sad64x64_bits10,
                    vpx_highbd_10_masked_variance64x64,
                    vpx_highbd_10_masked_sub_pixel_variance64x64)
        HIGHBD_MBFP(BLOCK_64X32,
                    vpx_highbd_masked_sad64x32_bits10,
                    vpx_highbd_10_masked_variance64x32,
                    vpx_highbd_10_masked_sub_pixel_variance64x32)
        HIGHBD_MBFP(BLOCK_32X64,
                    vpx_highbd_masked_sad32x64_bits10,
                    vpx_highbd_10_masked_variance32x64,
                    vpx_highbd_10_masked_sub_pixel_variance32x64)
        HIGHBD_MBFP(BLOCK_32X32,
                    vpx_highbd_masked_sad32x32_bits10,
                    vpx_highbd_10_masked_variance32x32,
                    vpx_highbd_10_masked_sub_pixel_variance32x32)
        HIGHBD_MBFP(BLOCK_32X16,
                    vpx_highbd_masked_sad32x16_bits10,
                    vpx_highbd_10_masked_variance32x16,
                    vpx_highbd_10_masked_sub_pixel_variance32x16)
        HIGHBD_MBFP(BLOCK_16X32,
                    vpx_highbd_masked_sad16x32_bits10,
                    vpx_highbd_10_masked_variance16x32,
                    vpx_highbd_10_masked_sub_pixel_variance16x32)
        HIGHBD_MBFP(BLOCK_16X16,
                    vpx_highbd_masked_sad16x16_bits10,
                    vpx_highbd_10_masked_variance16x16,
                    vpx_highbd_10_masked_sub_pixel_variance16x16)
        HIGHBD_MBFP(BLOCK_8X16,
                    vpx_highbd_masked_sad8x16_bits10,
                    vpx_highbd_10_masked_variance8x16,
                    vpx_highbd_10_masked_sub_pixel_variance8x16)
        HIGHBD_MBFP(BLOCK_16X8,
                    vpx_highbd_masked_sad16x8_bits10,
                    vpx_highbd_10_masked_variance16x8,
                    vpx_highbd_10_masked_sub_pixel_variance16x8)
        HIGHBD_MBFP(BLOCK_8X8,
                    vpx_highbd_masked_sad8x8_bits10,
                    vpx_highbd_10_masked_variance8x8,
                    vpx_highbd_10_masked_sub_pixel_variance8x8)
        HIGHBD_MBFP(BLOCK_4X8,
                    vpx_highbd_masked_sad4x8_bits10,
                    vpx_highbd_10_masked_variance4x8,
                    vpx_highbd_10_masked_sub_pixel_variance4x8)
        HIGHBD_MBFP(BLOCK_8X4,
                    vpx_highbd_masked_sad8x4_bits10,
                    vpx_highbd_10_masked_variance8x4,
                    vpx_highbd_10_masked_sub_pixel_variance8x4)
        HIGHBD_MBFP(BLOCK_4X4,
                    vpx_highbd_masked_sad4x4_bits10,
                    vpx_highbd_10_masked_variance4x4,
                    vpx_highbd_10_masked_sub_pixel_variance4x4)
#endif  // CONFIG_EXT_INTER
        break;

      case VPX_BITS_12:
        HIGHBD_BFP(BLOCK_32X16,
                   vpx_highbd_sad32x16_bits12,
                   vpx_highbd_sad32x16_avg_bits12,
                   vpx_highbd_12_variance32x16,
                   vpx_highbd_12_sub_pixel_variance32x16,
                   vpx_highbd_12_sub_pixel_avg_variance32x16,
                   NULL,
                   NULL,
                   vpx_highbd_sad32x16x4d_bits12)

        HIGHBD_BFP(BLOCK_16X32,
                   vpx_highbd_sad16x32_bits12,
                   vpx_highbd_sad16x32_avg_bits12,
                   vpx_highbd_12_variance16x32,
                   vpx_highbd_12_sub_pixel_variance16x32,
                   vpx_highbd_12_sub_pixel_avg_variance16x32,
                   NULL,
                   NULL,
                   vpx_highbd_sad16x32x4d_bits12)

        HIGHBD_BFP(BLOCK_64X32,
                   vpx_highbd_sad64x32_bits12,
                   vpx_highbd_sad64x32_avg_bits12,
                   vpx_highbd_12_variance64x32,
                   vpx_highbd_12_sub_pixel_variance64x32,
                   vpx_highbd_12_sub_pixel_avg_variance64x32,
                   NULL,
                   NULL,
                   vpx_highbd_sad64x32x4d_bits12)

        HIGHBD_BFP(BLOCK_32X64,
                   vpx_highbd_sad32x64_bits12,
                   vpx_highbd_sad32x64_avg_bits12,
                   vpx_highbd_12_variance32x64,
                   vpx_highbd_12_sub_pixel_variance32x64,
                   vpx_highbd_12_sub_pixel_avg_variance32x64,
                   NULL,
                   NULL,
                   vpx_highbd_sad32x64x4d_bits12)

        HIGHBD_BFP(BLOCK_32X32,
                   vpx_highbd_sad32x32_bits12,
                   vpx_highbd_sad32x32_avg_bits12,
                   vpx_highbd_12_variance32x32,
                   vpx_highbd_12_sub_pixel_variance32x32,
                   vpx_highbd_12_sub_pixel_avg_variance32x32,
                   vpx_highbd_sad32x32x3_bits12,
                   vpx_highbd_sad32x32x8_bits12,
                   vpx_highbd_sad32x32x4d_bits12)

        HIGHBD_BFP(BLOCK_64X64,
                   vpx_highbd_sad64x64_bits12,
                   vpx_highbd_sad64x64_avg_bits12,
                   vpx_highbd_12_variance64x64,
                   vpx_highbd_12_sub_pixel_variance64x64,
                   vpx_highbd_12_sub_pixel_avg_variance64x64,
                   vpx_highbd_sad64x64x3_bits12,
                   vpx_highbd_sad64x64x8_bits12,
                   vpx_highbd_sad64x64x4d_bits12)

        HIGHBD_BFP(BLOCK_16X16,
                   vpx_highbd_sad16x16_bits12,
                   vpx_highbd_sad16x16_avg_bits12,
                   vpx_highbd_12_variance16x16,
                   vpx_highbd_12_sub_pixel_variance16x16,
                   vpx_highbd_12_sub_pixel_avg_variance16x16,
                   vpx_highbd_sad16x16x3_bits12,
                   vpx_highbd_sad16x16x8_bits12,
                   vpx_highbd_sad16x16x4d_bits12)

        HIGHBD_BFP(BLOCK_16X8,
                   vpx_highbd_sad16x8_bits12,
                   vpx_highbd_sad16x8_avg_bits12,
                   vpx_highbd_12_variance16x8,
                   vpx_highbd_12_sub_pixel_variance16x8,
                   vpx_highbd_12_sub_pixel_avg_variance16x8,
                   vpx_highbd_sad16x8x3_bits12,
                   vpx_highbd_sad16x8x8_bits12,
                   vpx_highbd_sad16x8x4d_bits12)

        HIGHBD_BFP(BLOCK_8X16,
                   vpx_highbd_sad8x16_bits12,
                   vpx_highbd_sad8x16_avg_bits12,
                   vpx_highbd_12_variance8x16,
                   vpx_highbd_12_sub_pixel_variance8x16,
                   vpx_highbd_12_sub_pixel_avg_variance8x16,
                   vpx_highbd_sad8x16x3_bits12,
                   vpx_highbd_sad8x16x8_bits12,
                   vpx_highbd_sad8x16x4d_bits12)

        HIGHBD_BFP(BLOCK_8X8,
                   vpx_highbd_sad8x8_bits12,
                   vpx_highbd_sad8x8_avg_bits12,
                   vpx_highbd_12_variance8x8,
                   vpx_highbd_12_sub_pixel_variance8x8,
                   vpx_highbd_12_sub_pixel_avg_variance8x8,
                   vpx_highbd_sad8x8x3_bits12,
                   vpx_highbd_sad8x8x8_bits12,
                   vpx_highbd_sad8x8x4d_bits12)

        HIGHBD_BFP(BLOCK_8X4,
                   vpx_highbd_sad8x4_bits12,
                   vpx_highbd_sad8x4_avg_bits12,
                   vpx_highbd_12_variance8x4,
                   vpx_highbd_12_sub_pixel_variance8x4,
                   vpx_highbd_12_sub_pixel_avg_variance8x4,
                   NULL,
                   vpx_highbd_sad8x4x8_bits12,
                   vpx_highbd_sad8x4x4d_bits12)

        HIGHBD_BFP(BLOCK_4X8,
                   vpx_highbd_sad4x8_bits12,
                   vpx_highbd_sad4x8_avg_bits12,
                   vpx_highbd_12_variance4x8,
                   vpx_highbd_12_sub_pixel_variance4x8,
                   vpx_highbd_12_sub_pixel_avg_variance4x8,
                   NULL,
                   vpx_highbd_sad4x8x8_bits12,
                   vpx_highbd_sad4x8x4d_bits12)

        HIGHBD_BFP(BLOCK_4X4,
                   vpx_highbd_sad4x4_bits12,
                   vpx_highbd_sad4x4_avg_bits12,
                   vpx_highbd_12_variance4x4,
                   vpx_highbd_12_sub_pixel_variance4x4,
                   vpx_highbd_12_sub_pixel_avg_variance4x4,
                   vpx_highbd_sad4x4x3_bits12,
                   vpx_highbd_sad4x4x8_bits12,
                   vpx_highbd_sad4x4x4d_bits12)

#if CONFIG_EXT_PARTITION
        HIGHBD_BFP(BLOCK_128X128,
                   vpx_highbd_sad128x128_bits12,
                   vpx_highbd_sad128x128_avg_bits12,
                   vpx_highbd_12_variance128x128,
                   vpx_highbd_12_sub_pixel_variance128x128,
                   vpx_highbd_12_sub_pixel_avg_variance128x128,
                   vpx_highbd_sad128x128x3_bits12,
                   vpx_highbd_sad128x128x8_bits12,
                   vpx_highbd_sad128x128x4d_bits12)

        HIGHBD_BFP(BLOCK_128X64,
                   vpx_highbd_sad128x64_bits12,
                   vpx_highbd_sad128x64_avg_bits12,
                   vpx_highbd_12_variance128x64,
                   vpx_highbd_12_sub_pixel_variance128x64,
                   vpx_highbd_12_sub_pixel_avg_variance128x64,
                   NULL,
                   NULL,
                   vpx_highbd_sad128x64x4d_bits12)

        HIGHBD_BFP(BLOCK_64X128,
                   vpx_highbd_sad64x128_bits12,
                   vpx_highbd_sad64x128_avg_bits12,
                   vpx_highbd_12_variance64x128,
                   vpx_highbd_12_sub_pixel_variance64x128,
                   vpx_highbd_12_sub_pixel_avg_variance64x128,
                   NULL,
                   NULL,
                   vpx_highbd_sad64x128x4d_bits12)
#endif  // CONFIG_EXT_PARTITION

#if CONFIG_EXT_INTER
#if CONFIG_EXT_PARTITION
        HIGHBD_MBFP(BLOCK_128X128,
                    vpx_highbd_masked_sad128x128_bits12,
                    vpx_highbd_12_masked_variance128x128,
                    vpx_highbd_12_masked_sub_pixel_variance128x128)
        HIGHBD_MBFP(BLOCK_128X64,
                    vpx_highbd_masked_sad128x64_bits12,
                    vpx_highbd_12_masked_variance128x64,
                    vpx_highbd_12_masked_sub_pixel_variance128x64)
        HIGHBD_MBFP(BLOCK_64X128,
                    vpx_highbd_masked_sad64x128_bits12,
                    vpx_highbd_12_masked_variance64x128,
                    vpx_highbd_12_masked_sub_pixel_variance64x128)
#endif  // CONFIG_EXT_PARTITION
        HIGHBD_MBFP(BLOCK_64X64,
                    vpx_highbd_masked_sad64x64_bits12,
                    vpx_highbd_12_masked_variance64x64,
                    vpx_highbd_12_masked_sub_pixel_variance64x64)
        HIGHBD_MBFP(BLOCK_64X32,
                    vpx_highbd_masked_sad64x32_bits12,
                    vpx_highbd_12_masked_variance64x32,
                    vpx_highbd_12_masked_sub_pixel_variance64x32)
        HIGHBD_MBFP(BLOCK_32X64,
                    vpx_highbd_masked_sad32x64_bits12,
                    vpx_highbd_12_masked_variance32x64,
                    vpx_highbd_12_masked_sub_pixel_variance32x64)
        HIGHBD_MBFP(BLOCK_32X32,
                    vpx_highbd_masked_sad32x32_bits12,
                    vpx_highbd_12_masked_variance32x32,
                    vpx_highbd_12_masked_sub_pixel_variance32x32)
        HIGHBD_MBFP(BLOCK_32X16,
                    vpx_highbd_masked_sad32x16_bits12,
                    vpx_highbd_12_masked_variance32x16,
                    vpx_highbd_12_masked_sub_pixel_variance32x16)
        HIGHBD_MBFP(BLOCK_16X32,
                    vpx_highbd_masked_sad16x32_bits12,
                    vpx_highbd_12_masked_variance16x32,
                    vpx_highbd_12_masked_sub_pixel_variance16x32)
        HIGHBD_MBFP(BLOCK_16X16,
                    vpx_highbd_masked_sad16x16_bits12,
                    vpx_highbd_12_masked_variance16x16,
                    vpx_highbd_12_masked_sub_pixel_variance16x16)
        HIGHBD_MBFP(BLOCK_8X16,
                    vpx_highbd_masked_sad8x16_bits12,
                    vpx_highbd_12_masked_variance8x16,
                    vpx_highbd_12_masked_sub_pixel_variance8x16)
        HIGHBD_MBFP(BLOCK_16X8,
                    vpx_highbd_masked_sad16x8_bits12,
                    vpx_highbd_12_masked_variance16x8,
                    vpx_highbd_12_masked_sub_pixel_variance16x8)
        HIGHBD_MBFP(BLOCK_8X8,
                    vpx_highbd_masked_sad8x8_bits12,
                    vpx_highbd_12_masked_variance8x8,
                    vpx_highbd_12_masked_sub_pixel_variance8x8)
        HIGHBD_MBFP(BLOCK_4X8,
                    vpx_highbd_masked_sad4x8_bits12,
                    vpx_highbd_12_masked_variance4x8,
                    vpx_highbd_12_masked_sub_pixel_variance4x8)
        HIGHBD_MBFP(BLOCK_8X4,
                    vpx_highbd_masked_sad8x4_bits12,
                    vpx_highbd_12_masked_variance8x4,
                    vpx_highbd_12_masked_sub_pixel_variance8x4)
        HIGHBD_MBFP(BLOCK_4X4,
                    vpx_highbd_masked_sad4x4_bits12,
                    vpx_highbd_12_masked_variance4x4,
                    vpx_highbd_12_masked_sub_pixel_variance4x4)
#endif  // CONFIG_EXT_INTER
        break;

      default:
        assert(0 && "cm->bit_depth should be VPX_BITS_8, "
                    "VPX_BITS_10 or VPX_BITS_12");
    }
  }
}
#endif  // CONFIG_VP9_HIGHBITDEPTH

static void realloc_segmentation_maps(VP10_COMP *cpi) {
  VP10_COMMON *const cm = &cpi->common;

  // Create the encoder segmentation map and set all entries to 0
  vpx_free(cpi->segmentation_map);
  CHECK_MEM_ERROR(cm, cpi->segmentation_map,
                  vpx_calloc(cm->mi_rows * cm->mi_cols, 1));

  // Create a map used for cyclic background refresh.
  if (cpi->cyclic_refresh)
    vp10_cyclic_refresh_free(cpi->cyclic_refresh);
  CHECK_MEM_ERROR(cm, cpi->cyclic_refresh,
                  vp10_cyclic_refresh_alloc(cm->mi_rows, cm->mi_cols));

  // Create a map used to mark inactive areas.
  vpx_free(cpi->active_map.map);
  CHECK_MEM_ERROR(cm, cpi->active_map.map,
                  vpx_calloc(cm->mi_rows * cm->mi_cols, 1));

  // And a place holder structure is the coding context
  // for use if we want to save and restore it
  vpx_free(cpi->coding_context.last_frame_seg_map_copy);
  CHECK_MEM_ERROR(cm, cpi->coding_context.last_frame_seg_map_copy,
                  vpx_calloc(cm->mi_rows * cm->mi_cols, 1));
}

void vp10_change_config(struct VP10_COMP *cpi, const VP10EncoderConfig *oxcf) {
  VP10_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;
#if CONFIG_EXT_REFS
  int ref_frame;
#endif  // CONFIG_EXT_REFS

  if (cm->profile != oxcf->profile)
    cm->profile = oxcf->profile;
  cm->bit_depth = oxcf->bit_depth;
  cm->color_space = oxcf->color_space;
  cm->color_range = oxcf->color_range;

  if (cm->profile <= PROFILE_1)
    assert(cm->bit_depth == VPX_BITS_8);
  else
    assert(cm->bit_depth > VPX_BITS_8);

  cpi->oxcf = *oxcf;
#if CONFIG_VP9_HIGHBITDEPTH
  cpi->td.mb.e_mbd.bd = (int)cm->bit_depth;
#endif  // CONFIG_VP9_HIGHBITDEPTH

  if ((oxcf->pass == 0) && (oxcf->rc_mode == VPX_Q)) {
    rc->baseline_gf_interval = FIXED_GF_INTERVAL;
  } else {
    rc->baseline_gf_interval = (MIN_GF_INTERVAL + MAX_GF_INTERVAL) / 2;
  }

  cpi->refresh_golden_frame = 0;

#if CONFIG_EXT_REFS
  for (ref_frame = LAST_FRAME; ref_frame <= LAST4_FRAME; ++ref_frame) {
    if (ref_frame == cpi->last_ref_to_refresh)
      cpi->refresh_last_frames[ref_frame - LAST_FRAME] = 1;
    else
      cpi->refresh_last_frames[ref_frame - LAST_FRAME] = 0;
  }
#else
  cpi->refresh_last_frame = 1;
#endif  // CONFIG_EXT_REFS

  cm->refresh_frame_context =
      oxcf->error_resilient_mode ? REFRESH_FRAME_CONTEXT_OFF :
          oxcf->frame_parallel_decoding_mode ? REFRESH_FRAME_CONTEXT_FORWARD
                                             : REFRESH_FRAME_CONTEXT_BACKWARD;
  cm->reset_frame_context = RESET_FRAME_CONTEXT_NONE;

  cm->allow_screen_content_tools = (cpi->oxcf.content == VP9E_CONTENT_SCREEN);
  if (cm->allow_screen_content_tools) {
    MACROBLOCK *x = &cpi->td.mb;
    if (x->palette_buffer == 0) {
      CHECK_MEM_ERROR(cm, x->palette_buffer,
                      vpx_memalign(16, sizeof(*x->palette_buffer)));
    }
  }

  vp10_reset_segment_features(cm);
  vp10_set_high_precision_mv(cpi, 0);

  {
    int i;

    for (i = 0; i < MAX_SEGMENTS; i++)
      cpi->segment_encode_breakout[i] = cpi->oxcf.encode_breakout;
  }
  cpi->encode_breakout = cpi->oxcf.encode_breakout;

  set_rc_buffer_sizes(rc, &cpi->oxcf);

  // Under a configuration change, where maximum_buffer_size may change,
  // keep buffer level clipped to the maximum allowed buffer size.
  rc->bits_off_target = VPXMIN(rc->bits_off_target, rc->maximum_buffer_size);
  rc->buffer_level = VPXMIN(rc->buffer_level, rc->maximum_buffer_size);

  // Set up frame rate and related parameters rate control values.
  vp10_new_framerate(cpi, cpi->framerate);

  // Set absolute upper and lower quality limits
  rc->worst_quality = cpi->oxcf.worst_allowed_q;
  rc->best_quality = cpi->oxcf.best_allowed_q;

  cm->interp_filter = cpi->sf.default_interp_filter;

  if (cpi->oxcf.render_width > 0 && cpi->oxcf.render_height > 0) {
    cm->render_width = cpi->oxcf.render_width;
    cm->render_height = cpi->oxcf.render_height;
  } else {
    cm->render_width = cpi->oxcf.width;
    cm->render_height = cpi->oxcf.height;
  }
  cm->width = cpi->oxcf.width;
  cm->height = cpi->oxcf.height;

  if (cpi->initial_width) {
    if (cm->width > cpi->initial_width || cm->height > cpi->initial_height) {
      vp10_free_context_buffers(cm);
      vp10_alloc_compressor_data(cpi);
      realloc_segmentation_maps(cpi);
      cpi->initial_width = cpi->initial_height = 0;
    }
  }
  update_frame_size(cpi);

  cpi->alt_ref_source = NULL;
  rc->is_src_frame_alt_ref = 0;

#if 0
  // Experimental RD Code
  cpi->frame_distortion = 0;
  cpi->last_frame_distortion = 0;
#endif

  set_tile_limits(cpi);

  cpi->ext_refresh_frame_flags_pending = 0;
  cpi->ext_refresh_frame_context_pending = 0;

#if CONFIG_VP9_HIGHBITDEPTH
  highbd_set_var_fns(cpi);
#endif
}

#ifndef M_LOG2_E
#define M_LOG2_E 0.693147180559945309417
#endif
#define log2f(x) (log (x) / (float) M_LOG2_E)

#if !CONFIG_REF_MV
static void cal_nmvjointsadcost(int *mvjointsadcost) {
  mvjointsadcost[0] = 600;
  mvjointsadcost[1] = 300;
  mvjointsadcost[2] = 300;
  mvjointsadcost[3] = 300;
}
#endif

static void cal_nmvsadcosts(int *mvsadcost[2]) {
  int i = 1;

  mvsadcost[0][0] = 0;
  mvsadcost[1][0] = 0;

  do {
    double z = 256 * (2 * (log2f(8 * i) + .6));
    mvsadcost[0][i] = (int)z;
    mvsadcost[1][i] = (int)z;
    mvsadcost[0][-i] = (int)z;
    mvsadcost[1][-i] = (int)z;
  } while (++i <= MV_MAX);
}

static void cal_nmvsadcosts_hp(int *mvsadcost[2]) {
  int i = 1;

  mvsadcost[0][0] = 0;
  mvsadcost[1][0] = 0;

  do {
    double z = 256 * (2 * (log2f(8 * i) + .6));
    mvsadcost[0][i] = (int)z;
    mvsadcost[1][i] = (int)z;
    mvsadcost[0][-i] = (int)z;
    mvsadcost[1][-i] = (int)z;
  } while (++i <= MV_MAX);
}

static INLINE void init_upsampled_ref_frame_bufs(VP10_COMP *cpi) {
  int i;

  for (i = 0; i < MAX_REF_FRAMES; ++i) {
    cpi->upsampled_ref_bufs[i].ref_count = 0;
    cpi->upsampled_ref_idx[i] = INVALID_IDX;
  }
}

VP10_COMP *vp10_create_compressor(VP10EncoderConfig *oxcf,
                                BufferPool *const pool) {
  unsigned int i;
  VP10_COMP *volatile const cpi = vpx_memalign(32, sizeof(VP10_COMP));
  VP10_COMMON *volatile const cm = cpi != NULL ? &cpi->common : NULL;

  if (!cm)
    return NULL;

  vp10_zero(*cpi);

  if (setjmp(cm->error.jmp)) {
    cm->error.setjmp = 0;
    vp10_remove_compressor(cpi);
    return 0;
  }

  cm->error.setjmp = 1;
  cm->alloc_mi = vp10_enc_alloc_mi;
  cm->free_mi = vp10_enc_free_mi;
  cm->setup_mi = vp10_enc_setup_mi;

  CHECK_MEM_ERROR(cm, cm->fc,
                  (FRAME_CONTEXT *)vpx_calloc(1, sizeof(*cm->fc)));
  CHECK_MEM_ERROR(cm, cm->frame_contexts,
                  (FRAME_CONTEXT *)vpx_calloc(FRAME_CONTEXTS,
                  sizeof(*cm->frame_contexts)));

  cpi->resize_state = 0;
  cpi->resize_avg_qp = 0;
  cpi->resize_buffer_underflow = 0;
  cpi->common.buffer_pool = pool;

  init_config(cpi, oxcf);
  vp10_rc_init(&cpi->oxcf, oxcf->pass, &cpi->rc);

  cm->current_video_frame = 0;
  cpi->partition_search_skippable_frame = 0;
  cpi->tile_data = NULL;

  realloc_segmentation_maps(cpi);

#if CONFIG_REF_MV
  for (i = 0; i < NMV_CONTEXTS; ++i) {
    CHECK_MEM_ERROR(cm, cpi->nmv_costs[i][0],
                    vpx_calloc(MV_VALS, sizeof(*cpi->nmv_costs[i][0])));
    CHECK_MEM_ERROR(cm, cpi->nmv_costs[i][1],
                    vpx_calloc(MV_VALS, sizeof(*cpi->nmv_costs[i][1])));
    CHECK_MEM_ERROR(cm, cpi->nmv_costs_hp[i][0],
                    vpx_calloc(MV_VALS, sizeof(*cpi->nmv_costs_hp[i][0])));
    CHECK_MEM_ERROR(cm, cpi->nmv_costs_hp[i][1],
                    vpx_calloc(MV_VALS, sizeof(*cpi->nmv_costs_hp[i][1])));
  }
#endif

  CHECK_MEM_ERROR(cm, cpi->nmvcosts[0],
                  vpx_calloc(MV_VALS, sizeof(*cpi->nmvcosts[0])));
  CHECK_MEM_ERROR(cm, cpi->nmvcosts[1],
                  vpx_calloc(MV_VALS, sizeof(*cpi->nmvcosts[1])));
  CHECK_MEM_ERROR(cm, cpi->nmvcosts_hp[0],
                  vpx_calloc(MV_VALS, sizeof(*cpi->nmvcosts_hp[0])));
  CHECK_MEM_ERROR(cm, cpi->nmvcosts_hp[1],
                  vpx_calloc(MV_VALS, sizeof(*cpi->nmvcosts_hp[1])));
  CHECK_MEM_ERROR(cm, cpi->nmvsadcosts[0],
                  vpx_calloc(MV_VALS, sizeof(*cpi->nmvsadcosts[0])));
  CHECK_MEM_ERROR(cm, cpi->nmvsadcosts[1],
                  vpx_calloc(MV_VALS, sizeof(*cpi->nmvsadcosts[1])));
  CHECK_MEM_ERROR(cm, cpi->nmvsadcosts_hp[0],
                  vpx_calloc(MV_VALS, sizeof(*cpi->nmvsadcosts_hp[0])));
  CHECK_MEM_ERROR(cm, cpi->nmvsadcosts_hp[1],
                  vpx_calloc(MV_VALS, sizeof(*cpi->nmvsadcosts_hp[1])));

  for (i = 0; i < (sizeof(cpi->mbgraph_stats) /
                   sizeof(cpi->mbgraph_stats[0])); i++) {
    CHECK_MEM_ERROR(cm, cpi->mbgraph_stats[i].mb_stats,
                    vpx_calloc(cm->MBs *
                               sizeof(*cpi->mbgraph_stats[i].mb_stats), 1));
  }

#if CONFIG_FP_MB_STATS
  cpi->use_fp_mb_stats = 0;
  if (cpi->use_fp_mb_stats) {
    // a place holder used to store the first pass mb stats in the first pass
    CHECK_MEM_ERROR(cm, cpi->twopass.frame_mb_stats_buf,
                    vpx_calloc(cm->MBs * sizeof(uint8_t), 1));
  } else {
    cpi->twopass.frame_mb_stats_buf = NULL;
  }
#endif

  cpi->refresh_alt_ref_frame = 0;
  cpi->multi_arf_last_grp_enabled = 0;

  cpi->b_calculate_psnr = CONFIG_INTERNAL_STATS;
#if CONFIG_INTERNAL_STATS
  cpi->b_calculate_blockiness = 1;
  cpi->b_calculate_consistency = 1;
  cpi->total_inconsistency = 0;
  cpi->psnr.worst = 100.0;
  cpi->worst_ssim = 100.0;

  cpi->count = 0;
  cpi->bytes = 0;

  if (cpi->b_calculate_psnr) {
    cpi->total_sq_error = 0;
    cpi->total_samples = 0;
    cpi->tot_recode_hits = 0;
    cpi->summed_quality = 0;
    cpi->summed_weights = 0;
  }

  cpi->fastssim.worst = 100.0;
  cpi->psnrhvs.worst = 100.0;

  if (cpi->b_calculate_blockiness) {
    cpi->total_blockiness = 0;
    cpi->worst_blockiness = 0.0;
  }

  if (cpi->b_calculate_consistency) {
    CHECK_MEM_ERROR(cm, cpi->ssim_vars,
                    vpx_malloc(sizeof(*cpi->ssim_vars) * 4 *
                               cpi->common.mi_rows * cpi->common.mi_cols));
    cpi->worst_consistency = 100.0;
  }
#endif

  cpi->first_time_stamp_ever = INT64_MAX;

#if CONFIG_REF_MV
  for (i = 0; i < NMV_CONTEXTS; ++i) {
    cpi->td.mb.nmvcost[i][0] = &cpi->nmv_costs[i][0][MV_MAX];
    cpi->td.mb.nmvcost[i][1] = &cpi->nmv_costs[i][1][MV_MAX];
    cpi->td.mb.nmvcost_hp[i][0] = &cpi->nmv_costs_hp[i][0][MV_MAX];
    cpi->td.mb.nmvcost_hp[i][1] = &cpi->nmv_costs_hp[i][1][MV_MAX];
  }
#else
  cal_nmvjointsadcost(cpi->td.mb.nmvjointsadcost);
  cpi->td.mb.nmvcost[0] = &cpi->nmvcosts[0][MV_MAX];
  cpi->td.mb.nmvcost[1] = &cpi->nmvcosts[1][MV_MAX];
  cpi->td.mb.nmvcost_hp[0] = &cpi->nmvcosts_hp[0][MV_MAX];
  cpi->td.mb.nmvcost_hp[1] = &cpi->nmvcosts_hp[1][MV_MAX];
#endif
  cpi->td.mb.nmvsadcost[0] = &cpi->nmvsadcosts[0][MV_MAX];
  cpi->td.mb.nmvsadcost[1] = &cpi->nmvsadcosts[1][MV_MAX];
  cal_nmvsadcosts(cpi->td.mb.nmvsadcost);

  cpi->td.mb.nmvsadcost_hp[0] = &cpi->nmvsadcosts_hp[0][MV_MAX];
  cpi->td.mb.nmvsadcost_hp[1] = &cpi->nmvsadcosts_hp[1][MV_MAX];
  cal_nmvsadcosts_hp(cpi->td.mb.nmvsadcost_hp);

#if CONFIG_VP9_TEMPORAL_DENOISING
#ifdef OUTPUT_YUV_DENOISED
  yuv_denoised_file = fopen("denoised.yuv", "ab");
#endif
#endif
#ifdef OUTPUT_YUV_SKINMAP
  yuv_skinmap_file = fopen("skinmap.yuv", "ab");
#endif
#ifdef OUTPUT_YUV_REC
  yuv_rec_file = fopen("rec.yuv", "wb");
#endif

#if 0
  framepsnr = fopen("framepsnr.stt", "a");
  kf_list = fopen("kf_list.stt", "w");
#endif

  cpi->allow_encode_breakout = ENCODE_BREAKOUT_ENABLED;

  if (oxcf->pass == 1) {
    vp10_init_first_pass(cpi);
  } else if (oxcf->pass == 2) {
    const size_t packet_sz = sizeof(FIRSTPASS_STATS);
    const int packets = (int)(oxcf->two_pass_stats_in.sz / packet_sz);

#if CONFIG_FP_MB_STATS
    if (cpi->use_fp_mb_stats) {
      const size_t psz = cpi->common.MBs * sizeof(uint8_t);
      const int ps = (int)(oxcf->firstpass_mb_stats_in.sz / psz);

      cpi->twopass.firstpass_mb_stats.mb_stats_start =
          oxcf->firstpass_mb_stats_in.buf;
      cpi->twopass.firstpass_mb_stats.mb_stats_end =
          cpi->twopass.firstpass_mb_stats.mb_stats_start +
          (ps - 1) * cpi->common.MBs * sizeof(uint8_t);
    }
#endif

    cpi->twopass.stats_in_start = oxcf->two_pass_stats_in.buf;
    cpi->twopass.stats_in = cpi->twopass.stats_in_start;
    cpi->twopass.stats_in_end = &cpi->twopass.stats_in[packets - 1];

    vp10_init_second_pass(cpi);
  }

  init_upsampled_ref_frame_bufs(cpi);

  vp10_set_speed_features_framesize_independent(cpi);
  vp10_set_speed_features_framesize_dependent(cpi);

  // Allocate memory to store variances for a frame.
  CHECK_MEM_ERROR(cm, cpi->source_diff_var,
                  vpx_calloc(cm->MBs, sizeof(diff)));
  cpi->source_var_thresh = 0;
  cpi->frames_till_next_var_check = 0;

#define BFP(BT, SDF, SDAF, VF, SVF, SVAF, SDX3F, SDX8F, SDX4DF)\
    cpi->fn_ptr[BT].sdf            = SDF; \
    cpi->fn_ptr[BT].sdaf           = SDAF; \
    cpi->fn_ptr[BT].vf             = VF; \
    cpi->fn_ptr[BT].svf            = SVF; \
    cpi->fn_ptr[BT].svaf           = SVAF; \
    cpi->fn_ptr[BT].sdx3f          = SDX3F; \
    cpi->fn_ptr[BT].sdx8f          = SDX8F; \
    cpi->fn_ptr[BT].sdx4df         = SDX4DF;

#if CONFIG_EXT_PARTITION
  BFP(BLOCK_128X128, vpx_sad128x128, vpx_sad128x128_avg,
      vpx_variance128x128, vpx_sub_pixel_variance128x128,
      vpx_sub_pixel_avg_variance128x128, vpx_sad128x128x3, vpx_sad128x128x8,
      vpx_sad128x128x4d)

  BFP(BLOCK_128X64, vpx_sad128x64, vpx_sad128x64_avg,
      vpx_variance128x64, vpx_sub_pixel_variance128x64,
      vpx_sub_pixel_avg_variance128x64, NULL, NULL, vpx_sad128x64x4d)

  BFP(BLOCK_64X128, vpx_sad64x128, vpx_sad64x128_avg,
      vpx_variance64x128, vpx_sub_pixel_variance64x128,
      vpx_sub_pixel_avg_variance64x128, NULL, NULL, vpx_sad64x128x4d)
#endif  // CONFIG_EXT_PARTITION

  BFP(BLOCK_32X16, vpx_sad32x16, vpx_sad32x16_avg,
      vpx_variance32x16, vpx_sub_pixel_variance32x16,
      vpx_sub_pixel_avg_variance32x16, NULL, NULL, vpx_sad32x16x4d)

  BFP(BLOCK_16X32, vpx_sad16x32, vpx_sad16x32_avg,
      vpx_variance16x32, vpx_sub_pixel_variance16x32,
      vpx_sub_pixel_avg_variance16x32, NULL, NULL, vpx_sad16x32x4d)

  BFP(BLOCK_64X32, vpx_sad64x32, vpx_sad64x32_avg,
      vpx_variance64x32, vpx_sub_pixel_variance64x32,
      vpx_sub_pixel_avg_variance64x32, NULL, NULL, vpx_sad64x32x4d)

  BFP(BLOCK_32X64, vpx_sad32x64, vpx_sad32x64_avg,
      vpx_variance32x64, vpx_sub_pixel_variance32x64,
      vpx_sub_pixel_avg_variance32x64, NULL, NULL, vpx_sad32x64x4d)

  BFP(BLOCK_32X32, vpx_sad32x32, vpx_sad32x32_avg,
      vpx_variance32x32, vpx_sub_pixel_variance32x32,
      vpx_sub_pixel_avg_variance32x32, vpx_sad32x32x3, vpx_sad32x32x8,
      vpx_sad32x32x4d)

  BFP(BLOCK_64X64, vpx_sad64x64, vpx_sad64x64_avg,
      vpx_variance64x64, vpx_sub_pixel_variance64x64,
      vpx_sub_pixel_avg_variance64x64, vpx_sad64x64x3, vpx_sad64x64x8,
      vpx_sad64x64x4d)

  BFP(BLOCK_16X16, vpx_sad16x16, vpx_sad16x16_avg,
      vpx_variance16x16, vpx_sub_pixel_variance16x16,
      vpx_sub_pixel_avg_variance16x16, vpx_sad16x16x3, vpx_sad16x16x8,
      vpx_sad16x16x4d)

  BFP(BLOCK_16X8, vpx_sad16x8, vpx_sad16x8_avg,
      vpx_variance16x8, vpx_sub_pixel_variance16x8,
      vpx_sub_pixel_avg_variance16x8,
      vpx_sad16x8x3, vpx_sad16x8x8, vpx_sad16x8x4d)

  BFP(BLOCK_8X16, vpx_sad8x16, vpx_sad8x16_avg,
      vpx_variance8x16, vpx_sub_pixel_variance8x16,
      vpx_sub_pixel_avg_variance8x16,
      vpx_sad8x16x3, vpx_sad8x16x8, vpx_sad8x16x4d)

  BFP(BLOCK_8X8, vpx_sad8x8, vpx_sad8x8_avg,
      vpx_variance8x8, vpx_sub_pixel_variance8x8,
      vpx_sub_pixel_avg_variance8x8,
      vpx_sad8x8x3, vpx_sad8x8x8, vpx_sad8x8x4d)

  BFP(BLOCK_8X4, vpx_sad8x4, vpx_sad8x4_avg,
      vpx_variance8x4, vpx_sub_pixel_variance8x4,
      vpx_sub_pixel_avg_variance8x4, NULL, vpx_sad8x4x8, vpx_sad8x4x4d)

  BFP(BLOCK_4X8, vpx_sad4x8, vpx_sad4x8_avg,
      vpx_variance4x8, vpx_sub_pixel_variance4x8,
      vpx_sub_pixel_avg_variance4x8, NULL, vpx_sad4x8x8, vpx_sad4x8x4d)

  BFP(BLOCK_4X4, vpx_sad4x4, vpx_sad4x4_avg,
      vpx_variance4x4, vpx_sub_pixel_variance4x4,
      vpx_sub_pixel_avg_variance4x4,
      vpx_sad4x4x3, vpx_sad4x4x8, vpx_sad4x4x4d)

#if CONFIG_EXT_INTER
#define MBFP(BT, MSDF, MVF, MSVF)         \
  cpi->fn_ptr[BT].msdf            = MSDF; \
  cpi->fn_ptr[BT].mvf             = MVF;  \
  cpi->fn_ptr[BT].msvf            = MSVF;

#if CONFIG_EXT_PARTITION
  MBFP(BLOCK_128X128, vpx_masked_sad128x128, vpx_masked_variance128x128,
       vpx_masked_sub_pixel_variance128x128)
  MBFP(BLOCK_128X64, vpx_masked_sad128x64, vpx_masked_variance128x64,
       vpx_masked_sub_pixel_variance128x64)
  MBFP(BLOCK_64X128, vpx_masked_sad64x128, vpx_masked_variance64x128,
       vpx_masked_sub_pixel_variance64x128)
#endif  // CONFIG_EXT_PARTITION
  MBFP(BLOCK_64X64, vpx_masked_sad64x64, vpx_masked_variance64x64,
       vpx_masked_sub_pixel_variance64x64)
  MBFP(BLOCK_64X32, vpx_masked_sad64x32, vpx_masked_variance64x32,
       vpx_masked_sub_pixel_variance64x32)
  MBFP(BLOCK_32X64, vpx_masked_sad32x64, vpx_masked_variance32x64,
       vpx_masked_sub_pixel_variance32x64)
  MBFP(BLOCK_32X32, vpx_masked_sad32x32, vpx_masked_variance32x32,
       vpx_masked_sub_pixel_variance32x32)
  MBFP(BLOCK_32X16, vpx_masked_sad32x16, vpx_masked_variance32x16,
       vpx_masked_sub_pixel_variance32x16)
  MBFP(BLOCK_16X32, vpx_masked_sad16x32, vpx_masked_variance16x32,
       vpx_masked_sub_pixel_variance16x32)
  MBFP(BLOCK_16X16, vpx_masked_sad16x16, vpx_masked_variance16x16,
       vpx_masked_sub_pixel_variance16x16)
  MBFP(BLOCK_16X8, vpx_masked_sad16x8, vpx_masked_variance16x8,
       vpx_masked_sub_pixel_variance16x8)
  MBFP(BLOCK_8X16, vpx_masked_sad8x16, vpx_masked_variance8x16,
       vpx_masked_sub_pixel_variance8x16)
  MBFP(BLOCK_8X8, vpx_masked_sad8x8, vpx_masked_variance8x8,
       vpx_masked_sub_pixel_variance8x8)
  MBFP(BLOCK_4X8, vpx_masked_sad4x8, vpx_masked_variance4x8,
       vpx_masked_sub_pixel_variance4x8)
  MBFP(BLOCK_8X4, vpx_masked_sad8x4, vpx_masked_variance8x4,
       vpx_masked_sub_pixel_variance8x4)
  MBFP(BLOCK_4X4, vpx_masked_sad4x4, vpx_masked_variance4x4,
       vpx_masked_sub_pixel_variance4x4)
#endif  // CONFIG_EXT_INTER

#if CONFIG_VP9_HIGHBITDEPTH
  highbd_set_var_fns(cpi);
#endif

  /* vp10_init_quantizer() is first called here. Add check in
   * vp10_frame_init_quantizer() so that vp10_init_quantizer is only
   * called later when needed. This will avoid unnecessary calls of
   * vp10_init_quantizer() for every frame.
   */
  vp10_init_quantizer(cpi);

  vp10_loop_filter_init(cm);
#if CONFIG_LOOP_RESTORATION
  vp10_loop_restoration_precal();
#endif  // CONFIG_LOOP_RESTORATION
#if CONFIG_ANS
  vp10_build_pareto8_cdf_tab(vp10_pareto8_token_probs, cm->token_tab);
#endif  // CONFIG_ANS

  cm->error.setjmp = 0;

  return cpi;
}
#define SNPRINT(H, T) \
  snprintf((H) + strlen(H), sizeof(H) - strlen(H), (T))

#define SNPRINT2(H, T, V) \
  snprintf((H) + strlen(H), sizeof(H) - strlen(H), (T), (V))

void vp10_remove_compressor(VP10_COMP *cpi) {
  VP10_COMMON *cm;
  unsigned int i;
  int t;

  if (!cpi)
    return;

  cm = &cpi->common;
  if (cm->current_video_frame > 0) {
#if CONFIG_INTERNAL_STATS
    vpx_clear_system_state();

    if (cpi->oxcf.pass != 1) {
      char headings[512] = {0};
      char results[512] = {0};
      FILE *f = fopen("opsnr.stt", "a");
      double time_encoded = (cpi->last_end_time_stamp_seen
                             - cpi->first_time_stamp_ever) / 10000000.000;
      double total_encode_time = (cpi->time_receive_data +
                                  cpi->time_compress_data)   / 1000.000;
      const double dr =
          (double)cpi->bytes * (double) 8 / (double)1000 / time_encoded;
      const double peak = (double)((1 << cpi->oxcf.input_bit_depth) - 1);

      if (cpi->b_calculate_psnr) {
        const double total_psnr =
            vpx_sse_to_psnr((double)cpi->total_samples, peak,
                            (double)cpi->total_sq_error);
        const double total_ssim = 100 * pow(cpi->summed_quality /
                                            cpi->summed_weights, 8.0);
        snprintf(headings, sizeof(headings),
                 "Bitrate\tAVGPsnr\tGLBPsnr\tAVPsnrP\tGLPsnrP\t"
                 "VPXSSIM\tVPSSIMP\tFASTSIM\tPSNRHVS\t"
                 "WstPsnr\tWstSsim\tWstFast\tWstHVS");
        snprintf(results, sizeof(results),
                 "%7.2f\t%7.3f\t%7.3f\t%7.3f\t%7.3f\t"
                 "%7.3f\t%7.3f\t%7.3f\t%7.3f\t"
                 "%7.3f\t%7.3f\t%7.3f\t%7.3f",
                 dr, cpi->psnr.stat[ALL] / cpi->count, total_psnr,
                 cpi->psnr.stat[ALL] / cpi->count, total_psnr,
                 total_ssim, total_ssim,
                 cpi->fastssim.stat[ALL] / cpi->count,
                 cpi->psnrhvs.stat[ALL] / cpi->count,
                 cpi->psnr.worst, cpi->worst_ssim, cpi->fastssim.worst,
                 cpi->psnrhvs.worst);

        if (cpi->b_calculate_blockiness) {
          SNPRINT(headings, "\t  Block\tWstBlck");
          SNPRINT2(results, "\t%7.3f", cpi->total_blockiness / cpi->count);
          SNPRINT2(results, "\t%7.3f", cpi->worst_blockiness);
        }

        if (cpi->b_calculate_consistency) {
          double consistency =
              vpx_sse_to_psnr((double)cpi->total_samples, peak,
                              (double)cpi->total_inconsistency);

          SNPRINT(headings, "\tConsist\tWstCons");
          SNPRINT2(results, "\t%7.3f", consistency);
          SNPRINT2(results, "\t%7.3f", cpi->worst_consistency);
        }

        fprintf(f, "%s\t    Time\n", headings);
        fprintf(f, "%s\t%8.0f\n", results, total_encode_time);
      }

      fclose(f);
    }

#endif

#if 0
    {
      printf("\n_pick_loop_filter_level:%d\n", cpi->time_pick_lpf / 1000);
      printf("\n_frames recive_data encod_mb_row compress_frame  Total\n");
      printf("%6d %10ld %10ld %10ld %10ld\n", cpi->common.current_video_frame,
             cpi->time_receive_data / 1000, cpi->time_encode_sb_row / 1000,
             cpi->time_compress_data / 1000,
             (cpi->time_receive_data + cpi->time_compress_data) / 1000);
    }
#endif
  }

#if CONFIG_VP9_TEMPORAL_DENOISING
  vp10_denoiser_free(&(cpi->denoiser));
#endif

  for (t = 0; t < cpi->num_workers; ++t) {
    VPxWorker *const worker = &cpi->workers[t];
    EncWorkerData *const thread_data = &cpi->tile_thr_data[t];

    // Deallocate allocated threads.
    vpx_get_worker_interface()->end(worker);

    // Deallocate allocated thread data.
    if (t < cpi->num_workers - 1) {
      if (cpi->common.allow_screen_content_tools)
        vpx_free(thread_data->td->mb.palette_buffer);
      vpx_free(thread_data->td->counts);
      vp10_free_pc_tree(thread_data->td);
      vpx_free(thread_data->td);
    }
  }
  vpx_free(cpi->tile_thr_data);
  vpx_free(cpi->workers);

  if (cpi->num_workers > 1)
    vp10_loop_filter_dealloc(&cpi->lf_row_sync);

  dealloc_compressor_data(cpi);

  for (i = 0; i < sizeof(cpi->mbgraph_stats) /
                  sizeof(cpi->mbgraph_stats[0]); ++i) {
    vpx_free(cpi->mbgraph_stats[i].mb_stats);
  }

#if CONFIG_FP_MB_STATS
  if (cpi->use_fp_mb_stats) {
    vpx_free(cpi->twopass.frame_mb_stats_buf);
    cpi->twopass.frame_mb_stats_buf = NULL;
  }
#endif

  vp10_remove_common(cm);
  vp10_free_ref_frame_buffers(cm->buffer_pool);
#if CONFIG_VP9_POSTPROC
  vp10_free_postproc_buffers(cm);
#endif
  vpx_free(cpi);

#if CONFIG_VP9_TEMPORAL_DENOISING
#ifdef OUTPUT_YUV_DENOISED
  fclose(yuv_denoised_file);
#endif
#endif
#ifdef OUTPUT_YUV_SKINMAP
  fclose(yuv_skinmap_file);
#endif
#ifdef OUTPUT_YUV_REC
  fclose(yuv_rec_file);
#endif

#if 0

  if (keyfile)
    fclose(keyfile);

  if (framepsnr)
    fclose(framepsnr);

  if (kf_list)
    fclose(kf_list);

#endif
}


static void generate_psnr_packet(VP10_COMP *cpi) {
  struct vpx_codec_cx_pkt pkt;
  int i;
  PSNR_STATS psnr;
#if CONFIG_VP9_HIGHBITDEPTH
  vpx_calc_highbd_psnr(cpi->Source, cpi->common.frame_to_show, &psnr,
                       cpi->td.mb.e_mbd.bd, cpi->oxcf.input_bit_depth);
#else
  vpx_calc_psnr(cpi->Source, cpi->common.frame_to_show, &psnr);
#endif

  for (i = 0; i < 4; ++i) {
    pkt.data.psnr.samples[i] = psnr.samples[i];
    pkt.data.psnr.sse[i] = psnr.sse[i];
    pkt.data.psnr.psnr[i] = psnr.psnr[i];
  }
  pkt.kind = VPX_CODEC_PSNR_PKT;
  vpx_codec_pkt_list_add(cpi->output_pkt_list, &pkt);
}

int vp10_use_as_reference(VP10_COMP *cpi, int ref_frame_flags) {
  if (ref_frame_flags > ((1 << REFS_PER_FRAME) - 1))
    return -1;

  cpi->ref_frame_flags = ref_frame_flags;
  return 0;
}

void vp10_update_reference(VP10_COMP *cpi, int ref_frame_flags) {
  cpi->ext_refresh_golden_frame = (ref_frame_flags & VP9_GOLD_FLAG) != 0;
  cpi->ext_refresh_alt_ref_frame = (ref_frame_flags & VP9_ALT_FLAG) != 0;
#if CONFIG_EXT_REFS
  cpi->ext_refresh_last_frames[0] = (ref_frame_flags & VP9_LAST_FLAG) != 0;
  cpi->ext_refresh_last_frames[1] = (ref_frame_flags & VP9_LAST2_FLAG) != 0;
  cpi->ext_refresh_last_frames[2] = (ref_frame_flags & VP9_LAST3_FLAG) != 0;
  cpi->ext_refresh_last_frames[3] = (ref_frame_flags & VP9_LAST4_FLAG) != 0;
#else
  cpi->ext_refresh_last_frame = (ref_frame_flags & VP9_LAST_FLAG) != 0;
#endif  // CONFIG_EXT_REFS
  cpi->ext_refresh_frame_flags_pending = 1;
}

static YV12_BUFFER_CONFIG *get_vp10_ref_frame_buffer(VP10_COMP *cpi,
                                VP9_REFFRAME ref_frame_flag) {
  MV_REFERENCE_FRAME ref_frame = NONE;
  if (ref_frame_flag == VP9_LAST_FLAG)
    ref_frame = LAST_FRAME;
#if CONFIG_EXT_REFS
  else if (ref_frame_flag == VP9_LAST2_FLAG)
    ref_frame = LAST2_FRAME;
  else if (ref_frame_flag == VP9_LAST3_FLAG)
    ref_frame = LAST3_FRAME;
  else if (ref_frame_flag == VP9_LAST4_FLAG)
    ref_frame = LAST4_FRAME;
#endif  // CONFIG_EXT_REFS
  else if (ref_frame_flag == VP9_GOLD_FLAG)
    ref_frame = GOLDEN_FRAME;
  else if (ref_frame_flag == VP9_ALT_FLAG)
    ref_frame = ALTREF_FRAME;

  return ref_frame == NONE ? NULL : get_ref_frame_buffer(cpi, ref_frame);
}

int vp10_copy_reference_enc(VP10_COMP *cpi, VP9_REFFRAME ref_frame_flag,
                           YV12_BUFFER_CONFIG *sd) {
  YV12_BUFFER_CONFIG *cfg = get_vp10_ref_frame_buffer(cpi, ref_frame_flag);
  if (cfg) {
    vp8_yv12_copy_frame(cfg, sd);
    return 0;
  } else {
    return -1;
  }
}

int vp10_set_reference_enc(VP10_COMP *cpi, VP9_REFFRAME ref_frame_flag,
                          YV12_BUFFER_CONFIG *sd) {
  YV12_BUFFER_CONFIG *cfg = get_vp10_ref_frame_buffer(cpi, ref_frame_flag);
  if (cfg) {
    vp8_yv12_copy_frame(sd, cfg);
    return 0;
  } else {
    return -1;
  }
}

int vp10_update_entropy(VP10_COMP * cpi, int update) {
  cpi->ext_refresh_frame_context = update;
  cpi->ext_refresh_frame_context_pending = 1;
  return 0;
}

#if defined(OUTPUT_YUV_DENOISED) || defined(OUTPUT_YUV_SKINMAP)
// The denoiser buffer is allocated as a YUV 440 buffer. This function writes it
// as YUV 420. We simply use the top-left pixels of the UV buffers, since we do
// not denoise the UV channels at this time. If ever we implement UV channel
// denoising we will have to modify this.
void vp10_write_yuv_frame_420(YV12_BUFFER_CONFIG *s, FILE *f) {
  uint8_t *src = s->y_buffer;
  int h = s->y_height;

  do {
    fwrite(src, s->y_width, 1, f);
    src += s->y_stride;
  } while (--h);

  src = s->u_buffer;
  h = s->uv_height;

  do {
    fwrite(src, s->uv_width, 1, f);
    src += s->uv_stride;
  } while (--h);

  src = s->v_buffer;
  h = s->uv_height;

  do {
    fwrite(src, s->uv_width, 1, f);
    src += s->uv_stride;
  } while (--h);
}
#endif

#ifdef OUTPUT_YUV_REC
void vp10_write_yuv_rec_frame(VP10_COMMON *cm) {
  YV12_BUFFER_CONFIG *s = cm->frame_to_show;
  uint8_t *src = s->y_buffer;
  int h = cm->height;

#if CONFIG_VP9_HIGHBITDEPTH
  if (s->flags & YV12_FLAG_HIGHBITDEPTH) {
    uint16_t *src16 = CONVERT_TO_SHORTPTR(s->y_buffer);

    do {
      fwrite(src16, s->y_width, 2,  yuv_rec_file);
      src16 += s->y_stride;
    } while (--h);

    src16 = CONVERT_TO_SHORTPTR(s->u_buffer);
    h = s->uv_height;

    do {
      fwrite(src16, s->uv_width, 2,  yuv_rec_file);
      src16 += s->uv_stride;
    } while (--h);

    src16 = CONVERT_TO_SHORTPTR(s->v_buffer);
    h = s->uv_height;

    do {
      fwrite(src16, s->uv_width, 2, yuv_rec_file);
      src16 += s->uv_stride;
    } while (--h);

    fflush(yuv_rec_file);
    return;
  }
#endif  // CONFIG_VP9_HIGHBITDEPTH

  do {
    fwrite(src, s->y_width, 1,  yuv_rec_file);
    src += s->y_stride;
  } while (--h);

  src = s->u_buffer;
  h = s->uv_height;

  do {
    fwrite(src, s->uv_width, 1,  yuv_rec_file);
    src += s->uv_stride;
  } while (--h);

  src = s->v_buffer;
  h = s->uv_height;

  do {
    fwrite(src, s->uv_width, 1, yuv_rec_file);
    src += s->uv_stride;
  } while (--h);

  fflush(yuv_rec_file);
}
#endif

#if CONFIG_VP9_HIGHBITDEPTH
static void scale_and_extend_frame_nonnormative(const YV12_BUFFER_CONFIG *src,
                                                YV12_BUFFER_CONFIG *dst,
                                                int bd) {
#else
static void scale_and_extend_frame_nonnormative(const YV12_BUFFER_CONFIG *src,
                                                YV12_BUFFER_CONFIG *dst) {
#endif  // CONFIG_VP9_HIGHBITDEPTH
  // TODO(dkovalev): replace YV12_BUFFER_CONFIG with vpx_image_t
  int i;
  const uint8_t *const srcs[3] = {src->y_buffer, src->u_buffer, src->v_buffer};
  const int src_strides[3] = {src->y_stride, src->uv_stride, src->uv_stride};
  const int src_widths[3] = {src->y_crop_width, src->uv_crop_width,
                             src->uv_crop_width };
  const int src_heights[3] = {src->y_crop_height, src->uv_crop_height,
                              src->uv_crop_height};
  uint8_t *const dsts[3] = {dst->y_buffer, dst->u_buffer, dst->v_buffer};
  const int dst_strides[3] = {dst->y_stride, dst->uv_stride, dst->uv_stride};
  const int dst_widths[3] = {dst->y_crop_width, dst->uv_crop_width,
                             dst->uv_crop_width};
  const int dst_heights[3] = {dst->y_crop_height, dst->uv_crop_height,
                              dst->uv_crop_height};

  for (i = 0; i < MAX_MB_PLANE; ++i) {
#if CONFIG_VP9_HIGHBITDEPTH
    if (src->flags & YV12_FLAG_HIGHBITDEPTH) {
      vp10_highbd_resize_plane(srcs[i], src_heights[i], src_widths[i],
                              src_strides[i], dsts[i], dst_heights[i],
                              dst_widths[i], dst_strides[i], bd);
    } else {
      vp10_resize_plane(srcs[i], src_heights[i], src_widths[i], src_strides[i],
                       dsts[i], dst_heights[i], dst_widths[i], dst_strides[i]);
    }
#else
    vp10_resize_plane(srcs[i], src_heights[i], src_widths[i], src_strides[i],
                     dsts[i], dst_heights[i], dst_widths[i], dst_strides[i]);
#endif  // CONFIG_VP9_HIGHBITDEPTH
  }
  vpx_extend_frame_borders(dst);
}

#if CONFIG_VP9_HIGHBITDEPTH
static void scale_and_extend_frame(const YV12_BUFFER_CONFIG *src,
                                   YV12_BUFFER_CONFIG *dst, int planes,
                                   int bd) {
#else
static void scale_and_extend_frame(const YV12_BUFFER_CONFIG *src,
                                   YV12_BUFFER_CONFIG *dst, int planes) {
#endif  // CONFIG_VP9_HIGHBITDEPTH
  const int src_w = src->y_crop_width;
  const int src_h = src->y_crop_height;
  const int dst_w = dst->y_crop_width;
  const int dst_h = dst->y_crop_height;
  const uint8_t *const srcs[3] = {src->y_buffer, src->u_buffer, src->v_buffer};
  const int src_strides[3] = {src->y_stride, src->uv_stride, src->uv_stride};
  uint8_t *const dsts[3] = {dst->y_buffer, dst->u_buffer, dst->v_buffer};
  const int dst_strides[3] = {dst->y_stride, dst->uv_stride, dst->uv_stride};
  const InterpFilterParams interp_filter_params =
      vp10_get_interp_filter_params(EIGHTTAP_REGULAR);
  const int16_t *kernel = interp_filter_params.filter_ptr;
  const int taps = interp_filter_params.taps;
  int x, y, i;

  for (y = 0; y < dst_h; y += 16) {
    for (x = 0; x < dst_w; x += 16) {
      for (i = 0; i < planes; ++i) {
        const int factor = (i == 0 || i == 3 ? 1 : 2);
        const int x_q4 = x * (16 / factor) * src_w / dst_w;
        const int y_q4 = y * (16 / factor) * src_h / dst_h;
        const int src_stride = src_strides[i];
        const int dst_stride = dst_strides[i];
        const uint8_t *src_ptr = srcs[i] + (y / factor) * src_h / dst_h *
                                     src_stride + (x / factor) * src_w / dst_w;
        uint8_t *dst_ptr = dsts[i] + (y / factor) * dst_stride + (x / factor);

#if CONFIG_VP9_HIGHBITDEPTH
        if (src->flags & YV12_FLAG_HIGHBITDEPTH) {
          vpx_highbd_convolve8(src_ptr, src_stride, dst_ptr, dst_stride,
                               &kernel[(x_q4 & 0xf) * taps], 16 * src_w / dst_w,
                               &kernel[(y_q4 & 0xf) * taps], 16 * src_h / dst_h,
                               16 / factor, 16 / factor, bd);
        } else {
          vpx_scaled_2d(src_ptr, src_stride, dst_ptr, dst_stride,
                        &kernel[(x_q4 & 0xf) * taps], 16 * src_w / dst_w,
                        &kernel[(y_q4 & 0xf) * taps], 16 * src_h / dst_h,
                        16 / factor, 16 / factor);
        }
#else
        vpx_scaled_2d(src_ptr, src_stride, dst_ptr, dst_stride,
                      &kernel[(x_q4 & 0xf) * taps], 16 * src_w / dst_w,
                      &kernel[(y_q4 & 0xf) * taps], 16 * src_h / dst_h,
                      16 / factor, 16 / factor);
#endif  // CONFIG_VP9_HIGHBITDEPTH
      }
    }
  }

  if (planes == 1)
    vpx_extend_frame_borders_y(dst);
  else
    vpx_extend_frame_borders(dst);
}

static int scale_down(VP10_COMP *cpi, int q) {
  RATE_CONTROL *const rc = &cpi->rc;
  GF_GROUP *const gf_group = &cpi->twopass.gf_group;
  int scale = 0;
  assert(frame_is_kf_gf_arf(cpi));

  if (rc->frame_size_selector == UNSCALED &&
      q >= rc->rf_level_maxq[gf_group->rf_level[gf_group->index]]) {
    const int max_size_thresh = (int)(rate_thresh_mult[SCALE_STEP1]
        * VPXMAX(rc->this_frame_target, rc->avg_frame_bandwidth));
    scale = rc->projected_frame_size > max_size_thresh ? 1 : 0;
  }
  return scale;
}

// Function to test for conditions that indicate we should loop
// back and recode a frame.
static int recode_loop_test(VP10_COMP *cpi,
                            int high_limit, int low_limit,
                            int q, int maxq, int minq) {
  const RATE_CONTROL *const rc = &cpi->rc;
  const VP10EncoderConfig *const oxcf = &cpi->oxcf;
  const int frame_is_kfgfarf = frame_is_kf_gf_arf(cpi);
  int force_recode = 0;

  if ((rc->projected_frame_size >= rc->max_frame_bandwidth) ||
      (cpi->sf.recode_loop == ALLOW_RECODE) ||
      (frame_is_kfgfarf &&
       (cpi->sf.recode_loop == ALLOW_RECODE_KFARFGF))) {
    if (frame_is_kfgfarf &&
        (oxcf->resize_mode == RESIZE_DYNAMIC) &&
        scale_down(cpi, q)) {
        // Code this group at a lower resolution.
        cpi->resize_pending = 1;
        return 1;
    }

    // TODO(agrange) high_limit could be greater than the scale-down threshold.
    if ((rc->projected_frame_size > high_limit && q < maxq) ||
        (rc->projected_frame_size < low_limit && q > minq)) {
      force_recode = 1;
    } else if (cpi->oxcf.rc_mode == VPX_CQ) {
      // Deal with frame undershoot and whether or not we are
      // below the automatically set cq level.
      if (q > oxcf->cq_level &&
          rc->projected_frame_size < ((rc->this_frame_target * 7) >> 3)) {
        force_recode = 1;
      }
    }
  }
  return force_recode;
}

static INLINE int get_free_upsampled_ref_buf(EncRefCntBuffer *ubufs) {
  int i;

  for (i = 0; i < MAX_REF_FRAMES; i++) {
    if (!ubufs[i].ref_count) {
      return i;
    }
  }
  return INVALID_IDX;
}

// Up-sample 1 reference frame.
static INLINE int upsample_ref_frame(VP10_COMP *cpi,
                                     const YV12_BUFFER_CONFIG *const ref) {
  VP10_COMMON * const cm = &cpi->common;
  EncRefCntBuffer *ubufs = cpi->upsampled_ref_bufs;
  int new_uidx = get_free_upsampled_ref_buf(ubufs);

  if (new_uidx == INVALID_IDX) {
    return INVALID_IDX;
  } else {
    YV12_BUFFER_CONFIG *upsampled_ref = &ubufs[new_uidx].buf;

    // Can allocate buffer for Y plane only.
    if (upsampled_ref->buffer_alloc_sz < (ref->buffer_alloc_sz << 6))
      if (vpx_realloc_frame_buffer(upsampled_ref,
                                   (cm->width << 3), (cm->height << 3),
                                   cm->subsampling_x, cm->subsampling_y,
#if CONFIG_VP9_HIGHBITDEPTH
                                   cm->use_highbitdepth,
#endif
                                   (VP9_ENC_BORDER_IN_PIXELS << 3),
                                   cm->byte_alignment,
                                   NULL, NULL, NULL))
        vpx_internal_error(&cm->error, VPX_CODEC_MEM_ERROR,
                           "Failed to allocate up-sampled frame buffer");

    // Currently, only Y plane is up-sampled, U, V are not used.
#if CONFIG_VP9_HIGHBITDEPTH
    scale_and_extend_frame(ref, upsampled_ref, 1, (int)cm->bit_depth);
#else
    scale_and_extend_frame(ref, upsampled_ref, 1);
#endif
    return new_uidx;
  }
}

void vp10_update_reference_frames(VP10_COMP *cpi) {
  VP10_COMMON * const cm = &cpi->common;
  BufferPool *const pool = cm->buffer_pool;
  const int use_upsampled_ref = cpi->sf.use_upsampled_references;
  int new_uidx = 0;

#if CONFIG_EXT_REFS
  int ref_frame;
#endif  // CONFIG_EXT_REFS

  if (use_upsampled_ref) {
    // Up-sample the current encoded frame.
    RefCntBuffer *bufs = pool->frame_bufs;
    const YV12_BUFFER_CONFIG *const ref = &bufs[cm->new_fb_idx].buf;

    new_uidx = upsample_ref_frame(cpi, ref);
  }

  // At this point the new frame has been encoded.
  // If any buffer copy / swapping is signaled it should be done here.
  if (cm->frame_type == KEY_FRAME) {
    ref_cnt_fb(pool->frame_bufs,
               &cm->ref_frame_map[cpi->gld_fb_idx], cm->new_fb_idx);
    ref_cnt_fb(pool->frame_bufs,
               &cm->ref_frame_map[cpi->alt_fb_idx], cm->new_fb_idx);

    if (use_upsampled_ref) {
      uref_cnt_fb(cpi->upsampled_ref_bufs,
                  &cpi->upsampled_ref_idx[cpi->gld_fb_idx], new_uidx);
      uref_cnt_fb(cpi->upsampled_ref_bufs,
                  &cpi->upsampled_ref_idx[cpi->alt_fb_idx], new_uidx);
    }
  } else if (vp10_preserve_existing_gf(cpi)) {
    // We have decided to preserve the previously existing golden frame as our
    // new ARF frame. However, in the short term in function
    // vp10_bitstream.c::get_refresh_mask() we left it in the GF slot and, if
    // we're updating the GF with the current decoded frame, we save it to the
    // ARF slot instead.
    // We now have to update the ARF with the current frame and swap gld_fb_idx
    // and alt_fb_idx so that, overall, we've stored the old GF in the new ARF
    // slot and, if we're updating the GF, the current frame becomes the new GF.
    int tmp;

    ref_cnt_fb(pool->frame_bufs,
               &cm->ref_frame_map[cpi->alt_fb_idx], cm->new_fb_idx);
    if (use_upsampled_ref)
      uref_cnt_fb(cpi->upsampled_ref_bufs,
                  &cpi->upsampled_ref_idx[cpi->alt_fb_idx], new_uidx);

    tmp = cpi->alt_fb_idx;
    cpi->alt_fb_idx = cpi->gld_fb_idx;
    cpi->gld_fb_idx = tmp;
  } else { /* For non key/golden frames */
    if (cpi->refresh_alt_ref_frame) {
      int arf_idx = cpi->alt_fb_idx;
      if ((cpi->oxcf.pass == 2) && cpi->multi_arf_allowed) {
        const GF_GROUP *const gf_group = &cpi->twopass.gf_group;
        arf_idx = gf_group->arf_update_idx[gf_group->index];
      }

      ref_cnt_fb(pool->frame_bufs,
                 &cm->ref_frame_map[arf_idx], cm->new_fb_idx);
      if (use_upsampled_ref)
        uref_cnt_fb(cpi->upsampled_ref_bufs,
                    &cpi->upsampled_ref_idx[cpi->alt_fb_idx], new_uidx);

      memcpy(cpi->interp_filter_selected[ALTREF_FRAME],
             cpi->interp_filter_selected[0],
             sizeof(cpi->interp_filter_selected[0]));
    }

    if (cpi->refresh_golden_frame) {
      ref_cnt_fb(pool->frame_bufs,
                 &cm->ref_frame_map[cpi->gld_fb_idx], cm->new_fb_idx);
      if (use_upsampled_ref)
        uref_cnt_fb(cpi->upsampled_ref_bufs,
                    &cpi->upsampled_ref_idx[cpi->gld_fb_idx], new_uidx);

      if (!cpi->rc.is_src_frame_alt_ref)
        memcpy(cpi->interp_filter_selected[GOLDEN_FRAME],
               cpi->interp_filter_selected[0],
               sizeof(cpi->interp_filter_selected[0]));
      else
        memcpy(cpi->interp_filter_selected[GOLDEN_FRAME],
               cpi->interp_filter_selected[ALTREF_FRAME],
               sizeof(cpi->interp_filter_selected[ALTREF_FRAME]));
    }
  }

#if CONFIG_EXT_REFS
  for (ref_frame = LAST_FRAME; ref_frame <= LAST4_FRAME; ++ref_frame) {
    const int ref_idx = ref_frame - LAST_FRAME;
    if (cpi->refresh_last_frames[ref_idx]) {
      ref_cnt_fb(pool->frame_bufs,
                 &cm->ref_frame_map[cpi->lst_fb_idxes[ref_idx]],
                 cm->new_fb_idx);
      if (!cpi->rc.is_src_frame_alt_ref) {
        memcpy(cpi->interp_filter_selected[ref_frame],
               cpi->interp_filter_selected[0],
               sizeof(cpi->interp_filter_selected[0]));
      }
    }
  }
  // NOTE: The order for the refreshing of the 4 last reference frames are:
  // LAST_FRAME -> LAST2_FRAME -> LAST3_FRAME -> LAST4_FRAME -> LAST_FRAME
  cpi->last_ref_to_refresh += 1;
  if (cpi->last_ref_to_refresh == LAST4_FRAME)
    cpi->last_ref_to_refresh = LAST_FRAME;
#else
  if (cpi->refresh_last_frame) {
    ref_cnt_fb(pool->frame_bufs,
               &cm->ref_frame_map[cpi->lst_fb_idx], cm->new_fb_idx);
    if (use_upsampled_ref)
      uref_cnt_fb(cpi->upsampled_ref_bufs,
                  &cpi->upsampled_ref_idx[cpi->lst_fb_idx], new_uidx);

    if (!cpi->rc.is_src_frame_alt_ref) {
      memcpy(cpi->interp_filter_selected[LAST_FRAME],
             cpi->interp_filter_selected[0],
             sizeof(cpi->interp_filter_selected[0]));
    }
  }
#endif  // CONFIG_EXT_REFS

#if CONFIG_VP9_TEMPORAL_DENOISING
  if (cpi->oxcf.noise_sensitivity > 0) {
    vp10_denoiser_update_frame_info(&cpi->denoiser,
                                   *cpi->Source,
                                   cpi->common.frame_type,
#if CONFIG_EXT_REFS
                                   cpi->refresh_last_frames,
#else
                                   cpi->refresh_last_frame,
#endif  // CONFIG_EXT_REFS
                                   cpi->refresh_alt_ref_frame,
                                   cpi->refresh_golden_frame);
  }
#endif
}

static void loopfilter_frame(VP10_COMP *cpi, VP10_COMMON *cm) {
  MACROBLOCKD *xd = &cpi->td.mb.e_mbd;
  struct loopfilter *lf = &cm->lf;
  if (is_lossless_requested(&cpi->oxcf)) {
    lf->filter_level = 0;
  } else {
    struct vpx_usec_timer timer;

    vpx_clear_system_state();

    vpx_usec_timer_start(&timer);

#if CONFIG_LOOP_RESTORATION
    vp10_pick_filter_restoration(cpi->Source, cpi, cpi->sf.lpf_pick);
#else
    vp10_pick_filter_level(cpi->Source, cpi, cpi->sf.lpf_pick);
#endif  // CONFIG_LOOP_RESTORATION

    vpx_usec_timer_mark(&timer);
    cpi->time_pick_lpf += vpx_usec_timer_elapsed(&timer);
  }

  if (lf->filter_level > 0) {
#if CONFIG_VAR_TX
    vp10_loop_filter_frame(cm->frame_to_show, cm, xd, lf->filter_level, 0, 0);
#else
    if (cpi->num_workers > 1)
      vp10_loop_filter_frame_mt(cm->frame_to_show, cm, xd->plane,
                               lf->filter_level, 0, 0,
                               cpi->workers, cpi->num_workers,
                               &cpi->lf_row_sync);
    else
      vp10_loop_filter_frame(cm->frame_to_show, cm, xd, lf->filter_level, 0, 0);
#endif
  }
#if CONFIG_LOOP_RESTORATION
  if (cm->rst_info.restoration_type != RESTORE_NONE) {
    vp10_loop_restoration_init(&cm->rst_internal, &cm->rst_info,
                               cm->frame_type == KEY_FRAME);
    vp10_loop_restoration_rows(cm->frame_to_show, cm, 0, cm->mi_rows, 0);
  }
#endif  // CONFIG_LOOP_RESTORATION

  vpx_extend_frame_inner_borders(cm->frame_to_show);
}

static INLINE void alloc_frame_mvs(VP10_COMMON *const cm,
                                   int buffer_idx) {
  RefCntBuffer *const new_fb_ptr = &cm->buffer_pool->frame_bufs[buffer_idx];
  if (new_fb_ptr->mvs == NULL ||
      new_fb_ptr->mi_rows < cm->mi_rows ||
      new_fb_ptr->mi_cols < cm->mi_cols) {
    vpx_free(new_fb_ptr->mvs);
    CHECK_MEM_ERROR(cm, new_fb_ptr->mvs,
                    (MV_REF *)vpx_calloc(cm->mi_rows * cm->mi_cols,
                                         sizeof(*new_fb_ptr->mvs)));
    new_fb_ptr->mi_rows = cm->mi_rows;
    new_fb_ptr->mi_cols = cm->mi_cols;
  }
}

void vp10_scale_references(VP10_COMP *cpi) {
  VP10_COMMON *cm = &cpi->common;
  MV_REFERENCE_FRAME ref_frame;
  const VP9_REFFRAME ref_mask[REFS_PER_FRAME] = {
    VP9_LAST_FLAG,
#if CONFIG_EXT_REFS
    VP9_LAST2_FLAG,
    VP9_LAST3_FLAG,
    VP9_LAST4_FLAG,
#endif  // CONFIG_EXT_REFS
    VP9_GOLD_FLAG,
    VP9_ALT_FLAG
  };

  for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
    // Need to convert from VP9_REFFRAME to index into ref_mask (subtract 1).
    if (cpi->ref_frame_flags & ref_mask[ref_frame - 1]) {
      BufferPool *const pool = cm->buffer_pool;
      const YV12_BUFFER_CONFIG *const ref = get_ref_frame_buffer(cpi,
                                                                 ref_frame);

      if (ref == NULL) {
        cpi->scaled_ref_idx[ref_frame - 1] = INVALID_IDX;
        continue;
      }

#if CONFIG_VP9_HIGHBITDEPTH
      if (ref->y_crop_width != cm->width || ref->y_crop_height != cm->height) {
        RefCntBuffer *new_fb_ptr = NULL;
        int force_scaling = 0;
        int new_fb = cpi->scaled_ref_idx[ref_frame - 1];
        if (new_fb == INVALID_IDX) {
          new_fb = get_free_fb(cm);
          force_scaling = 1;
        }
        if (new_fb == INVALID_IDX)
          return;
        new_fb_ptr = &pool->frame_bufs[new_fb];
        if (force_scaling ||
            new_fb_ptr->buf.y_crop_width != cm->width ||
            new_fb_ptr->buf.y_crop_height != cm->height) {
          if (vpx_realloc_frame_buffer(&new_fb_ptr->buf, cm->width, cm->height,
                                       cm->subsampling_x, cm->subsampling_y,
                                       cm->use_highbitdepth,
                                       VP9_ENC_BORDER_IN_PIXELS,
                                       cm->byte_alignment, NULL, NULL, NULL))
            vpx_internal_error(&cm->error, VPX_CODEC_MEM_ERROR,
                               "Failed to allocate frame buffer");
          scale_and_extend_frame(ref, &new_fb_ptr->buf, MAX_MB_PLANE,
                                 (int)cm->bit_depth);
          cpi->scaled_ref_idx[ref_frame - 1] = new_fb;
          alloc_frame_mvs(cm, new_fb);
        }
#else
      if (ref->y_crop_width != cm->width || ref->y_crop_height != cm->height) {
        RefCntBuffer *new_fb_ptr = NULL;
        int force_scaling = 0;
        int new_fb = cpi->scaled_ref_idx[ref_frame - 1];
        if (new_fb == INVALID_IDX) {
          new_fb = get_free_fb(cm);
          force_scaling = 1;
        }
        if (new_fb == INVALID_IDX)
          return;
        new_fb_ptr = &pool->frame_bufs[new_fb];
        if (force_scaling ||
            new_fb_ptr->buf.y_crop_width != cm->width ||
            new_fb_ptr->buf.y_crop_height != cm->height) {
          if (vpx_realloc_frame_buffer(&new_fb_ptr->buf, cm->width, cm->height,
                                       cm->subsampling_x, cm->subsampling_y,
                                       VP9_ENC_BORDER_IN_PIXELS,
                                       cm->byte_alignment, NULL, NULL, NULL))
            vpx_internal_error(&cm->error, VPX_CODEC_MEM_ERROR,
                               "Failed to allocate frame buffer");
          scale_and_extend_frame(ref, &new_fb_ptr->buf, MAX_MB_PLANE);
          cpi->scaled_ref_idx[ref_frame - 1] = new_fb;
          alloc_frame_mvs(cm, new_fb);
        }
#endif  // CONFIG_VP9_HIGHBITDEPTH

        if (cpi->sf.use_upsampled_references && (force_scaling ||
            new_fb_ptr->buf.y_crop_width != cm->width ||
            new_fb_ptr->buf.y_crop_height != cm->height)) {
          const int map_idx = get_ref_frame_map_idx(cpi, ref_frame);
          EncRefCntBuffer *ubuf =
              &cpi->upsampled_ref_bufs[cpi->upsampled_ref_idx[map_idx]];

          if (vpx_realloc_frame_buffer(&ubuf->buf,
                                       (cm->width << 3), (cm->height << 3),
                                       cm->subsampling_x, cm->subsampling_y,
#if CONFIG_VP9_HIGHBITDEPTH
                                       cm->use_highbitdepth,
#endif
                                       (VP9_ENC_BORDER_IN_PIXELS << 3),
                                       cm->byte_alignment,
                                       NULL, NULL, NULL))
            vpx_internal_error(&cm->error, VPX_CODEC_MEM_ERROR,
                               "Failed to allocate up-sampled frame buffer");
#if CONFIG_VP9_HIGHBITDEPTH
          scale_and_extend_frame(&new_fb_ptr->buf, &ubuf->buf, 1,
                                 (int)cm->bit_depth);
#else
          scale_and_extend_frame(&new_fb_ptr->buf, &ubuf->buf, 1);
#endif
        }
      } else {
        const int buf_idx = get_ref_frame_buf_idx(cpi, ref_frame);
        RefCntBuffer *const buf = &pool->frame_bufs[buf_idx];
        buf->buf.y_crop_width = ref->y_crop_width;
        buf->buf.y_crop_height = ref->y_crop_height;
        cpi->scaled_ref_idx[ref_frame - 1] = buf_idx;
        ++buf->ref_count;
      }
    } else {
      if (cpi->oxcf.pass != 0)
        cpi->scaled_ref_idx[ref_frame - 1] = INVALID_IDX;
    }
  }
}

static void release_scaled_references(VP10_COMP *cpi) {
  VP10_COMMON *cm = &cpi->common;
  int i;
  if (cpi->oxcf.pass == 0) {
    // Only release scaled references under certain conditions:
    // if reference will be updated, or if scaled reference has same resolution.
    int refresh[REFS_PER_FRAME];
#if CONFIG_EXT_REFS
    for (i = LAST_FRAME; i <= LAST4_FRAME; ++i)
      refresh[i - LAST_FRAME] =
          (cpi->refresh_last_frames[i - LAST_FRAME]) ? 1 : 0;
    refresh[4] = (cpi->refresh_golden_frame) ? 1 : 0;
    refresh[5] = (cpi->refresh_alt_ref_frame) ? 1 : 0;
#else
    refresh[0] = (cpi->refresh_last_frame) ? 1 : 0;
    refresh[1] = (cpi->refresh_golden_frame) ? 1 : 0;
    refresh[2] = (cpi->refresh_alt_ref_frame) ? 1 : 0;
#endif  // CONFIG_EXT_REFS
    for (i = LAST_FRAME; i <= ALTREF_FRAME; ++i) {
      const int idx = cpi->scaled_ref_idx[i - 1];
      RefCntBuffer *const buf = idx != INVALID_IDX ?
          &cm->buffer_pool->frame_bufs[idx] : NULL;
      const YV12_BUFFER_CONFIG *const ref = get_ref_frame_buffer(cpi, i);
      if (buf != NULL &&
          (refresh[i - 1] ||
          (buf->buf.y_crop_width == ref->y_crop_width &&
           buf->buf.y_crop_height == ref->y_crop_height))) {
        --buf->ref_count;
        cpi->scaled_ref_idx[i -1] = INVALID_IDX;
      }
    }
  } else {
    for (i = 0; i < MAX_REF_FRAMES; ++i) {
      const int idx = cpi->scaled_ref_idx[i];
      RefCntBuffer *const buf = idx != INVALID_IDX ?
          &cm->buffer_pool->frame_bufs[idx] : NULL;
      if (buf != NULL) {
        --buf->ref_count;
        cpi->scaled_ref_idx[i] = INVALID_IDX;
      }
    }
  }
}

static void full_to_model_count(unsigned int *model_count,
                                unsigned int *full_count) {
  int n;
  model_count[ZERO_TOKEN] = full_count[ZERO_TOKEN];
  model_count[ONE_TOKEN] = full_count[ONE_TOKEN];
  model_count[TWO_TOKEN] = full_count[TWO_TOKEN];
  for (n = THREE_TOKEN; n < EOB_TOKEN; ++n)
    model_count[TWO_TOKEN] += full_count[n];
  model_count[EOB_MODEL_TOKEN] = full_count[EOB_TOKEN];
}

#if CONFIG_ENTROPY
void full_to_model_counts(vp10_coeff_count_model *model_count,
                                 vp10_coeff_count *full_count) {
#else
static void full_to_model_counts(vp10_coeff_count_model *model_count,
                                 vp10_coeff_count *full_count) {
#endif  // CONFIG_ENTROPY
  int i, j, k, l;

  for (i = 0; i < PLANE_TYPES; ++i)
    for (j = 0; j < REF_TYPES; ++j)
      for (k = 0; k < COEF_BANDS; ++k)
        for (l = 0; l < BAND_COEFF_CONTEXTS(k); ++l)
          full_to_model_count(model_count[i][j][k][l], full_count[i][j][k][l]);
}

#if 0 && CONFIG_INTERNAL_STATS
static void output_frame_level_debug_stats(VP10_COMP *cpi) {
  VP10_COMMON *const cm = &cpi->common;
  FILE *const f = fopen("tmp.stt", cm->current_video_frame ? "a" : "w");
  int64_t recon_err;

  vpx_clear_system_state();

  recon_err = vpx_get_y_sse(cpi->Source, get_frame_new_buffer(cm));

  if (cpi->twopass.total_left_stats.coded_error != 0.0)
    fprintf(f, "%10u %dx%d  %10d %10d %d %d %10d %10d %10d %10d"
       "%10"PRId64" %10"PRId64" %5d %5d %10"PRId64" "
       "%10"PRId64" %10"PRId64" %10d "
       "%7.2lf %7.2lf %7.2lf %7.2lf %7.2lf"
        "%6d %6d %5d %5d %5d "
        "%10"PRId64" %10.3lf"
        "%10lf %8u %10"PRId64" %10d %10d %10d\n",
        cpi->common.current_video_frame,
        cm->width, cm->height,
        cpi->td.rd_counts.m_search_count,
        cpi->td.rd_counts.ex_search_count,
        cpi->rc.source_alt_ref_pending,
        cpi->rc.source_alt_ref_active,
        cpi->rc.this_frame_target,
        cpi->rc.projected_frame_size,
        cpi->rc.projected_frame_size / cpi->common.MBs,
        (cpi->rc.projected_frame_size - cpi->rc.this_frame_target),
        cpi->rc.vbr_bits_off_target,
        cpi->rc.vbr_bits_off_target_fast,
        cpi->twopass.extend_minq,
        cpi->twopass.extend_minq_fast,
        cpi->rc.total_target_vs_actual,
        (cpi->rc.starting_buffer_level - cpi->rc.bits_off_target),
        cpi->rc.total_actual_bits, cm->base_qindex,
        vp10_convert_qindex_to_q(cm->base_qindex, cm->bit_depth),
        (double)vp10_dc_quant(cm->base_qindex, 0, cm->bit_depth) / 4.0,
        vp10_convert_qindex_to_q(cpi->twopass.active_worst_quality,
                                cm->bit_depth),
        cpi->rc.avg_q,
        vp10_convert_qindex_to_q(cpi->oxcf.cq_level, cm->bit_depth),
        cpi->refresh_last_frame, cpi->refresh_golden_frame,
        cpi->refresh_alt_ref_frame, cm->frame_type, cpi->rc.gfu_boost,
        cpi->twopass.bits_left,
        cpi->twopass.total_left_stats.coded_error,
        cpi->twopass.bits_left /
            (1 + cpi->twopass.total_left_stats.coded_error),
        cpi->tot_recode_hits, recon_err, cpi->rc.kf_boost,
        cpi->twopass.kf_zeromotion_pct,
        cpi->twopass.fr_content_type);

  fclose(f);

  if (0) {
    FILE *const fmodes = fopen("Modes.stt", "a");
    int i;

    fprintf(fmodes, "%6d:%1d:%1d:%1d ", cpi->common.current_video_frame,
            cm->frame_type, cpi->refresh_golden_frame,
            cpi->refresh_alt_ref_frame);

    for (i = 0; i < MAX_MODES; ++i)
      fprintf(fmodes, "%5d ", cpi->mode_chosen_counts[i]);

    fprintf(fmodes, "\n");

    fclose(fmodes);
  }
}
#endif

static void set_mv_search_params(VP10_COMP *cpi) {
  const VP10_COMMON *const cm = &cpi->common;
  const unsigned int max_mv_def = VPXMIN(cm->width, cm->height);

  // Default based on max resolution.
  cpi->mv_step_param = vp10_init_search_range(max_mv_def);

  if (cpi->sf.mv.auto_mv_step_size) {
    if (frame_is_intra_only(cm)) {
      // Initialize max_mv_magnitude for use in the first INTER frame
      // after a key/intra-only frame.
      cpi->max_mv_magnitude = max_mv_def;
    } else {
      if (cm->show_frame) {
        // Allow mv_steps to correspond to twice the max mv magnitude found
        // in the previous frame, capped by the default max_mv_magnitude based
        // on resolution.
        cpi->mv_step_param = vp10_init_search_range(
            VPXMIN(max_mv_def, 2 * cpi->max_mv_magnitude));
      }
      cpi->max_mv_magnitude = 0;
    }
  }
}

static void set_size_independent_vars(VP10_COMP *cpi) {
  vp10_set_speed_features_framesize_independent(cpi);
  vp10_set_rd_speed_thresholds(cpi);
  vp10_set_rd_speed_thresholds_sub8x8(cpi);
  cpi->common.interp_filter = cpi->sf.default_interp_filter;
}

static void set_size_dependent_vars(VP10_COMP *cpi, int *q,
                                    int *bottom_index, int *top_index) {
  VP10_COMMON *const cm = &cpi->common;
  const VP10EncoderConfig *const oxcf = &cpi->oxcf;

  // Setup variables that depend on the dimensions of the frame.
  vp10_set_speed_features_framesize_dependent(cpi);

  // Decide q and q bounds.
  *q = vp10_rc_pick_q_and_bounds(cpi, bottom_index, top_index);

  if (!frame_is_intra_only(cm)) {
    vp10_set_high_precision_mv(cpi, (*q) < HIGH_PRECISION_MV_QTHRESH);
  }

  // Configure experimental use of segmentation for enhanced coding of
  // static regions if indicated.
  // Only allowed in the second pass of a two pass encode, as it requires
  // lagged coding, and if the relevant speed feature flag is set.
  if (oxcf->pass == 2 && cpi->sf.static_segmentation)
    configure_static_seg_features(cpi);

#if CONFIG_VP9_POSTPROC
  if (oxcf->noise_sensitivity > 0) {
    int l = 0;
    switch (oxcf->noise_sensitivity) {
      case 1:
        l = 20;
        break;
      case 2:
        l = 40;
        break;
      case 3:
        l = 60;
        break;
      case 4:
      case 5:
        l = 100;
        break;
      case 6:
        l = 150;
        break;
    }
    vp10_denoise(cpi->Source, cpi->Source, l);
  }
#endif  // CONFIG_VP9_POSTPROC
}

static void init_motion_estimation(VP10_COMP *cpi) {
  int y_stride = cpi->scaled_source.y_stride;

  if (cpi->sf.mv.search_method == NSTEP) {
    vp10_init3smotion_compensation(&cpi->ss_cfg, y_stride);
  } else if (cpi->sf.mv.search_method == DIAMOND) {
    vp10_init_dsmotion_compensation(&cpi->ss_cfg, y_stride);
  }
}

static void set_frame_size(VP10_COMP *cpi) {
  int ref_frame;
  VP10_COMMON *const cm = &cpi->common;
  VP10EncoderConfig *const oxcf = &cpi->oxcf;
  MACROBLOCKD *const xd = &cpi->td.mb.e_mbd;

  if (oxcf->pass == 2 &&
      oxcf->rc_mode == VPX_VBR &&
      ((oxcf->resize_mode == RESIZE_FIXED && cm->current_video_frame == 0) ||
        (oxcf->resize_mode == RESIZE_DYNAMIC && cpi->resize_pending))) {
    vp10_calculate_coded_size(
        cpi, &oxcf->scaled_frame_width, &oxcf->scaled_frame_height);

    // There has been a change in frame size.
    vp10_set_size_literal(cpi, oxcf->scaled_frame_width,
                         oxcf->scaled_frame_height);
  }

  if (oxcf->pass == 0 &&
      oxcf->rc_mode == VPX_CBR &&
      oxcf->resize_mode == RESIZE_DYNAMIC) {
      if (cpi->resize_pending == 1) {
        oxcf->scaled_frame_width =
            (cm->width * cpi->resize_scale_num) / cpi->resize_scale_den;
        oxcf->scaled_frame_height =
            (cm->height * cpi->resize_scale_num) /cpi->resize_scale_den;
      } else if (cpi->resize_pending == -1) {
        // Go back up to original size.
        oxcf->scaled_frame_width = oxcf->width;
        oxcf->scaled_frame_height = oxcf->height;
      }
      if (cpi->resize_pending != 0) {
        // There has been a change in frame size.
        vp10_set_size_literal(cpi,
                             oxcf->scaled_frame_width,
                             oxcf->scaled_frame_height);

        // TODO(agrange) Scale cpi->max_mv_magnitude if frame-size has changed.
        set_mv_search_params(cpi);
      }
  }

  if (oxcf->pass == 2) {
    vp10_set_target_rate(cpi);
  }

  alloc_frame_mvs(cm, cm->new_fb_idx);

  // Reset the frame pointers to the current frame size.
  if (vpx_realloc_frame_buffer(get_frame_new_buffer(cm), cm->width, cm->height,
                               cm->subsampling_x, cm->subsampling_y,
#if CONFIG_VP9_HIGHBITDEPTH
                               cm->use_highbitdepth,
#endif
                               VP9_ENC_BORDER_IN_PIXELS, cm->byte_alignment,
                               NULL, NULL, NULL))
    vpx_internal_error(&cm->error, VPX_CODEC_MEM_ERROR,
                       "Failed to allocate frame buffer");

  alloc_util_frame_buffers(cpi);
  init_motion_estimation(cpi);

  for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
    RefBuffer *const ref_buf = &cm->frame_refs[ref_frame - LAST_FRAME];
    const int buf_idx = get_ref_frame_buf_idx(cpi, ref_frame);

    ref_buf->idx = buf_idx;

    if (buf_idx != INVALID_IDX) {
      YV12_BUFFER_CONFIG *const buf = &cm->buffer_pool->frame_bufs[buf_idx].buf;
      ref_buf->buf = buf;
#if CONFIG_VP9_HIGHBITDEPTH
      vp10_setup_scale_factors_for_frame(&ref_buf->sf,
                                        buf->y_crop_width, buf->y_crop_height,
                                        cm->width, cm->height,
                                        (buf->flags & YV12_FLAG_HIGHBITDEPTH) ?
                                            1 : 0);
#else
      vp10_setup_scale_factors_for_frame(&ref_buf->sf,
                                        buf->y_crop_width, buf->y_crop_height,
                                        cm->width, cm->height);
#endif  // CONFIG_VP9_HIGHBITDEPTH
      if (vp10_is_scaled(&ref_buf->sf))
        vpx_extend_frame_borders(buf);
    } else {
      ref_buf->buf = NULL;
    }
  }

  set_ref_ptrs(cm, xd, LAST_FRAME, LAST_FRAME);
}

static void reset_use_upsampled_references(VP10_COMP *cpi) {
  MV_REFERENCE_FRAME ref_frame;

  // reset up-sampled reference buffer structure.
  init_upsampled_ref_frame_bufs(cpi);

  for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
    const YV12_BUFFER_CONFIG *const ref = get_ref_frame_buffer(cpi,
                                                               ref_frame);
    int new_uidx = upsample_ref_frame(cpi, ref);

    // Update the up-sampled reference index.
    cpi->upsampled_ref_idx[get_ref_frame_map_idx(cpi, ref_frame)] =
        new_uidx;
    cpi->upsampled_ref_bufs[new_uidx].ref_count++;
  }
}

static void encode_without_recode_loop(VP10_COMP *cpi) {
  VP10_COMMON *const cm = &cpi->common;
  int q = 0, bottom_index = 0, top_index = 0;  // Dummy variables.
  const int use_upsampled_ref = cpi->sf.use_upsampled_references;

  vpx_clear_system_state();

  set_frame_size(cpi);

  // For 1 pass CBR under dynamic resize mode: use faster scaling for source.
  // Only for 2x2 scaling for now.
  if (cpi->oxcf.pass == 0 &&
      cpi->oxcf.rc_mode == VPX_CBR &&
      cpi->oxcf.resize_mode == RESIZE_DYNAMIC &&
      cpi->un_scaled_source->y_width == (cm->width << 1) &&
      cpi->un_scaled_source->y_height == (cm->height << 1)) {
    cpi->Source = vp10_scale_if_required_fast(cm,
                                             cpi->un_scaled_source,
                                             &cpi->scaled_source);
    if (cpi->unscaled_last_source != NULL)
       cpi->Last_Source = vp10_scale_if_required_fast(cm,
                                                     cpi->unscaled_last_source,
                                                     &cpi->scaled_last_source);
  } else {
    cpi->Source = vp10_scale_if_required(cm, cpi->un_scaled_source,
                                        &cpi->scaled_source);
    if (cpi->unscaled_last_source != NULL)
      cpi->Last_Source = vp10_scale_if_required(cm, cpi->unscaled_last_source,
                                               &cpi->scaled_last_source);
  }

  if (frame_is_intra_only(cm) == 0) {
    vp10_scale_references(cpi);
  }

  set_size_independent_vars(cpi);
  set_size_dependent_vars(cpi, &q, &bottom_index, &top_index);

  // cpi->sf.use_upsampled_references can be different from frame to frame.
  // Every time when cpi->sf.use_upsampled_references is changed from 0 to 1.
  // The reference frames for this frame have to be up-sampled before encoding.
  if (!use_upsampled_ref && cpi->sf.use_upsampled_references)
    reset_use_upsampled_references(cpi);

  vp10_set_quantizer(cm, q);
  vp10_set_variance_partition_thresholds(cpi, q);

  setup_frame(cpi);

#if CONFIG_ENTROPY
  cm->do_subframe_update =
      cm->log2_tile_cols == 0 && cm->log2_tile_rows == 0;
  vp10_copy(cm->starting_coef_probs, cm->fc->coef_probs);
  vp10_copy(cpi->subframe_stats.enc_starting_coef_probs,
            cm->fc->coef_probs);
  cm->coef_probs_update_idx = 0;
  vp10_copy(cpi->subframe_stats.coef_probs_buf[0], cm->fc->coef_probs);
#endif  // CONFIG_ENTROPY

  suppress_active_map(cpi);
  // Variance adaptive and in frame q adjustment experiments are mutually
  // exclusive.
  if (cpi->oxcf.aq_mode == VARIANCE_AQ) {
    vp10_vaq_frame_setup(cpi);
  } else if (cpi->oxcf.aq_mode == COMPLEXITY_AQ) {
    vp10_setup_in_frame_q_adj(cpi);
  } else if (cpi->oxcf.aq_mode == CYCLIC_REFRESH_AQ) {
    vp10_cyclic_refresh_setup(cpi);
  }
  apply_active_map(cpi);

  // transform / motion compensation build reconstruction frame
  vp10_encode_frame(cpi);

  // Update some stats from cyclic refresh, and check if we should not update
  // golden reference, for 1 pass CBR.
  if (cpi->oxcf.aq_mode == CYCLIC_REFRESH_AQ &&
      cm->frame_type != KEY_FRAME &&
      (cpi->oxcf.pass == 0 && cpi->oxcf.rc_mode == VPX_CBR))
    vp10_cyclic_refresh_check_golden_update(cpi);

  // Update the skip mb flag probabilities based on the distribution
  // seen in the last encoder iteration.
  // update_base_skip_probs(cpi);
  vpx_clear_system_state();
}

static void encode_with_recode_loop(VP10_COMP *cpi,
                                    size_t *size,
                                    uint8_t *dest) {
  VP10_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;
  int bottom_index, top_index;
  int loop_count = 0;
  int loop_at_this_size = 0;
  int loop = 0;
  int overshoot_seen = 0;
  int undershoot_seen = 0;
  int frame_over_shoot_limit;
  int frame_under_shoot_limit;
  int q = 0, q_low = 0, q_high = 0;
  const int use_upsampled_ref = cpi->sf.use_upsampled_references;

  set_size_independent_vars(cpi);

  // cpi->sf.use_upsampled_references can be different from frame to frame.
  // Every time when cpi->sf.use_upsampled_references is changed from 0 to 1.
  // The reference frames for this frame have to be up-sampled before encoding.
  if (!use_upsampled_ref && cpi->sf.use_upsampled_references)
    reset_use_upsampled_references(cpi);

  do {
    vpx_clear_system_state();

    set_frame_size(cpi);

    if (loop_count == 0 || cpi->resize_pending != 0) {
      set_size_dependent_vars(cpi, &q, &bottom_index, &top_index);

      // TODO(agrange) Scale cpi->max_mv_magnitude if frame-size has changed.
      set_mv_search_params(cpi);

      // Reset the loop state for new frame size.
      overshoot_seen = 0;
      undershoot_seen = 0;

      // Reconfiguration for change in frame size has concluded.
      cpi->resize_pending = 0;

      q_low = bottom_index;
      q_high = top_index;

      loop_at_this_size = 0;
    }

    // Decide frame size bounds first time through.
    if (loop_count == 0) {
      vp10_rc_compute_frame_size_bounds(cpi, rc->this_frame_target,
                                       &frame_under_shoot_limit,
                                       &frame_over_shoot_limit);
    }

    cpi->Source = vp10_scale_if_required(cm, cpi->un_scaled_source,
                                      &cpi->scaled_source);

    if (cpi->unscaled_last_source != NULL)
      cpi->Last_Source = vp10_scale_if_required(cm, cpi->unscaled_last_source,
                                               &cpi->scaled_last_source);

    if (frame_is_intra_only(cm) == 0) {
      if (loop_count > 0) {
        release_scaled_references(cpi);
      }
      vp10_scale_references(cpi);
    }

    vp10_set_quantizer(cm, q);

    if (loop_count == 0)
      setup_frame(cpi);

#if CONFIG_ENTROPY
    // Base q-index may have changed, so we need to assign proper default coef
    // probs before every iteration.
    if (frame_is_intra_only(cm) || cm->error_resilient_mode) {
      int i;
      vp10_default_coef_probs(cm);
      if (cm->frame_type == KEY_FRAME || cm->error_resilient_mode ||
          cm->reset_frame_context == RESET_FRAME_CONTEXT_ALL) {
        for (i = 0; i < FRAME_CONTEXTS; ++i)
          cm->frame_contexts[i] = *cm->fc;
      } else if (cm->reset_frame_context == RESET_FRAME_CONTEXT_CURRENT) {
        cm->frame_contexts[cm->frame_context_idx] = *cm->fc;
      }
    }
#endif  // CONFIG_ENTROPY

#if CONFIG_ENTROPY
    cm->do_subframe_update =
        cm->log2_tile_cols == 0 && cm->log2_tile_rows == 0;
    if (loop_count == 0 || frame_is_intra_only(cm) ||
        cm->error_resilient_mode) {
      vp10_copy(cm->starting_coef_probs, cm->fc->coef_probs);
      vp10_copy(cpi->subframe_stats.enc_starting_coef_probs,
                cm->fc->coef_probs);
    } else {
      if (cm->do_subframe_update) {
        vp10_copy(cm->fc->coef_probs,
                  cpi->subframe_stats.enc_starting_coef_probs);
        vp10_copy(cm->starting_coef_probs,
                  cpi->subframe_stats.enc_starting_coef_probs);
        vp10_zero(cpi->subframe_stats.coef_counts_buf);
        vp10_zero(cpi->subframe_stats.eob_counts_buf);
      }
    }
    cm->coef_probs_update_idx = 0;
    vp10_copy(cpi->subframe_stats.coef_probs_buf[0], cm->fc->coef_probs);
#endif  // CONFIG_ENTROPY

    // Variance adaptive and in frame q adjustment experiments are mutually
    // exclusive.
    if (cpi->oxcf.aq_mode == VARIANCE_AQ) {
      vp10_vaq_frame_setup(cpi);
    } else if (cpi->oxcf.aq_mode == COMPLEXITY_AQ) {
      vp10_setup_in_frame_q_adj(cpi);
    }

    // transform / motion compensation build reconstruction frame
    vp10_encode_frame(cpi);

    // Update the skip mb flag probabilities based on the distribution
    // seen in the last encoder iteration.
    // update_base_skip_probs(cpi);

    vpx_clear_system_state();

    // Dummy pack of the bitstream using up to date stats to get an
    // accurate estimate of output frame size to determine if we need
    // to recode.
    if (cpi->sf.recode_loop >= ALLOW_RECODE_KFARFGF) {
      save_coding_context(cpi);
      vp10_pack_bitstream(cpi, dest, size);
      rc->projected_frame_size = (int)(*size) << 3;
      restore_coding_context(cpi);

      if (frame_over_shoot_limit == 0)
        frame_over_shoot_limit = 1;
    }

    if (cpi->oxcf.rc_mode == VPX_Q) {
      loop = 0;
    } else {
      if ((cm->frame_type == KEY_FRAME) &&
           rc->this_key_frame_forced &&
           (rc->projected_frame_size < rc->max_frame_bandwidth)) {
        int last_q = q;
        int64_t kf_err;

        int64_t high_err_target = cpi->ambient_err;
        int64_t low_err_target = cpi->ambient_err >> 1;

#if CONFIG_VP9_HIGHBITDEPTH
        if (cm->use_highbitdepth) {
          kf_err = vpx_highbd_get_y_sse(cpi->Source, get_frame_new_buffer(cm));
        } else {
          kf_err = vpx_get_y_sse(cpi->Source, get_frame_new_buffer(cm));
        }
#else
        kf_err = vpx_get_y_sse(cpi->Source, get_frame_new_buffer(cm));
#endif  // CONFIG_VP9_HIGHBITDEPTH

        // Prevent possible divide by zero error below for perfect KF
        kf_err += !kf_err;

        // The key frame is not good enough or we can afford
        // to make it better without undue risk of popping.
        if ((kf_err > high_err_target &&
             rc->projected_frame_size <= frame_over_shoot_limit) ||
            (kf_err > low_err_target &&
             rc->projected_frame_size <= frame_under_shoot_limit)) {
          // Lower q_high
          q_high = q > q_low ? q - 1 : q_low;

          // Adjust Q
          q = (int)((q * high_err_target) / kf_err);
          q = VPXMIN(q, (q_high + q_low) >> 1);
        } else if (kf_err < low_err_target &&
                   rc->projected_frame_size >= frame_under_shoot_limit) {
          // The key frame is much better than the previous frame
          // Raise q_low
          q_low = q < q_high ? q + 1 : q_high;

          // Adjust Q
          q = (int)((q * low_err_target) / kf_err);
          q = VPXMIN(q, (q_high + q_low + 1) >> 1);
        }

        // Clamp Q to upper and lower limits:
        q = clamp(q, q_low, q_high);

        loop = q != last_q;
      } else if (recode_loop_test(
          cpi, frame_over_shoot_limit, frame_under_shoot_limit,
          q, VPXMAX(q_high, top_index), bottom_index)) {
        // Is the projected frame size out of range and are we allowed
        // to attempt to recode.
        int last_q = q;
        int retries = 0;

        if (cpi->resize_pending == 1) {
          // Change in frame size so go back around the recode loop.
          cpi->rc.frame_size_selector =
              SCALE_STEP1 - cpi->rc.frame_size_selector;
          cpi->rc.next_frame_size_selector = cpi->rc.frame_size_selector;

#if CONFIG_INTERNAL_STATS
          ++cpi->tot_recode_hits;
#endif
          ++loop_count;
          loop = 1;
          continue;
        }

        // Frame size out of permitted range:
        // Update correction factor & compute new Q to try...

        // Frame is too large
        if (rc->projected_frame_size > rc->this_frame_target) {
          // Special case if the projected size is > the max allowed.
          if (rc->projected_frame_size >= rc->max_frame_bandwidth)
            q_high = rc->worst_quality;

          // Raise Qlow as to at least the current value
          q_low = q < q_high ? q + 1 : q_high;

          if (undershoot_seen || loop_at_this_size > 1) {
            // Update rate_correction_factor unless
            vp10_rc_update_rate_correction_factors(cpi);

            q = (q_high + q_low + 1) / 2;
          } else {
            // Update rate_correction_factor unless
            vp10_rc_update_rate_correction_factors(cpi);

            q = vp10_rc_regulate_q(cpi, rc->this_frame_target,
                                   bottom_index, VPXMAX(q_high, top_index));

            while (q < q_low && retries < 10) {
              vp10_rc_update_rate_correction_factors(cpi);
              q = vp10_rc_regulate_q(cpi, rc->this_frame_target,
                                     bottom_index, VPXMAX(q_high, top_index));
              retries++;
            }
          }

          overshoot_seen = 1;
        } else {
          // Frame is too small
          q_high = q > q_low ? q - 1 : q_low;

          if (overshoot_seen || loop_at_this_size > 1) {
            vp10_rc_update_rate_correction_factors(cpi);
            q = (q_high + q_low) / 2;
          } else {
            vp10_rc_update_rate_correction_factors(cpi);
            q = vp10_rc_regulate_q(cpi, rc->this_frame_target,
                                   bottom_index, top_index);
            // Special case reset for qlow for constrained quality.
            // This should only trigger where there is very substantial
            // undershoot on a frame and the auto cq level is above
            // the user passsed in value.
            if (cpi->oxcf.rc_mode == VPX_CQ &&
                q < q_low) {
              q_low = q;
            }

            while (q > q_high && retries < 10) {
              vp10_rc_update_rate_correction_factors(cpi);
              q = vp10_rc_regulate_q(cpi, rc->this_frame_target,
                                     bottom_index, top_index);
              retries++;
            }
          }

          undershoot_seen = 1;
        }

        // Clamp Q to upper and lower limits:
        q = clamp(q, q_low, q_high);

        loop = (q != last_q);
      } else {
        loop = 0;
      }
    }

    // Special case for overlay frame.
    if (rc->is_src_frame_alt_ref &&
        rc->projected_frame_size < rc->max_frame_bandwidth)
      loop = 0;

    if (loop) {
      ++loop_count;
      ++loop_at_this_size;

#if CONFIG_INTERNAL_STATS
      ++cpi->tot_recode_hits;
#endif
    }
  } while (loop);
}

static int get_ref_frame_flags(const VP10_COMP *cpi) {
  const int *const map = cpi->common.ref_frame_map;

#if CONFIG_EXT_REFS
  const int gld_is_last = map[cpi->gld_fb_idx] == map[cpi->lst_fb_idxes[0]];
  const int alt_is_last = map[cpi->alt_fb_idx] == map[cpi->lst_fb_idxes[0]];

  const int last2_is_last =
      map[cpi->lst_fb_idxes[1]] == map[cpi->lst_fb_idxes[0]];
  const int gld_is_last2 = map[cpi->gld_fb_idx] == map[cpi->lst_fb_idxes[1]];
  const int alt_is_last2 = map[cpi->alt_fb_idx] == map[cpi->lst_fb_idxes[1]];

  const int last3_is_last =
      map[cpi->lst_fb_idxes[2]] == map[cpi->lst_fb_idxes[0]];
  const int last3_is_last2 =
      map[cpi->lst_fb_idxes[2]] == map[cpi->lst_fb_idxes[1]];
  const int gld_is_last3 = map[cpi->gld_fb_idx] == map[cpi->lst_fb_idxes[2]];
  const int alt_is_last3 = map[cpi->alt_fb_idx] == map[cpi->lst_fb_idxes[2]];

  const int last4_is_last =
      map[cpi->lst_fb_idxes[3]] == map[cpi->lst_fb_idxes[0]];
  const int last4_is_last2 =
      map[cpi->lst_fb_idxes[3]] == map[cpi->lst_fb_idxes[1]];
  const int last4_is_last3 =
      map[cpi->lst_fb_idxes[3]] == map[cpi->lst_fb_idxes[2]];
  const int gld_is_last4 = map[cpi->gld_fb_idx] == map[cpi->lst_fb_idxes[3]];
  const int alt_is_last4 = map[cpi->alt_fb_idx] == map[cpi->lst_fb_idxes[3]];
#else
  const int gld_is_last = map[cpi->gld_fb_idx] == map[cpi->lst_fb_idx];
  const int alt_is_last = map[cpi->alt_fb_idx] == map[cpi->lst_fb_idx];
#endif  // CONFIG_EXT_REFS
  const int gld_is_alt = map[cpi->gld_fb_idx] == map[cpi->alt_fb_idx];

  int flags = VP9_ALT_FLAG | VP9_GOLD_FLAG | VP9_LAST_FLAG;
#if CONFIG_EXT_REFS
  flags |= VP9_LAST2_FLAG;
  flags |= VP9_LAST3_FLAG;
  flags |= VP9_LAST4_FLAG;
#endif  // CONFIG_EXT_REFS

  if (gld_is_last)
    flags &= ~VP9_GOLD_FLAG;

  if (cpi->rc.frames_till_gf_update_due == INT_MAX)
    flags &= ~VP9_GOLD_FLAG;

  if (alt_is_last)
    flags &= ~VP9_ALT_FLAG;

  if (gld_is_alt)
    flags &= ~VP9_ALT_FLAG;

#if CONFIG_EXT_REFS
  if (last4_is_last || last4_is_last2 || last4_is_last3)
    flags &= ~VP9_LAST4_FLAG;

  if (last3_is_last || last3_is_last2)
    flags &= ~VP9_LAST3_FLAG;

  if (last2_is_last)
    flags &= ~VP9_LAST2_FLAG;

  if (gld_is_last4 || gld_is_last3 || gld_is_last2)
    flags &= ~VP9_GOLD_FLAG;

  if (alt_is_last4 || alt_is_last3 || alt_is_last2)
    flags &= ~VP9_ALT_FLAG;
#endif  // CONFIG_EXT_REFS

  return flags;
}

static void set_ext_overrides(VP10_COMP *cpi) {
  // Overrides the defaults with the externally supplied values with
  // vp10_update_reference() and vp10_update_entropy() calls
  // Note: The overrides are valid only for the next frame passed
  // to encode_frame_to_data_rate() function
  if (cpi->ext_refresh_frame_context_pending) {
    cpi->common.refresh_frame_context = cpi->ext_refresh_frame_context;
    cpi->ext_refresh_frame_context_pending = 0;
  }
  if (cpi->ext_refresh_frame_flags_pending) {
#if CONFIG_EXT_REFS
    int ref_frame;
    for (ref_frame = LAST_FRAME; ref_frame <= LAST4_FRAME; ++ref_frame) {
      cpi->refresh_last_frames[ref_frame - LAST_FRAME] =
          cpi->ext_refresh_last_frames[ref_frame - LAST_FRAME];
    }
#else
    cpi->refresh_last_frame = cpi->ext_refresh_last_frame;
#endif  // CONFIG_EXT_REFS
    cpi->refresh_golden_frame = cpi->ext_refresh_golden_frame;
    cpi->refresh_alt_ref_frame = cpi->ext_refresh_alt_ref_frame;
    cpi->ext_refresh_frame_flags_pending = 0;
  }
}

YV12_BUFFER_CONFIG *vp10_scale_if_required_fast(VP10_COMMON *cm,
                                               YV12_BUFFER_CONFIG *unscaled,
                                               YV12_BUFFER_CONFIG *scaled) {
  if (cm->mi_cols * MI_SIZE != unscaled->y_width ||
      cm->mi_rows * MI_SIZE != unscaled->y_height) {
    // For 2x2 scaling down.
    vpx_scale_frame(unscaled, scaled, unscaled->y_buffer, 9, 2, 1,
                    2, 1, 0);
    vpx_extend_frame_borders(scaled);
    return scaled;
  } else {
    return unscaled;
  }
}

YV12_BUFFER_CONFIG *vp10_scale_if_required(VP10_COMMON *cm,
                                          YV12_BUFFER_CONFIG *unscaled,
                                          YV12_BUFFER_CONFIG *scaled) {
  if (cm->mi_cols * MI_SIZE != unscaled->y_width ||
      cm->mi_rows * MI_SIZE != unscaled->y_height) {
#if CONFIG_VP9_HIGHBITDEPTH
    scale_and_extend_frame_nonnormative(unscaled, scaled, (int)cm->bit_depth);
#else
    scale_and_extend_frame_nonnormative(unscaled, scaled);
#endif  // CONFIG_VP9_HIGHBITDEPTH
    return scaled;
  } else {
    return unscaled;
  }
}

static void set_arf_sign_bias(VP10_COMP *cpi) {
  VP10_COMMON *const cm = &cpi->common;
  int arf_sign_bias;

  if ((cpi->oxcf.pass == 2) && cpi->multi_arf_allowed) {
    const GF_GROUP *const gf_group = &cpi->twopass.gf_group;
    arf_sign_bias = cpi->rc.source_alt_ref_active &&
                    (!cpi->refresh_alt_ref_frame ||
                     (gf_group->rf_level[gf_group->index] == GF_ARF_LOW));
  } else {
    arf_sign_bias =
      (cpi->rc.source_alt_ref_active && !cpi->refresh_alt_ref_frame);
  }
  cm->ref_frame_sign_bias[ALTREF_FRAME] = arf_sign_bias;
}

static int setup_interp_filter_search_mask(VP10_COMP *cpi) {
  INTERP_FILTER ifilter;
  int ref_total[MAX_REF_FRAMES] = {0};
  MV_REFERENCE_FRAME ref;
  int mask = 0;
  if (cpi->common.last_frame_type == KEY_FRAME ||
      cpi->refresh_alt_ref_frame)
    return mask;
  for (ref = LAST_FRAME; ref <= ALTREF_FRAME; ++ref)
    for (ifilter = EIGHTTAP_REGULAR; ifilter < SWITCHABLE_FILTERS; ++ifilter)
      ref_total[ref] += cpi->interp_filter_selected[ref][ifilter];

  for (ifilter = EIGHTTAP_REGULAR; ifilter < SWITCHABLE_FILTERS; ++ifilter) {
    if ((ref_total[LAST_FRAME] &&
        cpi->interp_filter_selected[LAST_FRAME][ifilter] == 0) &&
#if CONFIG_EXT_REFS
        (ref_total[LAST2_FRAME] == 0 ||
         cpi->interp_filter_selected[LAST2_FRAME][ifilter] * 50
         < ref_total[LAST2_FRAME]) &&
        (ref_total[LAST3_FRAME] == 0 ||
         cpi->interp_filter_selected[LAST3_FRAME][ifilter] * 50
         < ref_total[LAST3_FRAME]) &&
        (ref_total[LAST4_FRAME] == 0 ||
         cpi->interp_filter_selected[LAST4_FRAME][ifilter] * 50
         < ref_total[LAST4_FRAME]) &&
#endif  // CONFIG_EXT_REFS
        (ref_total[GOLDEN_FRAME] == 0 ||
         cpi->interp_filter_selected[GOLDEN_FRAME][ifilter] * 50
           < ref_total[GOLDEN_FRAME]) &&
        (ref_total[ALTREF_FRAME] == 0 ||
         cpi->interp_filter_selected[ALTREF_FRAME][ifilter] * 50
           < ref_total[ALTREF_FRAME]))
      mask |= 1 << ifilter;
  }
  return mask;
}

static void encode_frame_to_data_rate(VP10_COMP *cpi,
                                      size_t *size,
                                      uint8_t *dest,
                                      unsigned int *frame_flags) {
  VP10_COMMON *const cm = &cpi->common;
  const VP10EncoderConfig *const oxcf = &cpi->oxcf;
  struct segmentation *const seg = &cm->seg;
  TX_SIZE t;

  set_ext_overrides(cpi);
  vpx_clear_system_state();

  // Set the arf sign bias for this frame.
  set_arf_sign_bias(cpi);

  // Set default state for segment based loop filter update flags.
  cm->lf.mode_ref_delta_update = 0;

  if (cpi->oxcf.pass == 2 &&
      cpi->sf.adaptive_interp_filter_search)
    cpi->sf.interp_filter_search_mask =
        setup_interp_filter_search_mask(cpi);

  // Set various flags etc to special state if it is a key frame.
  if (frame_is_intra_only(cm)) {
    // Reset the loop filter deltas and segmentation map.
    vp10_reset_segment_features(cm);

    // If segmentation is enabled force a map update for key frames.
    if (seg->enabled) {
      seg->update_map = 1;
      seg->update_data = 1;
    }

    // The alternate reference frame cannot be active for a key frame.
    cpi->rc.source_alt_ref_active = 0;

    cm->error_resilient_mode = oxcf->error_resilient_mode;

    // By default, encoder assumes decoder can use prev_mi.
    if (cm->error_resilient_mode) {
      cm->reset_frame_context = RESET_FRAME_CONTEXT_NONE;
      cm->refresh_frame_context = REFRESH_FRAME_CONTEXT_OFF;
    } else if (cm->intra_only) {
      // Only reset the current context.
      cm->reset_frame_context = RESET_FRAME_CONTEXT_CURRENT;
    }
  }

  // For 1 pass CBR, check if we are dropping this frame.
  // Never drop on key frame.
  if (oxcf->pass == 0 &&
      oxcf->rc_mode == VPX_CBR &&
      cm->frame_type != KEY_FRAME) {
    if (vp10_rc_drop_frame(cpi)) {
      vp10_rc_postencode_update_drop_frame(cpi);
      ++cm->current_video_frame;
      return;
    }
  }

  vpx_clear_system_state();

#if CONFIG_INTERNAL_STATS
  memset(cpi->mode_chosen_counts, 0,
         MAX_MODES * sizeof(*cpi->mode_chosen_counts));
#endif

  if (cpi->sf.recode_loop == DISALLOW_RECODE) {
    encode_without_recode_loop(cpi);
  } else {
    encode_with_recode_loop(cpi, size, dest);
  }

#if CONFIG_VP9_TEMPORAL_DENOISING
#ifdef OUTPUT_YUV_DENOISED
  if (oxcf->noise_sensitivity > 0) {
    vp10_write_yuv_frame_420(&cpi->denoiser.running_avg_y[INTRA_FRAME],
                            yuv_denoised_file);
  }
#endif
#endif
#ifdef OUTPUT_YUV_SKINMAP
  if (cpi->common.current_video_frame > 1) {
    vp10_compute_skin_map(cpi, yuv_skinmap_file);
  }
#endif

  // Special case code to reduce pulsing when key frames are forced at a
  // fixed interval. Note the reconstruction error if it is the frame before
  // the force key frame
  if (cpi->rc.next_key_frame_forced && cpi->rc.frames_to_key == 1) {
#if CONFIG_VP9_HIGHBITDEPTH
    if (cm->use_highbitdepth) {
      cpi->ambient_err = vpx_highbd_get_y_sse(cpi->Source,
                                              get_frame_new_buffer(cm));
    } else {
      cpi->ambient_err = vpx_get_y_sse(cpi->Source, get_frame_new_buffer(cm));
    }
#else
    cpi->ambient_err = vpx_get_y_sse(cpi->Source, get_frame_new_buffer(cm));
#endif  // CONFIG_VP9_HIGHBITDEPTH
  }

  // If the encoder forced a KEY_FRAME decision
  if (cm->frame_type == KEY_FRAME) {
#if CONFIG_EXT_REFS
    int ref_frame;
    for (ref_frame = LAST_FRAME; ref_frame <= LAST4_FRAME; ++ref_frame)
      cpi->refresh_last_frames[ref_frame - LAST_FRAME] = 1;
    cpi->last_ref_to_refresh = LAST_FRAME;
#else
    cpi->refresh_last_frame = 1;
#endif  // CONFIG_EXT_REFS
  }

  cm->frame_to_show = get_frame_new_buffer(cm);
  cm->frame_to_show->color_space = cm->color_space;
  cm->frame_to_show->color_range = cm->color_range;
  cm->frame_to_show->render_width  = cm->render_width;
  cm->frame_to_show->render_height = cm->render_height;

  // Pick the loop filter level for the frame.
  loopfilter_frame(cpi, cm);

  // build the bitstream
  vp10_pack_bitstream(cpi, dest, size);

  if (cm->seg.update_map)
    update_reference_segmentation_map(cpi);

  if (frame_is_intra_only(cm) == 0) {
    release_scaled_references(cpi);
  }
  vp10_update_reference_frames(cpi);

  for (t = TX_4X4; t <= TX_32X32; t++)
    full_to_model_counts(cpi->td.counts->coef[t],
                         cpi->td.rd_counts.coef_counts[t]);

  if (cm->refresh_frame_context == REFRESH_FRAME_CONTEXT_BACKWARD) {
#if CONFIG_ENTROPY
    cm->partial_prob_update = 0;
#endif  // CONFIG_ENTROPY
    vp10_adapt_coef_probs(cm);
    vp10_adapt_intra_frame_probs(cm);
  }

  if (!frame_is_intra_only(cm)) {
    if (cm->refresh_frame_context == REFRESH_FRAME_CONTEXT_BACKWARD) {
      vp10_adapt_inter_frame_probs(cm);
      vp10_adapt_mv_probs(cm, cm->allow_high_precision_mv);
    }
  }

  if (cpi->refresh_golden_frame == 1)
    cpi->frame_flags |= FRAMEFLAGS_GOLDEN;
  else
    cpi->frame_flags &= ~FRAMEFLAGS_GOLDEN;

  if (cpi->refresh_alt_ref_frame == 1)
    cpi->frame_flags |= FRAMEFLAGS_ALTREF;
  else
    cpi->frame_flags &= ~FRAMEFLAGS_ALTREF;

  cpi->ref_frame_flags = get_ref_frame_flags(cpi);

#if CONFIG_EXT_REFS
  cm->last3_frame_type = cm->last2_frame_type;
  cm->last2_frame_type = cm->last_frame_type;
#endif  // CONFIG_EXT_REFS
  cm->last_frame_type = cm->frame_type;

  vp10_rc_postencode_update(cpi, *size);

#if 0
  output_frame_level_debug_stats(cpi);
#endif

  if (cm->frame_type == KEY_FRAME) {
    // Tell the caller that the frame was coded as a key frame
    *frame_flags = cpi->frame_flags | FRAMEFLAGS_KEY;
  } else {
    *frame_flags = cpi->frame_flags & ~FRAMEFLAGS_KEY;
  }

  // Clear the one shot update flags for segmentation map and mode/ref loop
  // filter deltas.
  cm->seg.update_map = 0;
  cm->seg.update_data = 0;
  cm->lf.mode_ref_delta_update = 0;

  // keep track of the last coded dimensions
  cm->last_width = cm->width;
  cm->last_height = cm->height;

  // reset to normal state now that we are done.
  if (!cm->show_existing_frame)
    cm->last_show_frame = cm->show_frame;

  if (cm->show_frame) {
    vp10_swap_mi_and_prev_mi(cm);
    // Don't increment frame counters if this was an altref buffer
    // update not a real frame
    ++cm->current_video_frame;
  }
  cm->prev_frame = cm->cur_frame;
}

static void Pass0Encode(VP10_COMP *cpi, size_t *size, uint8_t *dest,
                        unsigned int *frame_flags) {
  if (cpi->oxcf.rc_mode == VPX_CBR) {
    vp10_rc_get_one_pass_cbr_params(cpi);
  } else {
    vp10_rc_get_one_pass_vbr_params(cpi);
  }
  encode_frame_to_data_rate(cpi, size, dest, frame_flags);
}

static void Pass2Encode(VP10_COMP *cpi, size_t *size,
                        uint8_t *dest, unsigned int *frame_flags) {
  cpi->allow_encode_breakout = ENCODE_BREAKOUT_ENABLED;
  encode_frame_to_data_rate(cpi, size, dest, frame_flags);

  vp10_twopass_postencode_update(cpi);
}

static void init_ref_frame_bufs(VP10_COMMON *cm) {
  int i;
  BufferPool *const pool = cm->buffer_pool;
  cm->new_fb_idx = INVALID_IDX;
  for (i = 0; i < REF_FRAMES; ++i) {
    cm->ref_frame_map[i] = INVALID_IDX;
    pool->frame_bufs[i].ref_count = 0;
  }
}

static void check_initial_width(VP10_COMP *cpi,
#if CONFIG_VP9_HIGHBITDEPTH
                                int use_highbitdepth,
#endif
                                int subsampling_x, int subsampling_y) {
  VP10_COMMON *const cm = &cpi->common;

  if (!cpi->initial_width ||
#if CONFIG_VP9_HIGHBITDEPTH
      cm->use_highbitdepth != use_highbitdepth ||
#endif
      cm->subsampling_x != subsampling_x ||
      cm->subsampling_y != subsampling_y) {
    cm->subsampling_x = subsampling_x;
    cm->subsampling_y = subsampling_y;
#if CONFIG_VP9_HIGHBITDEPTH
    cm->use_highbitdepth = use_highbitdepth;
#endif

    alloc_raw_frame_buffers(cpi);
    init_ref_frame_bufs(cm);
    alloc_util_frame_buffers(cpi);

    init_motion_estimation(cpi);  // TODO(agrange) This can be removed.

    cpi->initial_width = cm->width;
    cpi->initial_height = cm->height;
    cpi->initial_mbs = cm->MBs;
  }
}

#if CONFIG_VP9_TEMPORAL_DENOISING
static void setup_denoiser_buffer(VP10_COMP *cpi) {
  VP10_COMMON *const cm = &cpi->common;
  if (cpi->oxcf.noise_sensitivity > 0 &&
      !cpi->denoiser.frame_buffer_initialized) {
    if (vp10_denoiser_alloc(&cpi->denoiser, cm->width, cm->height,
                            cm->subsampling_x, cm->subsampling_y,
#if CONFIG_VP9_HIGHBITDEPTH
                            cm->use_highbitdepth,
#endif
                            VP9_ENC_BORDER_IN_PIXELS))
      vpx_internal_error(&cm->error, VPX_CODEC_MEM_ERROR,
                         "Failed to allocate denoiser");
  }
}
#endif

int vp10_receive_raw_frame(VP10_COMP *cpi, unsigned int frame_flags,
                          YV12_BUFFER_CONFIG *sd, int64_t time_stamp,
                          int64_t end_time) {
  VP10_COMMON *const cm = &cpi->common;
  struct vpx_usec_timer timer;
  int res = 0;
  const int subsampling_x = sd->subsampling_x;
  const int subsampling_y = sd->subsampling_y;
#if CONFIG_VP9_HIGHBITDEPTH
  const int use_highbitdepth = (sd->flags & YV12_FLAG_HIGHBITDEPTH) != 0;
#endif

#if CONFIG_VP9_HIGHBITDEPTH
  check_initial_width(cpi, use_highbitdepth, subsampling_x, subsampling_y);
#else
  check_initial_width(cpi, subsampling_x, subsampling_y);
#endif  // CONFIG_VP9_HIGHBITDEPTH

#if CONFIG_VP9_TEMPORAL_DENOISING
  setup_denoiser_buffer(cpi);
#endif
  vpx_usec_timer_start(&timer);

  if (vp10_lookahead_push(cpi->lookahead, sd, time_stamp, end_time,
#if CONFIG_VP9_HIGHBITDEPTH
                         use_highbitdepth,
#endif  // CONFIG_VP9_HIGHBITDEPTH
                         frame_flags))
    res = -1;
  vpx_usec_timer_mark(&timer);
  cpi->time_receive_data += vpx_usec_timer_elapsed(&timer);

  if ((cm->profile == PROFILE_0 || cm->profile == PROFILE_2) &&
      (subsampling_x != 1 || subsampling_y != 1)) {
    vpx_internal_error(&cm->error, VPX_CODEC_INVALID_PARAM,
                       "Non-4:2:0 color format requires profile 1 or 3");
    res = -1;
  }
  if ((cm->profile == PROFILE_1 || cm->profile == PROFILE_3) &&
      (subsampling_x == 1 && subsampling_y == 1)) {
    vpx_internal_error(&cm->error, VPX_CODEC_INVALID_PARAM,
                       "4:2:0 color format requires profile 0 or 2");
    res = -1;
  }

  return res;
}


static int frame_is_reference(const VP10_COMP *cpi) {
  const VP10_COMMON *cm = &cpi->common;

  return cm->frame_type == KEY_FRAME ||
#if CONFIG_EXT_REFS
         cpi->refresh_last_frames[LAST_FRAME - LAST_FRAME] ||
         cpi->refresh_last_frames[LAST2_FRAME - LAST_FRAME] ||
         cpi->refresh_last_frames[LAST3_FRAME - LAST_FRAME] ||
         cpi->refresh_last_frames[LAST4_FRAME - LAST_FRAME] ||
#else
         cpi->refresh_last_frame ||
#endif  // CONFIG_EXT_REFS
         cpi->refresh_golden_frame ||
         cpi->refresh_alt_ref_frame ||
         cm->refresh_frame_context != REFRESH_FRAME_CONTEXT_OFF ||
         cm->lf.mode_ref_delta_update ||
         cm->seg.update_map ||
         cm->seg.update_data;
}

static void adjust_frame_rate(VP10_COMP *cpi,
                              const struct lookahead_entry *source) {
  int64_t this_duration;
  int step = 0;

  if (source->ts_start == cpi->first_time_stamp_ever) {
    this_duration = source->ts_end - source->ts_start;
    step = 1;
  } else {
    int64_t last_duration = cpi->last_end_time_stamp_seen
        - cpi->last_time_stamp_seen;

    this_duration = source->ts_end - cpi->last_end_time_stamp_seen;

    // do a step update if the duration changes by 10%
    if (last_duration)
      step = (int)((this_duration - last_duration) * 10 / last_duration);
  }

  if (this_duration) {
    if (step) {
      vp10_new_framerate(cpi, 10000000.0 / this_duration);
    } else {
      // Average this frame's rate into the last second's average
      // frame rate. If we haven't seen 1 second yet, then average
      // over the whole interval seen.
      const double interval = VPXMIN(
          (double)(source->ts_end - cpi->first_time_stamp_ever), 10000000.0);
      double avg_duration = 10000000.0 / cpi->framerate;
      avg_duration *= (interval - avg_duration + this_duration);
      avg_duration /= interval;

      vp10_new_framerate(cpi, 10000000.0 / avg_duration);
    }
  }
  cpi->last_time_stamp_seen = source->ts_start;
  cpi->last_end_time_stamp_seen = source->ts_end;
}

// Returns 0 if this is not an alt ref else the offset of the source frame
// used as the arf midpoint.
static int get_arf_src_index(VP10_COMP *cpi) {
  RATE_CONTROL *const rc = &cpi->rc;
  int arf_src_index = 0;
  if (is_altref_enabled(cpi)) {
    if (cpi->oxcf.pass == 2) {
      const GF_GROUP *const gf_group = &cpi->twopass.gf_group;
      if (gf_group->update_type[gf_group->index] == ARF_UPDATE) {
        arf_src_index = gf_group->arf_src_offset[gf_group->index];
      }
    } else if (rc->source_alt_ref_pending) {
      arf_src_index = rc->frames_till_gf_update_due;
    }
  }
  return arf_src_index;
}

static void check_src_altref(VP10_COMP *cpi,
                             const struct lookahead_entry *source) {
  RATE_CONTROL *const rc = &cpi->rc;

  if (cpi->oxcf.pass == 2) {
    const GF_GROUP *const gf_group = &cpi->twopass.gf_group;
    rc->is_src_frame_alt_ref =
      (gf_group->update_type[gf_group->index] == OVERLAY_UPDATE);
  } else {
    rc->is_src_frame_alt_ref = cpi->alt_ref_source &&
                               (source == cpi->alt_ref_source);
  }

  if (rc->is_src_frame_alt_ref) {
#if CONFIG_EXT_REFS
    int ref_frame;
#endif  // CONFIG_EXT_REFS

    // Current frame is an ARF overlay frame.
    cpi->alt_ref_source = NULL;

    // Don't refresh the last buffer for an ARF overlay frame. It will
    // become the GF so preserve last as an alternative prediction option.
#if CONFIG_EXT_REFS
    for (ref_frame = LAST_FRAME; ref_frame <= LAST4_FRAME; ++ref_frame)
      cpi->refresh_last_frames[ref_frame - LAST_FRAME] = 0;
#else
    cpi->refresh_last_frame = 0;
#endif  // CONFIG_EXT_REFS
  }
}

#if CONFIG_INTERNAL_STATS
extern double vp10_get_blockiness(const unsigned char *img1, int img1_pitch,
                                 const unsigned char *img2, int img2_pitch,
                                 int width, int height);

static void adjust_image_stat(double y, double u, double v, double all,
                              ImageStat *s) {
  s->stat[Y] += y;
  s->stat[U] += u;
  s->stat[V] += v;
  s->stat[ALL] += all;
  s->worst = VPXMIN(s->worst, all);
}

static void compute_internal_stats(VP10_COMP *cpi) {
  VP10_COMMON *const cm = &cpi->common;
  double samples = 0.0;
  uint32_t in_bit_depth = 8;
  uint32_t bit_depth = 8;

#if CONFIG_VP9_HIGHBITDEPTH
  if (cm->use_highbitdepth) {
    in_bit_depth = cpi->oxcf.input_bit_depth;
    bit_depth = cm->bit_depth;
  }
#endif
  if (cm->show_frame) {
    const YV12_BUFFER_CONFIG *orig = cpi->Source;
    const YV12_BUFFER_CONFIG *recon = cpi->common.frame_to_show;
    double y, u, v, frame_all;

    cpi->count++;
    if (cpi->b_calculate_psnr) {
      PSNR_STATS psnr;
      double frame_ssim2 = 0.0, weight = 0.0;
      vpx_clear_system_state();
      // TODO(yaowu): unify these two versions into one.
#if CONFIG_VP9_HIGHBITDEPTH
      vpx_calc_highbd_psnr(orig, recon, &psnr, bit_depth, in_bit_depth);
#else
      vpx_calc_psnr(orig, recon, &psnr);
#endif  // CONFIG_VP9_HIGHBITDEPTH

      adjust_image_stat(psnr.psnr[1], psnr.psnr[2], psnr.psnr[3],
                        psnr.psnr[0], &cpi->psnr);
      cpi->total_sq_error += psnr.sse[0];
      cpi->total_samples += psnr.samples[0];
      samples = psnr.samples[0];
      // TODO(yaowu): unify these two versions into one.
#if CONFIG_VP9_HIGHBITDEPTH
      if (cm->use_highbitdepth)
        frame_ssim2 = vpx_highbd_calc_ssim(orig, recon, &weight,
                                           bit_depth, in_bit_depth);
      else
        frame_ssim2 = vpx_calc_ssim(orig, recon, &weight);
#else
      frame_ssim2 = vpx_calc_ssim(orig, recon, &weight);
#endif  // CONFIG_VP9_HIGHBITDEPTH

      cpi->worst_ssim= VPXMIN(cpi->worst_ssim, frame_ssim2);
      cpi->summed_quality += frame_ssim2 * weight;
      cpi->summed_weights += weight;

#if 0
      {
        FILE *f = fopen("q_used.stt", "a");
        fprintf(f, "%5d : Y%f7.3:U%f7.3:V%f7.3:F%f7.3:S%7.3f\n",
                cpi->common.current_video_frame, y2, u2, v2,
                frame_psnr2, frame_ssim2);
        fclose(f);
      }
#endif
    }
    if (cpi->b_calculate_blockiness) {
#if CONFIG_VP9_HIGHBITDEPTH
      if (!cm->use_highbitdepth)
#endif
      {
        const double frame_blockiness = vp10_get_blockiness(
            orig->y_buffer, orig->y_stride, recon->y_buffer, recon->y_stride,
            orig->y_width, orig->y_height);
        cpi->worst_blockiness = VPXMAX(cpi->worst_blockiness, frame_blockiness);
        cpi->total_blockiness += frame_blockiness;
      }

      if (cpi->b_calculate_consistency) {
#if CONFIG_VP9_HIGHBITDEPTH
        if (!cm->use_highbitdepth)
#endif
        {
          const double this_inconsistency = vpx_get_ssim_metrics(
              orig->y_buffer, orig->y_stride, recon->y_buffer, recon->y_stride,
              orig->y_width, orig->y_height, cpi->ssim_vars, &cpi->metrics, 1);

          const double peak = (double)((1 << in_bit_depth) - 1);
          const double consistency = vpx_sse_to_psnr(
               samples, peak, cpi->total_inconsistency);
          if (consistency > 0.0)
            cpi->worst_consistency =
                VPXMIN(cpi->worst_consistency, consistency);
          cpi->total_inconsistency += this_inconsistency;
        }
      }
    }

    frame_all = vpx_calc_fastssim(orig, recon, &y, &u, &v,
                                  bit_depth, in_bit_depth);
    adjust_image_stat(y, u, v, frame_all, &cpi->fastssim);
    frame_all = vpx_psnrhvs(orig, recon, &y, &u, &v, bit_depth, in_bit_depth);
    adjust_image_stat(y, u, v, frame_all, &cpi->psnrhvs);
  }
}
#endif  // CONFIG_INTERNAL_STATS

int vp10_get_compressed_data(VP10_COMP *cpi, unsigned int *frame_flags,
                            size_t *size, uint8_t *dest,
                            int64_t *time_stamp, int64_t *time_end, int flush) {
  const VP10EncoderConfig *const oxcf = &cpi->oxcf;
  VP10_COMMON *const cm = &cpi->common;
  BufferPool *const pool = cm->buffer_pool;
  RATE_CONTROL *const rc = &cpi->rc;
  struct vpx_usec_timer  cmptimer;
  YV12_BUFFER_CONFIG *force_src_buffer = NULL;
  struct lookahead_entry *last_source = NULL;
  struct lookahead_entry *source = NULL;
  int arf_src_index;
  int i;

  vpx_usec_timer_start(&cmptimer);

  vp10_set_high_precision_mv(cpi, ALTREF_HIGH_PRECISION_MV);

  // Is multi-arf enabled.
  // Note that at the moment multi_arf is only configured for 2 pass VBR
  if ((oxcf->pass == 2) && (cpi->oxcf.enable_auto_arf > 1))
    cpi->multi_arf_allowed = 1;
  else
    cpi->multi_arf_allowed = 0;

  // Normal defaults
  cm->reset_frame_context = RESET_FRAME_CONTEXT_NONE;
  cm->refresh_frame_context =
      oxcf->error_resilient_mode ? REFRESH_FRAME_CONTEXT_OFF :
          oxcf->frame_parallel_decoding_mode ? REFRESH_FRAME_CONTEXT_FORWARD
                                             : REFRESH_FRAME_CONTEXT_BACKWARD;

#if CONFIG_EXT_REFS
  for (i = LAST_FRAME; i <= LAST4_FRAME; ++i) {
    if (i == cpi->last_ref_to_refresh)
      cpi->refresh_last_frames[i - LAST_FRAME] = 1;
    else
      cpi->refresh_last_frames[i - LAST_FRAME] = 0;
  }
#else
  cpi->refresh_last_frame = 1;
#endif  // CONFIG_EXT_REFS
  cpi->refresh_golden_frame = 0;
  cpi->refresh_alt_ref_frame = 0;

  // Should we encode an arf frame.
  arf_src_index = get_arf_src_index(cpi);

  if (arf_src_index) {
    assert(arf_src_index <= rc->frames_to_key);

    if ((source = vp10_lookahead_peek(cpi->lookahead, arf_src_index)) != NULL) {
      cpi->alt_ref_source = source;

      if (oxcf->arnr_max_frames > 0) {
        // Produce the filtered ARF frame.
        vp10_temporal_filter(cpi, arf_src_index);
        vpx_extend_frame_borders(&cpi->alt_ref_buffer);
        force_src_buffer = &cpi->alt_ref_buffer;
      }

      cm->show_frame = 0;
      cm->intra_only = 0;
      cpi->refresh_alt_ref_frame = 1;
      cpi->refresh_golden_frame = 0;
#if CONFIG_EXT_REFS
      for (i = LAST_FRAME; i <= LAST4_FRAME; ++i)
        cpi->refresh_last_frames[i - LAST_FRAME] = 0;
#else
      cpi->refresh_last_frame = 0;
#endif  // CONFIG_EXT_REFS
      rc->is_src_frame_alt_ref = 0;
    }
    rc->source_alt_ref_pending = 0;
  }

  if (!source) {
    // Get last frame source.
    if (cm->current_video_frame > 0) {
      if ((last_source = vp10_lookahead_peek(cpi->lookahead, -1)) == NULL)
        return -1;
    }

    // Read in the source frame.
    source = vp10_lookahead_pop(cpi->lookahead, flush);

    if (source != NULL) {
      cm->show_frame = 1;
      cm->intra_only = 0;

      // Check to see if the frame should be encoded as an arf overlay.
      check_src_altref(cpi, source);
    }
  }

  if (source) {
    cpi->un_scaled_source = cpi->Source = force_src_buffer ? force_src_buffer
                                                           : &source->img;

    cpi->unscaled_last_source = last_source != NULL ? &last_source->img : NULL;

    *time_stamp = source->ts_start;
    *time_end = source->ts_end;
    *frame_flags = (source->flags & VPX_EFLAG_FORCE_KF) ? FRAMEFLAGS_KEY : 0;

  } else {
    *size = 0;
    if (flush && oxcf->pass == 1 && !cpi->twopass.first_pass_done) {
      vp10_end_first_pass(cpi);    /* get last stats packet */
      cpi->twopass.first_pass_done = 1;
    }
    return -1;
  }

  if (source->ts_start < cpi->first_time_stamp_ever) {
    cpi->first_time_stamp_ever = source->ts_start;
    cpi->last_end_time_stamp_seen = source->ts_start;
  }

  // Clear down mmx registers
  vpx_clear_system_state();

  // adjust frame rates based on timestamps given
  if (cm->show_frame) {
    adjust_frame_rate(cpi, source);
  }

  // Find a free buffer for the new frame, releasing the reference previously
  // held.
  if (cm->new_fb_idx != INVALID_IDX) {
    --pool->frame_bufs[cm->new_fb_idx].ref_count;
  }
  cm->new_fb_idx = get_free_fb(cm);

  if (cm->new_fb_idx == INVALID_IDX)
    return -1;

  cm->cur_frame = &pool->frame_bufs[cm->new_fb_idx];

  if (cpi->multi_arf_allowed) {
    if (cm->frame_type == KEY_FRAME) {
      init_buffer_indices(cpi);
    } else if (oxcf->pass == 2) {
      const GF_GROUP *const gf_group = &cpi->twopass.gf_group;
      cpi->alt_fb_idx = gf_group->arf_ref_idx[gf_group->index];
    }
  }

  // Start with a 0 size frame.
  *size = 0;

  cpi->frame_flags = *frame_flags;

  if (oxcf->pass == 2) {
    vp10_rc_get_second_pass_params(cpi);
  } else if (oxcf->pass == 1) {
    set_frame_size(cpi);
  }

  if (cpi->oxcf.pass != 0 || frame_is_intra_only(cm) == 1) {
    for (i = 0; i < MAX_REF_FRAMES; ++i)
      cpi->scaled_ref_idx[i] = INVALID_IDX;
  }

  if (oxcf->pass == 1) {
    cpi->td.mb.e_mbd.lossless[0] = is_lossless_requested(oxcf);
    vp10_first_pass(cpi, source);
  } else if (oxcf->pass == 2) {
    Pass2Encode(cpi, size, dest, frame_flags);
  } else {
    // One pass encode
    Pass0Encode(cpi, size, dest, frame_flags);
  }

  if (cm->refresh_frame_context != REFRESH_FRAME_CONTEXT_OFF)
    cm->frame_contexts[cm->frame_context_idx] = *cm->fc;

  // No frame encoded, or frame was dropped, release scaled references.
  if ((*size == 0) && (frame_is_intra_only(cm) == 0)) {
    release_scaled_references(cpi);
  }

  if (*size > 0) {
    cpi->droppable = !frame_is_reference(cpi);
  }

  vpx_usec_timer_mark(&cmptimer);
  cpi->time_compress_data += vpx_usec_timer_elapsed(&cmptimer);

  if (cpi->b_calculate_psnr && oxcf->pass != 1 && cm->show_frame)
    generate_psnr_packet(cpi);

#if CONFIG_INTERNAL_STATS
  if (oxcf->pass != 1) {
    compute_internal_stats(cpi);
    cpi->bytes += (int)(*size);
  }
#endif
  vpx_clear_system_state();
  return 0;
}

int vp10_get_preview_raw_frame(VP10_COMP *cpi, YV12_BUFFER_CONFIG *dest,
                              vp10_ppflags_t *flags) {
  VP10_COMMON *cm = &cpi->common;
#if !CONFIG_VP9_POSTPROC
  (void)flags;
#endif

  if (!cm->show_frame) {
    return -1;
  } else {
    int ret;
#if CONFIG_VP9_POSTPROC
    ret = vp10_post_proc_frame(cm, dest, flags);
#else
    if (cm->frame_to_show) {
      *dest = *cm->frame_to_show;
      dest->y_width = cm->width;
      dest->y_height = cm->height;
      dest->uv_width = cm->width >> cm->subsampling_x;
      dest->uv_height = cm->height >> cm->subsampling_y;
      ret = 0;
    } else {
      ret = -1;
    }
#endif  // !CONFIG_VP9_POSTPROC
    vpx_clear_system_state();
    return ret;
  }
}

int vp10_set_internal_size(VP10_COMP *cpi,
                          VPX_SCALING horiz_mode, VPX_SCALING vert_mode) {
  VP10_COMMON *cm = &cpi->common;
  int hr = 0, hs = 0, vr = 0, vs = 0;

  if (horiz_mode > ONETWO || vert_mode > ONETWO)
    return -1;

  Scale2Ratio(horiz_mode, &hr, &hs);
  Scale2Ratio(vert_mode, &vr, &vs);

  // always go to the next whole number
  cm->width = (hs - 1 + cpi->oxcf.width * hr) / hs;
  cm->height = (vs - 1 + cpi->oxcf.height * vr) / vs;
  assert(cm->width <= cpi->initial_width);
  assert(cm->height <= cpi->initial_height);

  update_frame_size(cpi);

  return 0;
}

int vp10_set_size_literal(VP10_COMP *cpi, unsigned int width,
                         unsigned int height) {
  VP10_COMMON *cm = &cpi->common;
#if CONFIG_VP9_HIGHBITDEPTH
  check_initial_width(cpi, cm->use_highbitdepth, 1, 1);
#else
  check_initial_width(cpi, 1, 1);
#endif  // CONFIG_VP9_HIGHBITDEPTH

#if CONFIG_VP9_TEMPORAL_DENOISING
  setup_denoiser_buffer(cpi);
#endif

  if (width) {
    cm->width = width;
    if (cm->width > cpi->initial_width) {
      cm->width = cpi->initial_width;
      printf("Warning: Desired width too large, changed to %d\n", cm->width);
    }
  }

  if (height) {
    cm->height = height;
    if (cm->height > cpi->initial_height) {
      cm->height = cpi->initial_height;
      printf("Warning: Desired height too large, changed to %d\n", cm->height);
    }
  }
  assert(cm->width <= cpi->initial_width);
  assert(cm->height <= cpi->initial_height);

  update_frame_size(cpi);

  return 0;
}

int vp10_get_quantizer(VP10_COMP *cpi) {
  return cpi->common.base_qindex;
}

void vp10_apply_encoding_flags(VP10_COMP *cpi, vpx_enc_frame_flags_t flags) {
  if (flags & (VP8_EFLAG_NO_REF_LAST | VP8_EFLAG_NO_REF_GF |
               VP8_EFLAG_NO_REF_ARF)) {
    int ref = 7;

    if (flags & VP8_EFLAG_NO_REF_LAST)
      ref ^= VP9_LAST_FLAG;

    if (flags & VP8_EFLAG_NO_REF_GF)
      ref ^= VP9_GOLD_FLAG;

    if (flags & VP8_EFLAG_NO_REF_ARF)
      ref ^= VP9_ALT_FLAG;

    vp10_use_as_reference(cpi, ref);
  }

  if (flags & (VP8_EFLAG_NO_UPD_LAST | VP8_EFLAG_NO_UPD_GF |
               VP8_EFLAG_NO_UPD_ARF | VP8_EFLAG_FORCE_GF |
               VP8_EFLAG_FORCE_ARF)) {
    int upd = 7;

    if (flags & VP8_EFLAG_NO_UPD_LAST)
      upd ^= VP9_LAST_FLAG;

    if (flags & VP8_EFLAG_NO_UPD_GF)
      upd ^= VP9_GOLD_FLAG;

    if (flags & VP8_EFLAG_NO_UPD_ARF)
      upd ^= VP9_ALT_FLAG;

    vp10_update_reference(cpi, upd);
  }

  if (flags & VP8_EFLAG_NO_UPD_ENTROPY) {
    vp10_update_entropy(cpi, 0);
  }
}
