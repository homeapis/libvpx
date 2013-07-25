
/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <limits.h>

#include "vp9/common/vp9_common.h"
#include "vp9/common/vp9_pred_common.h"
#include "vp9/common/vp9_seg_common.h"
#include "vp9/common/vp9_treecoder.h"

// Returns a context number for the given MB prediction signal
unsigned char vp9_get_pred_context_switchable_interp(const MACROBLOCKD *xd) {
  MODE_INFO_8x8 *mi_8x8 = xd->mi_8x8;
  const MODE_INFO * const above_mi = mi_8x8[-xd->mode_info_stride].mi;
  const MODE_INFO * const left_mi = mi_8x8[-1].mi;
  const int left_in_image = xd->left_available && mi_8x8[-1].mb_in_image;
  const int above_in_image = xd->up_available &&
      mi_8x8[-xd->mode_info_stride].mb_in_image;
  // Note:
  // The mode info data structure has a one element border above and to the
  // left of the entries correpsonding to real macroblocks.
  // The prediction flags in these dummy entries are initialised to 0.
  // left
  int left_mv_pred = 0;
  int left_interp = 0;

  // above
  int above_mv_pred = 0;
  int above_interp = 0;
  INTERPOLATIONFILTERTYPE left_interp_filter = left_mi ?
      left_mi->mbmi.interp_filter : 0;
  INTERPOLATIONFILTERTYPE above_interp_filter = above_mi ?
      above_mi->mbmi.interp_filter : 0;
  MB_PREDICTION_MODE left_mode = left_mi ? left_mi->mbmi.mode : 0;
  MB_PREDICTION_MODE above_mode = above_mi ? above_mi->mbmi.mode : 0;

  left_mv_pred = is_inter_mode(left_mode);
  left_interp =
      left_in_image && left_mv_pred ?
          vp9_switchable_interp_map[left_interp_filter] :
          VP9_SWITCHABLE_FILTERS;

  // above
  above_mv_pred = is_inter_mode(above_mode);
  above_interp =
      above_in_image && above_mv_pred ?
          vp9_switchable_interp_map[above_interp_filter] :
          VP9_SWITCHABLE_FILTERS;

  assert(left_interp != -1);
  assert(above_interp != -1);

  if (left_interp == above_interp)
    return left_interp;
  else if (left_interp == VP9_SWITCHABLE_FILTERS &&
           above_interp != VP9_SWITCHABLE_FILTERS)
    return above_interp;
  else if (left_interp != VP9_SWITCHABLE_FILTERS &&
           above_interp == VP9_SWITCHABLE_FILTERS)
    return left_interp;
  else
    return VP9_SWITCHABLE_FILTERS;
}
// Returns a context number for the given MB prediction signal
unsigned char vp9_get_pred_context_intra_inter(const MACROBLOCKD *xd) {
  int pred_context;
  const MODE_INFO_8x8 *const mi_8x8 = xd->mi_8x8;
  const int mis = -xd->mode_info_stride;
  const MB_MODE_INFO *const above_mbmi = &mi_8x8[mis].mi->mbmi;
  const MB_MODE_INFO *const left_mbmi = &mi_8x8[-1].mi->mbmi;
  const int left_in_image = xd->left_available && mi_8x8[-1].mb_in_image;
  const int above_in_image = xd->up_available && mi_8x8[mis].mb_in_image;
  // Note:
  // The mode info data structure has a one element border above and to the
  // left of the entries correpsonding to real macroblocks.
  // The prediction flags in these dummy entries are initialised to 0.
  if (above_in_image && left_in_image) {  // both edges available
    if (left_mbmi->ref_frame[0] == INTRA_FRAME &&
        above_mbmi->ref_frame[0] == INTRA_FRAME) {  // intra/intra (3)
      pred_context = 3;
    } else {  // intra/inter (1) or inter/inter (0)
      pred_context = left_mbmi->ref_frame[0] == INTRA_FRAME ||
                     above_mbmi->ref_frame[0] == INTRA_FRAME;
    }
  } else if (above_in_image || left_in_image) {  // one edge available
    const MB_MODE_INFO *edge_mbmi = above_in_image ? above_mbmi : left_mbmi;

    // inter: 0, intra: 2
    pred_context = 2 * (edge_mbmi->ref_frame[0] == INTRA_FRAME);
  } else {
    pred_context = 0;
  }
  assert(pred_context >= 0 && pred_context < INTRA_INTER_CONTEXTS);
  return pred_context;
}
// Returns a context number for the given MB prediction signal
unsigned char vp9_get_pred_context_comp_inter_inter(const VP9_COMMON *cm,
                                                    const MACROBLOCKD *xd) {
  int pred_context;
  const MODE_INFO_8x8 *const mi_8x8 = xd->mi_8x8;
  const int mis = -cm->mode_info_stride;
  const MB_MODE_INFO *const above_mbmi = &mi_8x8[mis].mi->mbmi;
  const MB_MODE_INFO *const left_mbmi = &mi_8x8[-1].mi->mbmi;
  const int left_in_image = xd->left_available && mi_8x8[-1].mb_in_image;
  const int above_in_image = xd->up_available && mi_8x8[mis].mb_in_image;
  // Note:
  // The mode info data structure has a one element border above and to the
  // left of the entries correpsonding to real macroblocks.
  // The prediction flags in these dummy entries are initialised to 0.
  if (above_in_image && left_in_image) {  // both edges available
    if (above_mbmi->ref_frame[1] <= INTRA_FRAME &&
        left_mbmi->ref_frame[1] <= INTRA_FRAME)
      // neither edge uses comp pred (0/1)
      pred_context = (above_mbmi->ref_frame[0] == cm->comp_fixed_ref) ^
                     (left_mbmi->ref_frame[0] == cm->comp_fixed_ref);
    else if (above_mbmi->ref_frame[1] <= INTRA_FRAME)
      // one of two edges uses comp pred (2/3)
      pred_context = 2 + (above_mbmi->ref_frame[0] == cm->comp_fixed_ref ||
                          above_mbmi->ref_frame[0] == INTRA_FRAME);
    else if (left_mbmi->ref_frame[1] <= INTRA_FRAME)
      // one of two edges uses comp pred (2/3)
      pred_context = 2 + (left_mbmi->ref_frame[0] == cm->comp_fixed_ref ||
                          left_mbmi->ref_frame[0] == INTRA_FRAME);
    else  // both edges use comp pred (4)
      pred_context = 4;
  } else if (above_in_image || left_in_image) {  // one edge available
    const MB_MODE_INFO *edge_mbmi = above_in_image ? above_mbmi : left_mbmi;

    if (edge_mbmi->ref_frame[1] <= INTRA_FRAME)
      // edge does not use comp pred (0/1)
      pred_context = edge_mbmi->ref_frame[0] == cm->comp_fixed_ref;
    else
      // edge uses comp pred (3)
      pred_context = 3;
  } else {  // no edges available (1)
    pred_context = 1;
  }
  assert(pred_context >= 0 && pred_context < COMP_INTER_CONTEXTS);
  return pred_context;
}

// Returns a context number for the given MB prediction signal
unsigned char vp9_get_pred_context_comp_ref_p(const VP9_COMMON *cm,
                                              const MACROBLOCKD *xd) {
  int pred_context;
  const MODE_INFO_8x8 *const mi_8x8 = xd->mi_8x8;
  const int mis = -cm->mode_info_stride;
  const MB_MODE_INFO *const above_mbmi = &mi_8x8[mis].mi->mbmi;
  const MB_MODE_INFO *const left_mbmi = &mi_8x8[-1].mi->mbmi;
  const int left_in_image = xd->left_available && mi_8x8[-1].mb_in_image;
  const int above_in_image = xd->up_available && mi_8x8[mis].mb_in_image;
  // Note:
  // The mode info data structure has a one element border above and to the
  // left of the entries correpsonding to real macroblocks.
  // The prediction flags in these dummy entries are initialised to 0.
  const int fix_ref_idx = cm->ref_frame_sign_bias[cm->comp_fixed_ref];
  const int var_ref_idx = !fix_ref_idx;

  if (above_in_image && left_in_image) {  // both edges available
    if (above_mbmi->ref_frame[0] == INTRA_FRAME &&
        left_mbmi->ref_frame[0] == INTRA_FRAME) {  // intra/intra (2)
      pred_context = 2;
    } else if (above_mbmi->ref_frame[0] == INTRA_FRAME ||
               left_mbmi->ref_frame[0] == INTRA_FRAME) {  // intra/inter
      const MB_MODE_INFO *edge_mbmi = above_mbmi->ref_frame[0] == INTRA_FRAME ?
                                          left_mbmi : above_mbmi;

      if (edge_mbmi->ref_frame[1] <= INTRA_FRAME)  // single pred (1/3)
        pred_context = 1 + 2 * (edge_mbmi->ref_frame[0] != cm->comp_var_ref[1]);
      else  // comp pred (1/3)
        pred_context = 1 + 2 * (edge_mbmi->ref_frame[var_ref_idx]
                                    != cm->comp_var_ref[1]);
    } else {  // inter/inter
      int l_sg = left_mbmi->ref_frame[1] <= INTRA_FRAME;
      int a_sg = above_mbmi->ref_frame[1] <= INTRA_FRAME;
      MV_REFERENCE_FRAME vrfa = a_sg ? above_mbmi->ref_frame[0]
                                     : above_mbmi->ref_frame[var_ref_idx];
      MV_REFERENCE_FRAME vrfl = l_sg ? left_mbmi->ref_frame[0]
                                     : left_mbmi->ref_frame[var_ref_idx];

      if (vrfa == vrfl && cm->comp_var_ref[1] == vrfa) {
        pred_context = 0;
      } else if (l_sg && a_sg) {  // single/single
        if ((vrfa == cm->comp_fixed_ref && vrfl == cm->comp_var_ref[0]) ||
            (vrfl == cm->comp_fixed_ref && vrfa == cm->comp_var_ref[0]))
          pred_context = 4;
        else if (vrfa == vrfl)
          pred_context = 3;
        else
          pred_context = 1;
      } else if (l_sg || a_sg) {  // single/comp
        MV_REFERENCE_FRAME vrfc = l_sg ? vrfa : vrfl;
        MV_REFERENCE_FRAME rfs = a_sg ? vrfa : vrfl;
        if (vrfc == cm->comp_var_ref[1] && rfs != cm->comp_var_ref[1])
          pred_context = 1;
        else if (rfs == cm->comp_var_ref[1] && vrfc != cm->comp_var_ref[1])
          pred_context = 2;
        else
          pred_context = 4;
      } else if (vrfa == vrfl) {  // comp/comp
        pred_context = 4;
      } else {
        pred_context = 2;
      }
    }
  } else if (above_in_image || left_in_image) {  // one edge available
    const MB_MODE_INFO *edge_mbmi = above_in_image ? above_mbmi : left_mbmi;

    if (edge_mbmi->ref_frame[0] == INTRA_FRAME)
      pred_context = 2;
    else if (edge_mbmi->ref_frame[1] > INTRA_FRAME)
      pred_context = 4 * (edge_mbmi->ref_frame[var_ref_idx]
                              != cm->comp_var_ref[1]);
    else
      pred_context = 3 * (edge_mbmi->ref_frame[0] != cm->comp_var_ref[1]);
  } else {  // no edges available (2)
    pred_context = 2;
  }
  assert(pred_context >= 0 && pred_context < REF_CONTEXTS);

  return pred_context;
}
unsigned char vp9_get_pred_context_single_ref_p1(const MACROBLOCKD *xd) {
  int pred_context;
  const MODE_INFO_8x8 *const mi_8x8 = xd->mi_8x8;
  const int mis = -xd->mode_info_stride;
  const MB_MODE_INFO *const above_mbmi = &mi_8x8[mis].mi->mbmi;
  const MB_MODE_INFO *const left_mbmi = &mi_8x8[-1].mi->mbmi;
  const int left_in_image = xd->left_available && mi_8x8[-1].mb_in_image;
  const int above_in_image = xd->up_available && mi_8x8[mis].mb_in_image;
  // Note:
  // The mode info data structure has a one element border above and to the
  // left of the entries correpsonding to real macroblocks.
  // The prediction flags in these dummy entries are initialised to 0.
  if (above_in_image && left_in_image) {  // both edges available
    if (above_mbmi->ref_frame[0] == INTRA_FRAME &&
        left_mbmi->ref_frame[0] == INTRA_FRAME) {
      pred_context = 2;
    } else if (above_mbmi->ref_frame[0] == INTRA_FRAME ||
               left_mbmi->ref_frame[0] == INTRA_FRAME) {
      const MB_MODE_INFO *edge_mbmi = above_mbmi->ref_frame[0] == INTRA_FRAME ?
                                          left_mbmi : above_mbmi;

      if (edge_mbmi->ref_frame[1] <= INTRA_FRAME)
        pred_context = 4 * (edge_mbmi->ref_frame[0] == LAST_FRAME);
      else
        pred_context = 1 + (edge_mbmi->ref_frame[0] == LAST_FRAME ||
                            edge_mbmi->ref_frame[1] == LAST_FRAME);
    } else if (above_mbmi->ref_frame[1] <= INTRA_FRAME &&
               left_mbmi->ref_frame[1] <= INTRA_FRAME) {
      pred_context = 2 * (above_mbmi->ref_frame[0] == LAST_FRAME) +
                     2 * (left_mbmi->ref_frame[0] == LAST_FRAME);
    } else if (above_mbmi->ref_frame[1] > INTRA_FRAME &&
               left_mbmi->ref_frame[1] > INTRA_FRAME) {
      pred_context = 1 + (above_mbmi->ref_frame[0] == LAST_FRAME ||
                          above_mbmi->ref_frame[1] == LAST_FRAME ||
                          left_mbmi->ref_frame[0] == LAST_FRAME ||
                          left_mbmi->ref_frame[1] == LAST_FRAME);
    } else {
      MV_REFERENCE_FRAME rfs = above_mbmi->ref_frame[1] <= INTRA_FRAME ?
              above_mbmi->ref_frame[0] : left_mbmi->ref_frame[0];
      MV_REFERENCE_FRAME crf1 = above_mbmi->ref_frame[1] > INTRA_FRAME ?
              above_mbmi->ref_frame[0] : left_mbmi->ref_frame[0];
      MV_REFERENCE_FRAME crf2 = above_mbmi->ref_frame[1] > INTRA_FRAME ?
              above_mbmi->ref_frame[1] : left_mbmi->ref_frame[1];

      if (rfs == LAST_FRAME)
        pred_context = 3 + (crf1 == LAST_FRAME || crf2 == LAST_FRAME);
      else
        pred_context = crf1 == LAST_FRAME || crf2 == LAST_FRAME;
    }
  } else if (above_in_image || left_in_image) {  // one edge available
    const MB_MODE_INFO *edge_mbmi = above_in_image ? above_mbmi : left_mbmi;

    if (edge_mbmi->ref_frame[0] == INTRA_FRAME)
      pred_context = 2;
    else if (edge_mbmi->ref_frame[1] <= INTRA_FRAME)
      pred_context = 4 * (edge_mbmi->ref_frame[0] == LAST_FRAME);
    else
      pred_context = 1 + (edge_mbmi->ref_frame[0] == LAST_FRAME ||
                          edge_mbmi->ref_frame[1] == LAST_FRAME);
  } else {  // no edges available (2)
    pred_context = 2;
  }
  assert(pred_context >= 0 && pred_context < REF_CONTEXTS);
  return pred_context;
}

unsigned char vp9_get_pred_context_single_ref_p2(const MACROBLOCKD *xd) {
  int pred_context;
  const MODE_INFO_8x8 *const mi_8x8 = xd->mi_8x8;
  const int mis = -xd->mode_info_stride;
  const MB_MODE_INFO *const above_mbmi = &mi_8x8[mis].mi->mbmi;
  const MB_MODE_INFO *const left_mbmi = &mi_8x8[-1].mi->mbmi;
  const int left_in_image = xd->left_available && mi_8x8[-1].mb_in_image;
  const int above_in_image = xd->up_available && mi_8x8[mis].mb_in_image;

  // Note:
  // The mode info data structure has a one element border above and to the
  // left of the entries correpsonding to real macroblocks.
  // The prediction flags in these dummy entries are initialised to 0.
  if (above_in_image && left_in_image) {  // both edges available
    if (above_mbmi->ref_frame[0] == INTRA_FRAME &&
        left_mbmi->ref_frame[0] == INTRA_FRAME) {
      pred_context = 2;
    } else if (above_mbmi->ref_frame[0] == INTRA_FRAME ||
               left_mbmi->ref_frame[0] == INTRA_FRAME) {
      const MB_MODE_INFO *edge_mbmi = above_mbmi->ref_frame[0] == INTRA_FRAME ?
                                          left_mbmi : above_mbmi;

      if (edge_mbmi->ref_frame[1] <= INTRA_FRAME) {
        if (edge_mbmi->ref_frame[0] == LAST_FRAME)
          pred_context = 3;
        else
          pred_context = 4 * (edge_mbmi->ref_frame[0] == GOLDEN_FRAME);
      } else {
        pred_context = 1 + 2 * (edge_mbmi->ref_frame[0] == GOLDEN_FRAME ||
                                edge_mbmi->ref_frame[1] == GOLDEN_FRAME);
      }
    } else if (above_mbmi->ref_frame[1] <= INTRA_FRAME &&
               left_mbmi->ref_frame[1] <= INTRA_FRAME) {
      if (above_mbmi->ref_frame[0] == LAST_FRAME &&
          left_mbmi->ref_frame[0] == LAST_FRAME) {
        pred_context = 3;
      } else if (above_mbmi->ref_frame[0] == LAST_FRAME ||
                 left_mbmi->ref_frame[0] == LAST_FRAME) {
        const MB_MODE_INFO *edge_mbmi = above_mbmi->ref_frame[0] == LAST_FRAME ?
                                           left_mbmi : above_mbmi;

        pred_context = 4 * (edge_mbmi->ref_frame[0] == GOLDEN_FRAME);
      } else {
        pred_context = 2 * (above_mbmi->ref_frame[0] == GOLDEN_FRAME) +
                       2 * (left_mbmi->ref_frame[0] == GOLDEN_FRAME);
      }
    } else if (above_mbmi->ref_frame[1] > INTRA_FRAME &&
               left_mbmi->ref_frame[1] > INTRA_FRAME) {
      if (above_mbmi->ref_frame[0] == left_mbmi->ref_frame[0] &&
          above_mbmi->ref_frame[1] == left_mbmi->ref_frame[1])
        pred_context = 3 * (above_mbmi->ref_frame[0] == GOLDEN_FRAME ||
                            above_mbmi->ref_frame[1] == GOLDEN_FRAME ||
                            left_mbmi->ref_frame[0] == GOLDEN_FRAME ||
                            left_mbmi->ref_frame[1] == GOLDEN_FRAME);
      else
        pred_context = 2;
    } else {
      MV_REFERENCE_FRAME rfs = above_mbmi->ref_frame[1] <= INTRA_FRAME ?
              above_mbmi->ref_frame[0] : left_mbmi->ref_frame[0];
      MV_REFERENCE_FRAME crf1 = above_mbmi->ref_frame[1] > INTRA_FRAME ?
              above_mbmi->ref_frame[0] : left_mbmi->ref_frame[0];
      MV_REFERENCE_FRAME crf2 = above_mbmi->ref_frame[1] > INTRA_FRAME ?
              above_mbmi->ref_frame[1] : left_mbmi->ref_frame[1];

      if (rfs == GOLDEN_FRAME)
        pred_context = 3 + (crf1 == GOLDEN_FRAME || crf2 == GOLDEN_FRAME);
      else if (rfs == ALTREF_FRAME)
        pred_context = crf1 == GOLDEN_FRAME || crf2 == GOLDEN_FRAME;
      else
        pred_context = 1 + 2 * (crf1 == GOLDEN_FRAME || crf2 == GOLDEN_FRAME);
    }
  } else if (above_in_image || left_in_image) {  // one edge available
    const MB_MODE_INFO *edge_mbmi = above_in_image ? above_mbmi : left_mbmi;

    if (edge_mbmi->ref_frame[0] == INTRA_FRAME ||
        (edge_mbmi->ref_frame[0] == LAST_FRAME &&
         edge_mbmi->ref_frame[1] <= INTRA_FRAME))
      pred_context = 2;
    else if (edge_mbmi->ref_frame[1] <= INTRA_FRAME)
      pred_context = 4 * (edge_mbmi->ref_frame[0] == GOLDEN_FRAME);
    else
      pred_context = 3 * (edge_mbmi->ref_frame[0] == GOLDEN_FRAME ||
                          edge_mbmi->ref_frame[1] == GOLDEN_FRAME);
  } else {  // no edges available (2)
    pred_context = 2;
  }
  assert(pred_context >= 0 && pred_context < REF_CONTEXTS);
  return pred_context;
}
// Returns a context number for the given MB prediction signal
unsigned char vp9_get_pred_context_tx_size(const MACROBLOCKD *xd) {
  const MODE_INFO_8x8 *const mi_8x8 = xd->mi_8x8;
  const int mis = -xd->mode_info_stride;
  const MODE_INFO *const above_mi = mi_8x8[mis].mi;
  const MODE_INFO *const left_mi = mi_8x8[-1].mi;
  const int left_in_image = xd->left_available && mi_8x8[-1].mb_in_image;
  const int above_in_image = xd->up_available && mi_8x8[mis].mb_in_image;
  const MODE_INFO *const mi = mi_8x8[0].mi;
  // Note:
  // The mode info data structure has a one element border above and to the
  // left of the entries correpsonding to real macroblocks.
  // The prediction flags in these dummy entries are initialised to 0.
  int above_context, left_context;
  int max_tx_size;
  if (mi->mbmi.sb_type < BLOCK_SIZE_SB8X8)
    max_tx_size = TX_4X4;
  else if (mi->mbmi.sb_type < BLOCK_SIZE_MB16X16)
    max_tx_size = TX_8X8;
  else if (mi->mbmi.sb_type < BLOCK_SIZE_SB32X32)
    max_tx_size = TX_16X16;
  else
    max_tx_size = TX_32X32;

  above_context = left_context = max_tx_size;

  if (above_in_image)
    above_context = above_mi->mbmi.mb_skip_coeff ? max_tx_size
                                                 : above_mi->mbmi.txfm_size;

  if (left_in_image)
    left_context = left_mi->mbmi.mb_skip_coeff ? max_tx_size
                                               : left_mi->mbmi.txfm_size;

  if (!left_in_image)
    left_context = above_context;

  if (!above_in_image)
    above_context = left_context;

  return above_context + left_context > max_tx_size;
}

void vp9_set_pred_flag_seg_id(MACROBLOCKD *xd, BLOCK_SIZE_TYPE bsize,
                              uint8_t pred_flag) {
  xd->mi_8x8[0].mi->mbmi.seg_id_predicted = pred_flag;
}

void vp9_set_pred_flag_seg_id_e(VP9_COMMON *cm, BLOCK_SIZE_TYPE bsize,
                                int mi_row, int mi_col, uint8_t pred_flag) {
  MODE_INFO_8x8 *mi_8x8 = &cm->mi_grid_visible[mi_row * cm->mode_info_stride + mi_col];
  const int bw = 1 << mi_width_log2(bsize);
  const int bh = 1 << mi_height_log2(bsize);
  const int xmis = MIN(cm->mi_cols - mi_col, bw);
  const int ymis = MIN(cm->mi_rows - mi_row, bh);
  int x, y;

  for (y = 0; y < ymis; y++)
    for (x = 0; x < xmis; x++)
      mi_8x8[y * cm->mode_info_stride + x].mi->mbmi.seg_id_predicted = pred_flag;
}

void vp9_set_pred_flag_mbskip(MACROBLOCKD *xd, BLOCK_SIZE_TYPE bsize,
                              uint8_t pred_flag) {
  xd->mi_8x8[0].mi->mbmi.mb_skip_coeff = pred_flag;
}

void vp9_set_pred_flag_mbskip_e(VP9_COMMON *cm, BLOCK_SIZE_TYPE bsize,
                                int mi_row, int mi_col, uint8_t pred_flag) {
  MODE_INFO_8x8 *mi_8x8 = &cm->mi_grid_visible[mi_row * cm->mode_info_stride + mi_col];
  const int bw = 1 << mi_width_log2(bsize);
  const int bh = 1 << mi_height_log2(bsize);
  const int xmis = MIN(cm->mi_cols - mi_col, bw);
  const int ymis = MIN(cm->mi_rows - mi_row, bh);
  int x, y;

  for (y = 0; y < ymis; y++)
    for (x = 0; x < xmis; x++)
      mi_8x8[y * cm->mode_info_stride + x].mi->mbmi.mb_skip_coeff = pred_flag;
}

int vp9_get_segment_id(VP9_COMMON *cm, const uint8_t *segment_ids,
                       BLOCK_SIZE_TYPE bsize, int mi_row, int mi_col) {
  const int mi_offset = mi_row * cm->mi_cols + mi_col;
  const int bw = 1 << mi_width_log2(bsize);
  const int bh = 1 << mi_height_log2(bsize);
  const int xmis = MIN(cm->mi_cols - mi_col, bw);
  const int ymis = MIN(cm->mi_rows - mi_row, bh);
  int x, y, segment_id = INT_MAX;

  for (y = 0; y < ymis; y++)
    for (x = 0; x < xmis; x++)
      segment_id = MIN(segment_id,
                       segment_ids[mi_offset + y * cm->mi_cols + x]);

  assert(segment_id >= 0 && segment_id < MAX_MB_SEGMENTS);
  return segment_id;
}
