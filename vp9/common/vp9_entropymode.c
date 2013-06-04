/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include "vp9/common/vp9_onyxc_int.h"
#include "vp9/common/vp9_modecont.h"
#include "vp9/common/vp9_seg_common.h"
#include "vp9/common/vp9_alloccommon.h"
#include "vpx_mem/vpx_mem.h"

static const unsigned int y_mode_cts[VP9_INTRA_MODES] = {
  /* DC V  H D45 D135 D117 D153 D27 D63 TM */
  98, 19, 15, 14, 14, 14, 14, 12, 12, 13,
};

static const unsigned int uv_mode_cts[VP9_INTRA_MODES][VP9_INTRA_MODES] = {
  /* DC   V   H  D45 135 117 153 D27 D63 TM */
  { 200, 15, 15, 10, 10, 10, 10, 10, 10,  6}, /* DC */
  { 130, 75, 10, 10, 10, 10, 10, 10, 10,  6}, /* V */
  { 130, 10, 75, 10, 10, 10, 10, 10, 10,  6}, /* H */
  { 130, 15, 10, 75, 10, 10, 10, 10, 10,  6}, /* D45 */
  { 150, 15, 10, 10, 75, 10, 10, 10, 10,  6}, /* D135 */
  { 150, 15, 10, 10, 10, 75, 10, 10, 10,  6}, /* D117 */
  { 150, 15, 10, 10, 10, 10, 75, 10, 10,  6}, /* D153 */
  { 150, 15, 10, 10, 10, 10, 10, 75, 10,  6}, /* D27 */
  { 150, 15, 10, 10, 10, 10, 10, 10, 75,  6}, /* D63 */
  { 160, 30, 30, 10, 10, 10, 10, 10, 10, 16}, /* TM */
};

static const unsigned int kf_uv_mode_cts[VP9_INTRA_MODES][VP9_INTRA_MODES] = {
  // DC   V   H  D45 135 117 153 D27 D63 TM
  { 160, 24, 24, 20, 20, 20, 20, 20, 20,  8}, /* DC */
  { 102, 64, 30, 20, 20, 20, 20, 20, 20, 10}, /* V */
  { 102, 30, 64, 20, 20, 20, 20, 20, 20, 10}, /* H */
  { 102, 33, 20, 64, 20, 20, 20, 20, 20, 14}, /* D45 */
  { 102, 33, 20, 20, 64, 20, 20, 20, 20, 14}, /* D135 */
  { 122, 33, 20, 20, 20, 64, 20, 20, 20, 14}, /* D117 */
  { 102, 33, 20, 20, 20, 20, 64, 20, 20, 14}, /* D153 */
  { 102, 33, 20, 20, 20, 20, 20, 64, 20, 14}, /* D27 */
  { 102, 33, 20, 20, 20, 20, 20, 20, 64, 14}, /* D63 */
  { 132, 36, 30, 20, 20, 20, 20, 20, 20, 18}, /* TM */
};

const vp9_prob vp9_partition_probs[NUM_PARTITION_CONTEXTS]
                                  [PARTITION_TYPES - 1] = {
  // FIXME(jingning,rbultje) put real probabilities here
  {202, 162, 107},
  {16,  2,   169},
  {3,   246,  19},
  {104, 90,  134},
  {202, 162, 107},
  {16,  2,   169},
  {3,   246,  19},
  {104, 90,  134},
  {202, 162, 107},
  {16,  2,   169},
  {3,   246,  19},
  {104, 90,  134},
  {183, 70,  109},
  {30,  14,  162},
  {67,  208,  22},
  {4,   17,   5},
};

/* Array indices are identical to previously-existing INTRAMODECONTEXTNODES. */
const vp9_tree_index vp9_intra_mode_tree[VP9_INTRA_MODES * 2 - 2] = {
  -DC_PRED, 2,                      /* 0 = DC_NODE */
  -TM_PRED, 4,                      /* 1 = TM_NODE */
  -V_PRED, 6,                       /* 2 = V_NODE */
  8, 12,                            /* 3 = COM_NODE */
  -H_PRED, 10,                      /* 4 = H_NODE */
  -D135_PRED, -D117_PRED,           /* 5 = D135_NODE */
  -D45_PRED, 14,                    /* 6 = D45_NODE */
  -D63_PRED, 16,                    /* 7 = D63_NODE */
  -D153_PRED, -D27_PRED             /* 8 = D153_NODE */
};

const vp9_tree_index vp9_sb_mv_ref_tree[6] = {
  -ZEROMV, 2,
  -NEARESTMV, 4,
  -NEARMV, -NEWMV
};

const vp9_tree_index vp9_partition_tree[6] = {
  -PARTITION_NONE, 2,
  -PARTITION_HORZ, 4,
  -PARTITION_VERT, -PARTITION_SPLIT
};

struct vp9_token vp9_intra_mode_encodings[VP9_INTRA_MODES];

struct vp9_token vp9_sb_mv_ref_encoding_array[VP9_MVREFS];

struct vp9_token vp9_partition_encodings[PARTITION_TYPES];

void vp9_init_mbmode_probs(VP9_COMMON *x) {
  unsigned int bct[VP9_INTRA_MODES][2];  // num Ymodes > num UV modes
  int i;

  vp9_tree_probs_from_distribution(vp9_intra_mode_tree, x->fc.y_mode_prob,
                                   bct, y_mode_cts, 0);

  for (i = 0; i < VP9_INTRA_MODES; i++) {
    vp9_tree_probs_from_distribution(vp9_intra_mode_tree, x->kf_uv_mode_prob[i],
                                     bct, kf_uv_mode_cts[i], 0);
    vp9_tree_probs_from_distribution(vp9_intra_mode_tree, x->fc.uv_mode_prob[i],
                                     bct, uv_mode_cts[i], 0);
  }

  vpx_memcpy(x->fc.switchable_interp_prob, vp9_switchable_interp_prob,
             sizeof(vp9_switchable_interp_prob));

  vpx_memcpy(x->fc.partition_prob, vp9_partition_probs,
             sizeof(vp9_partition_probs));

  x->ref_pred_probs[0] = DEFAULT_PRED_PROB_0;
  x->ref_pred_probs[1] = DEFAULT_PRED_PROB_1;
  x->ref_pred_probs[2] = DEFAULT_PRED_PROB_2;
}

#if VP9_SWITCHABLE_FILTERS == 3
const vp9_tree_index vp9_switchable_interp_tree[VP9_SWITCHABLE_FILTERS*2-2] = {
  -0, 2,
  -1, -2
};
struct vp9_token vp9_switchable_interp_encodings[VP9_SWITCHABLE_FILTERS];
const INTERPOLATIONFILTERTYPE vp9_switchable_interp[VP9_SWITCHABLE_FILTERS] = {
  EIGHTTAP, EIGHTTAP_SMOOTH, EIGHTTAP_SHARP};
const int vp9_switchable_interp_map[SWITCHABLE+1] = {1, 0, 2, -1, -1};
const vp9_prob vp9_switchable_interp_prob [VP9_SWITCHABLE_FILTERS+1]
                                          [VP9_SWITCHABLE_FILTERS-1] = {
  {248, 192}, { 32, 248}, { 32,  32}, {192, 160}
};
#elif VP9_SWITCHABLE_FILTERS == 2
const vp9_tree_index vp9_switchable_interp_tree[VP9_SWITCHABLE_FILTERS*2-2] = {
  -0, -1,
};
struct vp9_token vp9_switchable_interp_encodings[VP9_SWITCHABLE_FILTERS];
const vp9_prob vp9_switchable_interp_prob [VP9_SWITCHABLE_FILTERS+1]
                                          [VP9_SWITCHABLE_FILTERS-1] = {
  {248},
  { 64},
  {192},
};
const INTERPOLATIONFILTERTYPE vp9_switchable_interp[VP9_SWITCHABLE_FILTERS] = {
  EIGHTTAP, EIGHTTAP_SHARP};
const int vp9_switchable_interp_map[SWITCHABLE+1] = {-1, 0, 1, -1, -1};
#endif  // VP9_SWITCHABLE_FILTERS

// Indicates if the filter is interpolating or non-interpolating
// Note currently only the EIGHTTAP_SMOOTH is non-interpolating
const int vp9_is_interpolating_filter[SWITCHABLE + 1] = {0, 1, 1, 1, -1};

void vp9_entropy_mode_init() {
  vp9_tokens_from_tree(vp9_intra_mode_encodings, vp9_intra_mode_tree);
  vp9_tokens_from_tree(vp9_switchable_interp_encodings,
                       vp9_switchable_interp_tree);
  vp9_tokens_from_tree(vp9_partition_encodings, vp9_partition_tree);

  vp9_tokens_from_tree_offset(vp9_sb_mv_ref_encoding_array,
                              vp9_sb_mv_ref_tree, NEARESTMV);
}

void vp9_init_mode_contexts(VP9_COMMON *pc) {
  vpx_memset(pc->fc.mv_ref_ct, 0, sizeof(pc->fc.mv_ref_ct));
  vpx_memcpy(pc->fc.vp9_mode_contexts,
             vp9_default_mode_contexts,
             sizeof(vp9_default_mode_contexts));
}

void vp9_accum_mv_refs(VP9_COMMON *pc,
                       MB_PREDICTION_MODE m,
                       const int context) {
  unsigned int (*mv_ref_ct)[VP9_MVREFS - 1][2] = pc->fc.mv_ref_ct;

  if (m == ZEROMV) {
    ++mv_ref_ct[context][0][0];
  } else {
    ++mv_ref_ct[context][0][1];
    if (m == NEARESTMV) {
      ++mv_ref_ct[context][1][0];
    } else {
      ++mv_ref_ct[context][1][1];
      if (m == NEARMV) {
        ++mv_ref_ct[context][2][0];
      } else {
        ++mv_ref_ct[context][2][1];
      }
    }
  }
}

#define MVREF_COUNT_SAT 20
#define MVREF_MAX_UPDATE_FACTOR 128
void vp9_adapt_mode_context(VP9_COMMON *pc) {
  int i, j;
  unsigned int (*mv_ref_ct)[VP9_MVREFS - 1][2] = pc->fc.mv_ref_ct;
  int (*mode_context)[VP9_MVREFS - 1] = pc->fc.vp9_mode_contexts;

  for (j = 0; j < INTER_MODE_CONTEXTS; j++) {
    for (i = 0; i < VP9_MVREFS - 1; i++) {
      int count = mv_ref_ct[j][i][0] + mv_ref_ct[j][i][1], factor;

      count = count > MVREF_COUNT_SAT ? MVREF_COUNT_SAT : count;
      factor = (MVREF_MAX_UPDATE_FACTOR * count / MVREF_COUNT_SAT);
      mode_context[j][i] = weighted_prob(pc->fc.vp9_mode_contexts[j][i],
                                         get_binary_prob(mv_ref_ct[j][i][0],
                                                         mv_ref_ct[j][i][1]),
                                         factor);
    }
  }
}

#define MODE_COUNT_SAT 20
#define MODE_MAX_UPDATE_FACTOR 144
static void update_mode_probs(int n_modes,
                              const vp9_tree_index *tree, unsigned int *cnt,
                              vp9_prob *pre_probs, vp9_prob *dst_probs,
                              unsigned int tok0_offset) {
#define MAX_PROBS 32
  vp9_prob probs[MAX_PROBS];
  unsigned int branch_ct[MAX_PROBS][2];
  int t, count, factor;

  assert(n_modes - 1 < MAX_PROBS);
  vp9_tree_probs_from_distribution(tree, probs, branch_ct, cnt, tok0_offset);
  for (t = 0; t < n_modes - 1; ++t) {
    count = branch_ct[t][0] + branch_ct[t][1];
    count = count > MODE_COUNT_SAT ? MODE_COUNT_SAT : count;
    factor = (MODE_MAX_UPDATE_FACTOR * count / MODE_COUNT_SAT);
    dst_probs[t] = weighted_prob(pre_probs[t], probs[t], factor);
  }
}

// #define MODE_COUNT_TESTING
void vp9_adapt_mode_probs(VP9_COMMON *cm) {
  int i;
  FRAME_CONTEXT *fc = &cm->fc;
#ifdef MODE_COUNT_TESTING
  int t;

  printf("static const unsigned int\nymode_counts"
         "[VP9_INTRA_MODES] = {\n");
  for (t = 0; t < VP9_INTRA_MODES; ++t)
    printf("%d, ", fc->ymode_counts[t]);
  printf("};\n");
  printf("static const unsigned int\nuv_mode_counts"
         "[VP9_INTRA_MODES] [VP9_INTRA_MODES] = {\n");
  for (i = 0; i < VP9_INTRA_MODES; ++i) {
    printf("  {");
    for (t = 0; t < VP9_INTRA_MODES; ++t)
      printf("%d, ", fc->uv_mode_counts[i][t]);
    printf("},\n");
  }
  printf("};\n");
  printf("static const unsigned int\nbmode_counts"
         "[VP9_NKF_BINTRAMODES] = {\n");
  for (t = 0; t < VP9_NKF_BINTRAMODES; ++t)
    printf("%d, ", fc->bmode_counts[t]);
  printf("};\n");
  printf("static const unsigned int\ni8x8_mode_counts"
         "[VP9_I8X8_MODES] = {\n");
  for (t = 0; t < VP9_I8X8_MODES; ++t)
    printf("%d, ", fc->i8x8_mode_counts[t]);
  printf("};\n");
  printf("static const unsigned int\nmbsplit_counts"
         "[VP9_NUMMBSPLITS] = {\n");
  for (t = 0; t < VP9_NUMMBSPLITS; ++t)
    printf("%d, ", fc->mbsplit_counts[t]);
  printf("};\n");
#endif

  update_mode_probs(VP9_INTRA_MODES, vp9_intra_mode_tree,
                    fc->y_mode_counts, fc->pre_y_mode_prob,
                    fc->y_mode_prob, 0);

  for (i = 0; i < VP9_INTRA_MODES; ++i)
    update_mode_probs(VP9_INTRA_MODES, vp9_intra_mode_tree,
                      fc->uv_mode_counts[i], fc->pre_uv_mode_prob[i],
                      fc->uv_mode_prob[i], 0);

  for (i = 0; i < NUM_PARTITION_CONTEXTS; i++)
    update_mode_probs(PARTITION_TYPES, vp9_partition_tree,
                      fc->partition_counts[i], fc->pre_partition_prob[i],
                      fc->partition_prob[i], 0);

  if (cm->mcomp_filter_type == SWITCHABLE) {
    for (i = 0; i <= VP9_SWITCHABLE_FILTERS; i++) {
      update_mode_probs(VP9_SWITCHABLE_FILTERS, vp9_switchable_interp_tree,
                        fc->switchable_interp_count[i],
                        fc->pre_switchable_interp_prob[i],
                        fc->switchable_interp_prob[i], 0);
    }
  }
}

static void set_default_lf_deltas(MACROBLOCKD *xd) {
  xd->mode_ref_lf_delta_enabled = 1;
  xd->mode_ref_lf_delta_update = 1;

  xd->ref_lf_deltas[INTRA_FRAME] = 1;
  xd->ref_lf_deltas[LAST_FRAME] = 0;
  xd->ref_lf_deltas[GOLDEN_FRAME] = -1;
  xd->ref_lf_deltas[ALTREF_FRAME] = -1;

  xd->mode_lf_deltas[0] = 2;               // I4X4_PRED
  xd->mode_lf_deltas[1] = -1;              // Zero
  xd->mode_lf_deltas[2] = 1;               // New mv
  xd->mode_lf_deltas[3] = 2;               // Split mv
}

void vp9_setup_past_independence(VP9_COMMON *cm, MACROBLOCKD *xd) {
  // Reset the segment feature data to the default stats:
  // Features disabled, 0, with delta coding (Default state).
  int i;
  vp9_clearall_segfeatures(xd);
  xd->mb_segment_abs_delta = SEGMENT_DELTADATA;
  if (cm->last_frame_seg_map)
    vpx_memset(cm->last_frame_seg_map, 0, (cm->mi_rows * cm->mi_cols));

  // Reset the mode ref deltas for loop filter
  vpx_memset(xd->last_ref_lf_deltas, 0, sizeof(xd->last_ref_lf_deltas));
  vpx_memset(xd->last_mode_lf_deltas, 0, sizeof(xd->last_mode_lf_deltas));
  set_default_lf_deltas(xd);

  vp9_default_coef_probs(cm);
  vp9_init_mbmode_probs(cm);
  vpx_memcpy(cm->kf_y_mode_prob, vp9_kf_default_bmode_probs,
             sizeof(vp9_kf_default_bmode_probs));
  vp9_init_mv_probs(cm);

  // To force update of the sharpness
  cm->last_sharpness_level = -1;

  vp9_init_mode_contexts(cm);

  for (i = 0; i < NUM_FRAME_CONTEXTS; i++)
    vpx_memcpy(&cm->frame_contexts[i], &cm->fc, sizeof(cm->fc));

  vpx_memset(cm->prev_mip, 0,
             cm->mode_info_stride * (cm->mi_rows + 1) * sizeof(MODE_INFO));
  vpx_memset(cm->mip, 0,
             cm->mode_info_stride * (cm->mi_rows + 1) * sizeof(MODE_INFO));

  vp9_update_mode_info_border(cm, cm->mip);
  vp9_update_mode_info_in_image(cm, cm->mi);

  vp9_update_mode_info_border(cm, cm->prev_mip);
  vp9_update_mode_info_in_image(cm, cm->prev_mi);

  vpx_memset(cm->ref_frame_sign_bias, 0, sizeof(cm->ref_frame_sign_bias));

  cm->frame_context_idx = 0;
}
