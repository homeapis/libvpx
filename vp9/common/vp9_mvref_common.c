/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "vp9/common/vp9_mvref_common.h"

#define MVREF_NEIGHBOURS 8
static const int mv_ref_blocks[BLOCK_SIZE_TYPES][MVREF_NEIGHBOURS][2] = {
  // SB4X4
  {{0, -1}, {-1, 0}, {-1, -1}, {0, -2}, {-2, 0}, {-1, -2}, {-2, -1}, {-2, -2}},
  // SB4X8
  {{0, -1}, {-1, 0}, {-1, -1}, {0, -2}, {-2, 0}, {-1, -2}, {-2, -1}, {-2, -2}},
  // SB8X4
  {{0, -1}, {-1, 0}, {-1, -1}, {0, -2}, {-2, 0}, {-1, -2}, {-2, -1}, {-2, -2}},
  // SB8X8
  {{0, -1}, {-1, 0}, {-1, -1}, {0, -2}, {-2, 0}, {-1, -2}, {-2, -1}, {-2, -2}},
  // SB8X16
  {{-1, 0}, {0, -1}, {-1, 1}, {-1, -1}, {-2, 0}, {0, -2}, {-1, -2}, {-2, -1}},
  // SB16X8
  {{0, -1}, {-1, 0}, {1, -1}, {-1, -1}, {0, -2}, {-2, 0}, {-2, -1}, {-1, -2}},
  // SB16X16
  {{0, -1}, {-1, 0}, {1, -1}, {-1, 1}, {-1, -1}, {0, -3}, {-3, 0}, {-3, -3}},
  // SB16X32
  {{-1, 0}, {0, -1}, {-1, 2}, {-1, -1}, {1, -1}, {-3, 0}, {0, -3}, {-3, -3}},
  // SB32X16
  {{0, -1}, {-1, 0}, {2, -1}, {-1, -1}, {-1, 1}, {0, -3}, {-3, 0}, {-3, -3}},
  // SB32X32
  {{1, -1}, {-1, 1}, {2, -1}, {-1, 2}, {-1, -1}, {0, -3}, {-3, 0}, {-3, -3}},
  // SB32X64
  {{-1, 0}, {0, -1}, {-1, 4}, {2, -1}, {-1, -1}, {-3, 0}, {0, -3}, {-1, 2}},
  // SB64X32
  {{0, -1}, {-1, 0}, {4, -1}, {-1, 2}, {-1, -1}, {0, -3}, {-3, 0}, {2, -1}},
  // SB64X64
  {{3, -1}, {-1, 3}, {4, -1}, {-1, 4}, {-1, -1}, {0, -1}, {-1, 0}, {6, -1}}
};
// clamp_mv_ref
#define MV_BORDER (16 << 3) // Allow 16 pels in 1/8th pel units

static void clamp_mv_ref(const MACROBLOCKD *xd, int_mv *mv) {
  mv->as_mv.col = clamp(mv->as_mv.col, xd->mb_to_left_edge - MV_BORDER,
                                       xd->mb_to_right_edge + MV_BORDER);
  mv->as_mv.row = clamp(mv->as_mv.row, xd->mb_to_top_edge - MV_BORDER,
                                       xd->mb_to_bottom_edge + MV_BORDER);
}

// Gets a candidate reference motion vector from the given mode info
// structure if one exists that matches the given reference frame.
static int get_matching_candidate(const MODE_INFO *candidate_mi,
                                  MV_REFERENCE_FRAME ref_frame,
                                  int_mv *c_mv, int block_idx) {
  if (ref_frame == candidate_mi->mbmi.ref_frame[0]) {
    if (block_idx >= 0 && candidate_mi->mbmi.sb_type < BLOCK_SIZE_SB8X8)
      c_mv->as_int = candidate_mi->bmi[block_idx].as_mv[0].as_int;
    else
      c_mv->as_int = candidate_mi->mbmi.mv[0].as_int;
  } else if (ref_frame == candidate_mi->mbmi.ref_frame[1]) {
    if (block_idx >= 0 && candidate_mi->mbmi.sb_type < BLOCK_SIZE_SB8X8)
      c_mv->as_int = candidate_mi->bmi[block_idx].as_mv[1].as_int;
    else
      c_mv->as_int = candidate_mi->mbmi.mv[1].as_int;
  } else {
    return 0;
  }

  return 1;
}
// Gets a candidate reference motion vector from the given mode info
// structure if one exists that matches the given reference frame.
static int get_matching_candidateb(const MODE_INFO *candidate_mi,
                                  MV_REFERENCE_FRAME ref_frame,
                                  int_mv *c_mv, int block_idx) {
  if (ref_frame == candidate_mi->mbmi.ref_frame[0]) {
      c_mv->as_int = candidate_mi->bmi[block_idx].as_mv[0].as_int;
  } else if (ref_frame == candidate_mi->mbmi.ref_frame[1]) {
      c_mv->as_int = candidate_mi->bmi[block_idx].as_mv[1].as_int;
  } else {
    return 0;
  }

  return 1;
}

// Gets candidate reference motion vector(s) from the given mode info
// structure if they exists and do NOT match the given reference frame.
static void get_non_matching_candidates(const MODE_INFO *candidate_mi,
                                        MV_REFERENCE_FRAME ref_frame,
                                        MV_REFERENCE_FRAME *c_ref_frame,
                                        int_mv *c_mv,
                                        MV_REFERENCE_FRAME *c2_ref_frame,
                                        int_mv *c2_mv) {

  c_mv->as_int = 0;
  c2_mv->as_int = 0;
  *c_ref_frame = INTRA_FRAME;
  *c2_ref_frame = INTRA_FRAME;

  // If first candidate not valid neither will be.
  if (candidate_mi->mbmi.ref_frame[0] > INTRA_FRAME) {
    // First candidate
    if (candidate_mi->mbmi.ref_frame[0] != ref_frame) {
      *c_ref_frame = candidate_mi->mbmi.ref_frame[0];
      c_mv->as_int = candidate_mi->mbmi.mv[0].as_int;
    }

    // Second candidate
    if ((candidate_mi->mbmi.ref_frame[1] > INTRA_FRAME) &&
        (candidate_mi->mbmi.ref_frame[1] != ref_frame) &&
        (candidate_mi->mbmi.mv[1].as_int != candidate_mi->mbmi.mv[0].as_int)) {
      *c2_ref_frame = candidate_mi->mbmi.ref_frame[1];
      c2_mv->as_int = candidate_mi->mbmi.mv[1].as_int;
    }
  }
}


// Performs mv sign inversion if indicated by the reference frame combination.
static void scale_mv(MACROBLOCKD *xd, MV_REFERENCE_FRAME this_ref_frame,
                     MV_REFERENCE_FRAME candidate_ref_frame,
                     int_mv *candidate_mv, int *ref_sign_bias) {

  // Sign inversion where appropriate.
  if (ref_sign_bias[candidate_ref_frame] != ref_sign_bias[this_ref_frame]) {
    candidate_mv->as_mv.row = -candidate_mv->as_mv.row;
    candidate_mv->as_mv.col = -candidate_mv->as_mv.col;
  }
}

// Add a candidate mv.
// Discard if it has already been seen.
static void add_candidate_mv(int_mv *mv_list,  int *mv_scores,
                             int *candidate_count, int_mv candidate_mv,
                             int weight) {
  if (*candidate_count == 0) {
    mv_list[0].as_int = candidate_mv.as_int;
    mv_scores[0] = weight;
    *candidate_count += 1;
  } else if ((*candidate_count == 1) &&
             (candidate_mv.as_int != mv_list[0].as_int)) {
    mv_list[1].as_int = candidate_mv.as_int;
    mv_scores[1] = weight;
    *candidate_count += 1;
  }
}

// This function searches the neighbourhood of a given MB/SB
// to try and find candidate reference vectors.
//
#if 1
typedef enum {
  MV_REF_INTRA,
  MV_REF_ZERO,
  MV_REF_NEW,
  MV_REF_NONE,
  MV_REF_NUM
} MV_REF;

static const int mode_2_mv_ref_count[MB_MODE_COUNT] = {
  MV_REF_INTRA,  // DC_PRED
  MV_REF_INTRA,  // V_PRED,
  MV_REF_INTRA,  // H_PRED,
  MV_REF_INTRA,  // D45_PRED,
  MV_REF_INTRA,  // D135_PRED,
  MV_REF_INTRA,  // D117_PRED,
  MV_REF_INTRA,  // D153_PRED,
  MV_REF_INTRA,  // D27_PRED,
  MV_REF_INTRA,  // D63_PRED,
  MV_REF_INTRA,  // TM_PRED,
  MV_REF_NONE,    // NEARESTMV,
  MV_REF_NONE,   // NEARMV,
  MV_REF_ZERO,  // ZEROMV,
  MV_REF_NEW,   // NEWMV,
};
static int check_and_add_mv(const MODE_INFO *candidate_mi,
                            MV_REFERENCE_FRAME ref_frame, int weight,
                            int_mv *candidate_mv,
                            int *scores,
                            int *refmv_count) {
  int same_ref_0 = (ref_frame == candidate_mi->mbmi.ref_frame[0]);
  int same_ref_1 = (ref_frame == candidate_mi->mbmi.ref_frame[1]);
  int found = 0;
  int proposed_mv = (
      same_ref_0 ? candidate_mi->mbmi.mv[0].as_int :
          (same_ref_1 ? candidate_mi->mbmi.mv[1].as_int : 0));

  found = (same_ref_0 || same_ref_1)
      && (!*refmv_count || candidate_mv[0].as_int != proposed_mv);

  if (found) {
    scores[*refmv_count] = weight;
    candidate_mv[*refmv_count].as_int = proposed_mv;
    ++(*refmv_count);
  }
  return found;
}
static int check_and_add_block_mv(const MODE_INFO *candidate_mi,
                            MV_REFERENCE_FRAME ref_frame, int weight,
                            int_mv *candidate_mv,
                            int *scores,
                            int *refmv_count, int block_idx) {
  int same_ref_0 = (ref_frame == candidate_mi->mbmi.ref_frame[0]);
  int same_ref_1 = (ref_frame == candidate_mi->mbmi.ref_frame[1]);
  int found = 0;
  int proposed_mv;

  if (candidate_mi->mbmi.sb_type < BLOCK_SIZE_SB8X8)
    proposed_mv = (
        same_ref_0 ?
            candidate_mi->bmi[block_idx].as_mv[0].as_int :
            (same_ref_1 ?
                candidate_mi->bmi[block_idx].as_mv[1].as_int :
                candidate_mv[0].as_int));
  else
    proposed_mv = (
        same_ref_0 ?
            candidate_mi->mbmi.mv[0].as_int :
            (same_ref_1 ? candidate_mi->mbmi.mv[1].as_int : 0));

  found = (same_ref_0 || same_ref_1)
      && (!*refmv_count || candidate_mv[0].as_int != proposed_mv);

  if (found) {
    scores[*refmv_count] = weight;
    candidate_mv[*refmv_count].as_int = proposed_mv;
    ++(*refmv_count);
  }

  return found;
}
static INLINE int block_idx_to_mv_ref(int x_offset, int block_idx) {
  return (x_offset ? 1 + (block_idx & 0xfffffffe) : 2 + (block_idx & 1));
}

#if 1
void vp9_find_mv_refs_idx(VP9_COMMON *cm, MACROBLOCKD *xd, MODE_INFO *here,
                          MODE_INFO *lf_here, MV_REFERENCE_FRAME ref_frame,
                          int_mv *mv_ref_list, int *ref_sign_bias,
                          int block_idx) {
  int i;
  const int (*mv_ref_search)[2];
  const int mi_col = get_mi_col(xd);
  const int mi_row = get_mi_row(xd);
  int mv_counts[MV_REF_NUM] = {0, 0, 0, 0};
  int_mv mvs_from_diff_ref[18];
  int same_counter = 0;
  int diff_counter = 0;
  MB_MODE_INFO * mbmi = &xd->mode_info_context->mbmi;
  int b;

  mv_ref_search = mv_ref_blocks[mbmi->sb_type];

  // Blank the reference vector lists and other local structures.
  vpx_memset(mv_ref_list, 0, sizeof(int_mv) * MAX_MV_REF_CANDIDATES);

  mv_ref_search = mv_ref_blocks[mbmi->sb_type];

  for (i = 0; (i < MVREF_NEIGHBOURS + 1) &&
              (same_counter < MAX_MV_REF_CANDIDATES); ++i) {

    int mi_search_col, mi_search_row;
    int proposed_ref_0, proposed_ref_1;
    int_mv proposed_mv_0, proposed_mv_1;
    MODE_INFO *candidate;

    // if i < MVREF_NEIGHBORS candidate is in same reference frame.
    if (i < MVREF_NEIGHBOURS) {
      mi_search_col = mi_col + mv_ref_search[i][0];
      mi_search_row = mi_row + mv_ref_search[i][1];

      if ((mi_search_col < cm->cur_tile_mi_col_start) ||
          (mi_search_col > cm->cur_tile_mi_col_end) ||
          (mi_search_row < 0) || (mi_search_row > cm->mi_rows))
        continue;

      candidate = here + mv_ref_search[i][0]
          + (mv_ref_search[i][1] * xd->mode_info_stride);
    } else {
      // i == MVREF_NEIGHBORS so this is candidate from the last frame
      candidate = lf_here;
      if (!lf_here)
        continue;
    }
    proposed_ref_0 = candidate->mbmi.ref_frame[0];
    proposed_ref_1 = candidate->mbmi.ref_frame[1];
    proposed_mv_0.as_int = candidate->mbmi.mv[0].as_int;
    proposed_mv_1.as_int = candidate->mbmi.mv[1].as_int;
    mv_counts[mode_2_mv_ref_count[candidate->mbmi.mode]] += (i < 2);

    if (proposed_ref_0 == INTRA_FRAME)
      continue;

    if (proposed_ref_0 == ref_frame) {

      int which_mv = proposed_mv_0.as_int;

      // if we are less than 8x8 and close to the current block
      if (candidate->mbmi.sb_type < BLOCK_SIZE_SB8X8 && i < 2
          && block_idx >= 0) {
        b = block_idx_to_mv_ref(mv_ref_search[i][0], block_idx);
        which_mv = candidate->bmi[b].as_mv[0].as_int;
      }

      if (!same_counter || which_mv != mv_ref_list[0].as_int)
        mv_ref_list[same_counter++].as_int = which_mv;

    } else if (diff_counter < MAX_MV_REF_CANDIDATES) {
      int_mv pmv0 = proposed_mv_0;
      scale_mv(xd, ref_frame, proposed_ref_0, &pmv0, ref_sign_bias);
      if (!diff_counter
          || pmv0.as_int != mvs_from_diff_ref[0].as_int)
        mvs_from_diff_ref[diff_counter++].as_int = pmv0.as_int;
    }

    if (proposed_ref_1 == ref_frame) {
      // if we are less than 8x8 and close to the current block
      if (candidate->mbmi.sb_type < BLOCK_SIZE_SB8X8
          && i < 2
          && block_idx >= 0) {
        b = block_idx_to_mv_ref(mv_ref_search[i][0], block_idx);
        proposed_mv_1.as_int = candidate->bmi[b].as_mv[1].as_int;
      }
      if (!same_counter || proposed_mv_1.as_int != mv_ref_list[0].as_int)
        mv_ref_list[same_counter++].as_int = proposed_mv_1.as_int;
    } else if (proposed_ref_1 > INTRA_FRAME &&
               diff_counter < MAX_MV_REF_CANDIDATES) {
      int_mv pmv1 = proposed_mv_1;
      scale_mv(xd, ref_frame, proposed_ref_1, &pmv1, ref_sign_bias);
      if (proposed_mv_1.as_int != proposed_mv_0.as_int
          && (!diff_counter
              || pmv1.as_int != mvs_from_diff_ref[0].as_int))
        mvs_from_diff_ref[diff_counter++].as_int = pmv1.as_int;
    }
  }
  for (i = 0; i < diff_counter && same_counter < MAX_MV_REF_CANDIDATES; ++i) {
    if (!same_counter || mvs_from_diff_ref[i].as_int != mv_ref_list[0].as_int)
      mv_ref_list[same_counter++].as_int = mvs_from_diff_ref[i].as_int;
  }

  if (!mv_counts[MV_REF_INTRA]) {
    if (!mv_counts[MV_REF_NEW]) {
      // 0 = both zero mv
      // 1 = one zero mv + one a predicted mv
      // 2 = two predicted mvs
      mbmi->mb_mode_context[ref_frame] = 2 - mv_counts[MV_REF_ZERO];
    } else {
      // 3 = one predicted/zero and one new mv
      // 4 = two new mvs
      mbmi->mb_mode_context[ref_frame] = 2 + mv_counts[MV_REF_NEW];
    }
  } else {
    // 5 = one intra neighbour + x
    // 6 = two intra neighbours
    mbmi->mb_mode_context[ref_frame] = 4 + mv_counts[MV_REF_INTRA];
  }

  // Clamp vectors
  for (i = 0; i < MAX_MV_REF_CANDIDATES; ++i) {
    clamp_mv_ref(xd, &mv_ref_list[i]);
  }
}
#else
void vp9_find_mv_refs_idx(VP9_COMMON *cm, MACROBLOCKD *xd, MODE_INFO *here,
                          MODE_INFO *lf_here, MV_REFERENCE_FRAME ref_frame,
                          int_mv *mv_ref_list, int *ref_sign_bias,
                          int block_idx) {
  int i;
  MODE_INFO *candidate_mi;
  MB_MODE_INFO * mbmi = &xd->mode_info_context->mbmi;
  int_mv c_refmv;
  int_mv c2_refmv;
  MV_REFERENCE_FRAME c_ref_frame;
  MV_REFERENCE_FRAME c2_ref_frame;
  int candidate_scores[MAX_MV_REF_CANDIDATES] = { 0 };
  int refmv_count = 0;
  int split_count = 0;
  const int (*mv_ref_search)[2];
  const int mi_col = get_mi_col(xd);
  const int mi_row = get_mi_row(xd);
  int intra_count = 0;
  int zero_count = 0;
  int newmv_count = 0;
  int x_idx = 0, y_idx = 0;

  // Blank the reference vector lists and other local structures.
  vpx_memset(mv_ref_list, 0, sizeof(int_mv) * MAX_MV_REF_CANDIDATES);

  mv_ref_search = mv_ref_blocks[mbmi->sb_type];
  if (mbmi->sb_type < BLOCK_SIZE_SB8X8) {
    x_idx = block_idx & 1;
    y_idx = block_idx >> 1;
  }

  // We first scan for candidate vectors that match the current reference frame
  // Look at nearest neigbours
  for (i = 0; i < 2; ++i) {
    const int mi_search_col = mi_col + mv_ref_search[i][0];
    const int mi_search_row = mi_row + mv_ref_search[i][1];
    if ((mi_search_col >= cm->cur_tile_mi_col_start) &&
        (mi_search_col < cm->cur_tile_mi_col_end) &&
        (mi_search_row >= 0) &&
        (mi_search_row < cm->mi_rows)) {
      int b;

      candidate_mi = here + mv_ref_search[i][0] +
                     (mv_ref_search[i][1] * xd->mode_info_stride);

      if (block_idx >= 0) {
        if (mv_ref_search[i][0])
          b = 1 + y_idx * 2;
        else
          b = 2 + x_idx;
      } else {
        b = -1;
      }
      if (get_matching_candidate(candidate_mi, ref_frame, &c_refmv, b)) {
        add_candidate_mv(mv_ref_list, candidate_scores,
                         &refmv_count, c_refmv, 16);
      }
      split_count += (candidate_mi->mbmi.sb_type < BLOCK_SIZE_SB8X8 &&
                      candidate_mi->mbmi.ref_frame[0] != INTRA_FRAME);

      // Count number of neihgbours coded intra and zeromv
      intra_count += (candidate_mi->mbmi.mode < NEARESTMV);
      zero_count += (candidate_mi->mbmi.mode == ZEROMV);
      newmv_count += (candidate_mi->mbmi.mode >= NEWMV);
    }
  }

  // More distant neigbours
  for (i = 2; (i < MVREF_NEIGHBOURS) &&
              (refmv_count < MAX_MV_REF_CANDIDATES); ++i) {
    const int mi_search_col = mi_col + mv_ref_search[i][0];
    const int mi_search_row = mi_row + mv_ref_search[i][1];
    if ((mi_search_col >= cm->cur_tile_mi_col_start) &&
        (mi_search_col < cm->cur_tile_mi_col_end) &&
        (mi_search_row >= 0) && (mi_search_row < cm->mi_rows)) {
      candidate_mi = here + mv_ref_search[i][0] +
                     (mv_ref_search[i][1] * xd->mode_info_stride);

      if (get_matching_candidate(candidate_mi, ref_frame, &c_refmv, -1)) {
        add_candidate_mv(mv_ref_list, candidate_scores,
                         &refmv_count, c_refmv, 16);
      }
    }
  }

  // Look in the last frame if it exists
  if (lf_here && (refmv_count < MAX_MV_REF_CANDIDATES)) {
    candidate_mi = lf_here;
    if (get_matching_candidate(candidate_mi, ref_frame, &c_refmv, -1)) {
      add_candidate_mv(mv_ref_list, candidate_scores,
                       &refmv_count, c_refmv, 16);
    }
  }

  // If we have not found enough candidates consider ones where the
  // reference frame does not match. Break out when we have
  // MAX_MV_REF_CANDIDATES candidates.
  // Look first at spatial neighbours
  for (i = 0; (i < MVREF_NEIGHBOURS) &&
              (refmv_count < MAX_MV_REF_CANDIDATES); ++i) {
    const int mi_search_col = mi_col + mv_ref_search[i][0];
    const int mi_search_row = mi_row + mv_ref_search[i][1];
    if ((mi_search_col >= cm->cur_tile_mi_col_start) &&
        (mi_search_col < cm->cur_tile_mi_col_end) &&
        (mi_search_row >= 0) && (mi_search_row < cm->mi_rows)) {
      candidate_mi = here + mv_ref_search[i][0] +
                     (mv_ref_search[i][1] * xd->mode_info_stride);

      get_non_matching_candidates(candidate_mi, ref_frame,
                                  &c_ref_frame, &c_refmv,
                                  &c2_ref_frame, &c2_refmv);

      if (c_ref_frame != INTRA_FRAME) {
        scale_mv(xd, ref_frame, c_ref_frame, &c_refmv, ref_sign_bias);
        add_candidate_mv(mv_ref_list, candidate_scores,
                         &refmv_count, c_refmv, 1);
      }

      if (c2_ref_frame != INTRA_FRAME) {
        scale_mv(xd, ref_frame, c2_ref_frame, &c2_refmv, ref_sign_bias);
        add_candidate_mv(mv_ref_list, candidate_scores,
                         &refmv_count, c2_refmv, 1);
      }
    }
  }

  // Look at the last frame if it exists
  if (lf_here && (refmv_count < MAX_MV_REF_CANDIDATES)) {
    candidate_mi = lf_here;
    get_non_matching_candidates(candidate_mi, ref_frame,
                                &c_ref_frame, &c_refmv,
                                &c2_ref_frame, &c2_refmv);

    if (c_ref_frame != INTRA_FRAME) {
      scale_mv(xd, ref_frame, c_ref_frame, &c_refmv, ref_sign_bias);
      add_candidate_mv(mv_ref_list, candidate_scores,
                       &refmv_count, c_refmv, 1);
    }

    if (c2_ref_frame != INTRA_FRAME) {
      scale_mv(xd, ref_frame, c2_ref_frame, &c2_refmv, ref_sign_bias);
      add_candidate_mv(mv_ref_list, candidate_scores,
                       &refmv_count, c2_refmv, 1);
    }
  }

  if (!intra_count) {
    if (!newmv_count) {
      // 0 = both zero mv
      // 1 = one zero mv + one a predicted mv
      // 2 = two predicted mvs
      mbmi->mb_mode_context[ref_frame] = 2 - zero_count;
    } else {
      // 3 = one predicted/zero and one new mv
      // 4 = two new mvs
      mbmi->mb_mode_context[ref_frame] = 2 + newmv_count;
    }
  } else {
    // 5 = one intra neighbour + x
    // 6 = two intra neighbours
    mbmi->mb_mode_context[ref_frame] = 4 + intra_count;
  }

  // Clamp vectors
  for (i = 0; i < MAX_MV_REF_CANDIDATES; ++i) {
    clamp_mv_ref(xd, &mv_ref_list[i]);
  }
}
#endif


#endif
