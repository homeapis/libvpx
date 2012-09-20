/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef __INC_BLOCK_H
#define __INC_BLOCK_H

#include "vp8/common/onyx.h"
#include "vp8/common/entropymv.h"
#include "vp8/common/entropy.h"
#include "vpx_ports/mem.h"
#include "vp8/common/onyxc_int.h"

// motion search site
typedef struct {
  MV mv;
  int offset;
} search_site;

typedef struct {
  // 16 Y blocks, 4 U blocks, 4 V blocks each with 16 entries
  short *src_diff;
  short *coeff;

  // 16 Y blocks, 4 U blocks, 4 V blocks each with 16 entries
  short *quant;
  short *quant_fast;      // fast quant deprecated for now
  unsigned char *quant_shift;
  short *zbin;
  short *zbin_8x8;
#if CONFIG_TX16X16 || CONFIG_HYBRIDTRANSFORM16X16
  short *zbin_16x16;
#endif
  short *zrun_zbin_boost;
  short *zrun_zbin_boost_8x8;
#if CONFIG_TX16X16 || CONFIG_HYBRIDTRANSFORM16X16
  short *zrun_zbin_boost_16x16;
#endif
  short *round;

  // Zbin Over Quant value
  short zbin_extra;

  unsigned char **base_src;
  unsigned char **base_second_src;
  int src;
  int src_stride;

  int eob_max_offset;
  int eob_max_offset_8x8;
#if CONFIG_TX16X16 || CONFIG_HYBRIDTRANSFORM16X16
  int eob_max_offset_16x16;
#endif
} BLOCK;

typedef struct {
  int count;
  struct {
    B_PREDICTION_MODE mode;
    int_mv mv;
    int_mv second_mv;
  } bmi[16];
} PARTITION_INFO;

// Structure to hold snapshot of coding context during the mode picking process
// TODO Do we need all of these?
typedef struct {
  MODE_INFO mic;
  PARTITION_INFO partition_info;
  int_mv best_ref_mv;
  int_mv second_best_ref_mv;
#if CONFIG_NEWBESTREFMV || CONFIG_NEW_MVREF
  int_mv ref_mvs[MAX_REF_FRAMES][MAX_MV_REFS];
#endif
  int rate;
  int distortion;
  int64_t intra_error;
  int best_mode_index;
  int rddiv;
  int rdmult;
  int hybrid_pred_diff;
  int comp_pred_diff;
  int single_pred_diff;
} PICK_MODE_CONTEXT;

typedef struct {
  DECLARE_ALIGNED(16, short, src_diff[400]);  // 16x16 Y 8x8 U 8x8 V 4x4 2nd Y
  DECLARE_ALIGNED(16, short, coeff[400]);     // 16x16 Y 8x8 U 8x8 V 4x4 2nd Y
  DECLARE_ALIGNED(16, unsigned char, thismb[256]);    // 16x16 Y

  unsigned char *thismb_ptr;
  // 16 Y blocks, 4 U blocks, 4 V blocks,
  // 1 DC 2nd order block each with 16 entries
  BLOCK block[25];

  YV12_BUFFER_CONFIG src;

  MACROBLOCKD e_mbd;
  PARTITION_INFO *partition_info; /* work pointer */
  PARTITION_INFO *pi;   /* Corresponds to upper left visible macroblock */
  PARTITION_INFO *pip;  /* Base of allocated array */

  search_site *ss;
  int ss_count;
  int searches_per_step;

  int errorperbit;
  int sadperbit16;
  int sadperbit4;
  int rddiv;
  int rdmult;
  unsigned int *mb_activity_ptr;
  int *mb_norm_activity_ptr;
  signed int act_zbin_adj;

#if CONFIG_NEWMVENTROPY
  int nmvjointcost[MV_JOINTS];
  int nmvcosts[2][MV_VALS];
  int *nmvcost[2];
  int nmvcosts_hp[2][MV_VALS];
  int *nmvcost_hp[2];

  int nmvjointsadcost[MV_JOINTS];
  int nmvsadcosts[2][MV_VALS];
  int *nmvsadcost[2];
  int nmvsadcosts_hp[2][MV_VALS];
  int *nmvsadcost_hp[2];
#else
  int mvcosts[2][MVvals + 1];
  int *mvcost[2];
  int mvsadcosts[2][MVfpvals + 1];
  int *mvsadcost[2];
  int mvcosts_hp[2][MVvals_hp + 1];
  int *mvcost_hp[2];
  int mvsadcosts_hp[2][MVfpvals_hp + 1];
  int *mvsadcost_hp[2];
#endif  /* CONFIG_NEWMVENTROPY */

  int mbmode_cost[2][MB_MODE_COUNT];
  int intra_uv_mode_cost[2][MB_MODE_COUNT];
  int bmode_costs[VP8_BINTRAMODES][VP8_BINTRAMODES][VP8_BINTRAMODES];
  int i8x8_mode_costs[MB_MODE_COUNT];
  int inter_bmode_costs[B_MODE_COUNT];
#if CONFIG_SWITCHABLE_INTERP
  int switchable_interp_costs[VP8_SWITCHABLE_FILTERS+1]
                             [VP8_SWITCHABLE_FILTERS];
#endif

  // These define limits to motion vector components to prevent them from extending outside the UMV borders
  int mv_col_min;
  int mv_col_max;
  int mv_row_min;
  int mv_row_max;

  int skip;

  int encode_breakout;

  // char * gf_active_ptr;
  signed char *gf_active_ptr;

  unsigned char *active_ptr;

  unsigned int token_costs[TX_SIZE_MAX][BLOCK_TYPES][COEF_BANDS]
    [PREV_COEF_CONTEXTS][MAX_ENTROPY_TOKENS];

  int optimize;
  int q_index;

  // Structure to hold context for each of the 4 MBs within a SB:
  // when encoded as 4 independent MBs:
  PICK_MODE_CONTEXT mb_context[4];
#if CONFIG_SUPERBLOCKS
  // when 4 MBs share coding parameters:
  PICK_MODE_CONTEXT sb_context[4];
#endif

  void (*vp8_short_fdct4x4)(short *input, short *output, int pitch);
  void (*vp8_short_fdct8x4)(short *input, short *output, int pitch);
  void (*short_walsh4x4)(short *input, short *output, int pitch);
  void (*quantize_b)(BLOCK *b, BLOCKD *d);
  void (*quantize_b_pair)(BLOCK *b1, BLOCK *b2, BLOCKD *d0, BLOCKD *d1);
  void (*vp8_short_fdct8x8)(short *input, short *output, int pitch);
#if CONFIG_TX16X16 || CONFIG_HYBRIDTRANSFORM16X16
  void (*vp8_short_fdct16x16)(short *input, short *output, int pitch);
#endif
  void (*short_fhaar2x2)(short *input, short *output, int pitch);
#if CONFIG_TX16X16 || CONFIG_HYBRIDTRANSFORM16X16
  void (*quantize_b_16x16)(BLOCK *b, BLOCKD *d);
#endif
  void (*quantize_b_8x8)(BLOCK *b, BLOCKD *d);
  void (*quantize_b_2x2)(BLOCK *b, BLOCKD *d);

} MACROBLOCK;


#endif
