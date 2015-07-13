/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "vpx_mem/vpx_mem.h"
#include "vpx_ports/mem.h"

#include "vp9/common/vp9_blockd.h"
#include "vp9/common/vp9_common.h"
#include "vp9/common/vp9_entropy.h"
#if CONFIG_COEFFICIENT_RANGE_CHECKING
#include "vp9/common/vp9_idct.h"
#endif

#include "vp9/decoder/vp9_detokenize.h"

#define EOB_CONTEXT_NODE            0
#define ZERO_CONTEXT_NODE           1
#define ONE_CONTEXT_NODE            2
#define LOW_VAL_CONTEXT_NODE        0
#define TWO_CONTEXT_NODE            1
#define THREE_CONTEXT_NODE          2
#define HIGH_LOW_CONTEXT_NODE       3
#define CAT_ONE_CONTEXT_NODE        4
#define CAT_THREEFOUR_CONTEXT_NODE  5
#define CAT_THREE_CONTEXT_NODE      6
#define CAT_FIVE_CONTEXT_NODE       7

#define INCREMENT_COUNT(token)                              \
  do {                                                      \
     if (counts)                                            \
       ++coef_counts[band][ctx][token];                     \
  } while (0)

static INLINE int read_coeff(const vp9_prob *probs, int n, vp9_reader *r) {
  int i, val = 0;
  VP9_READ_VARS(r)
  for (i = 0; i < n; ++i) {
    VP9_READ_BIT(r, probs[i]);
    val = val << 1;
    if (VP9_READ_SET) {
      val |= 1;
      VP9_READ_ADJUST_FOR_ONE(r);
    } else {
      VP9_READ_ADJUST_FOR_ZERO
    }
  }
  VP9_READ_STORE(r)
  return val;
}
#define READ_COEFF(v, probs, n, r) \
  val = 0; \
  for (i = 0; i < n; ++i) { \
    VP9_READ_BIT(r, probs[i]); \
    val = val << 1; \
    if (VP9_READ_SET) { \
      val |= 1; \
      VP9_READ_ADJUST_FOR_ONE(r); \
    } else { \
      VP9_READ_ADJUST_FOR_ZERO \
    } \
  } \
  val += v;

static int decode_coefs(const MACROBLOCKD *xd,
                        PLANE_TYPE type,
                        tran_low_t *dqcoeff, TX_SIZE tx_size, const int16_t *dq,
                        int ctx, const int16_t *scan, const int16_t *nb,
                        vp9_reader *r) {
  FRAME_COUNTS *counts = xd->counts;
  const int max_eob = 16 << (tx_size << 1);
  const FRAME_CONTEXT *const fc = xd->fc;
  const int ref = is_inter_block(&xd->mi[0]->mbmi);
  int band, c = 0;
  const vp9_prob (*coef_probs)[COEFF_CONTEXTS][UNCONSTRAINED_NODES] =
      fc->coef_probs[tx_size][type][ref];
  const vp9_prob *prob;
  unsigned int (*coef_counts)[COEFF_CONTEXTS][UNCONSTRAINED_NODES + 1];
  unsigned int (*eob_branch_count)[COEFF_CONTEXTS];
  uint8_t token_cache[32 * 32];
  const uint8_t *band_translate = get_band_translate(tx_size);
  const int dq_shift = (tx_size == TX_32X32);
  int v, i;
  int16_t dqv = dq[0];
  const uint8_t *cat1_prob;
  const uint8_t *cat2_prob;
  const uint8_t *cat3_prob;
  const uint8_t *cat4_prob;
  const uint8_t *cat5_prob;
  const uint8_t *cat6_prob;
  VP9_READ_VARS(r)

  if (counts) {
    coef_counts = counts->coef[tx_size][type][ref];
    eob_branch_count = counts->eob_branch[tx_size][type][ref];
  }

#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->bd > VPX_BITS_8) {
    if (xd->bd == VPX_BITS_10) {
      cat1_prob = vp9_cat1_prob_high10;
      cat2_prob = vp9_cat2_prob_high10;
      cat3_prob = vp9_cat3_prob_high10;
      cat4_prob = vp9_cat4_prob_high10;
      cat5_prob = vp9_cat5_prob_high10;
      cat6_prob = vp9_cat6_prob_high10;
    } else {
      cat1_prob = vp9_cat1_prob_high12;
      cat2_prob = vp9_cat2_prob_high12;
      cat3_prob = vp9_cat3_prob_high12;
      cat4_prob = vp9_cat4_prob_high12;
      cat5_prob = vp9_cat5_prob_high12;
      cat6_prob = vp9_cat6_prob_high12;
    }
  } else {
    cat1_prob = vp9_cat1_prob;
    cat2_prob = vp9_cat2_prob;
    cat3_prob = vp9_cat3_prob;
    cat4_prob = vp9_cat4_prob;
    cat5_prob = vp9_cat5_prob;
    cat6_prob = vp9_cat6_prob;
  }
#else
  cat1_prob = vp9_cat1_prob;
  cat2_prob = vp9_cat2_prob;
  cat3_prob = vp9_cat3_prob;
  cat4_prob = vp9_cat4_prob;
  cat5_prob = vp9_cat5_prob;
  cat6_prob = vp9_cat6_prob;
#endif

  while (c < max_eob) {
    int val = -1;
    band = *band_translate++;
    prob = coef_probs[band][ctx];
    if (counts)
      ++eob_branch_count[band][ctx];

    VP9_READ_BIT(r, prob[EOB_CONTEXT_NODE]);
    if (!VP9_READ_SET) {
      VP9_READ_ADJUST_FOR_ZERO
      INCREMENT_COUNT(EOB_MODEL_TOKEN);
      break;
    }
    VP9_READ_ADJUST_FOR_ONE(r)
    while (1) {
      VP9_READ_BIT(r, prob[ZERO_CONTEXT_NODE])
      if (VP9_READ_SET) {
        VP9_READ_ADJUST_FOR_ONE(r)
        break;
      }
      VP9_READ_ADJUST_FOR_ZERO
      INCREMENT_COUNT(ZERO_TOKEN);
      dqv = dq[1];
      token_cache[scan[c]] = 0;
      ++c;
      if (c >= max_eob) {
        VP9_READ_STORE(r)
        return c;  // zero tokens at the end (no eob token)
      }
      ctx = get_coef_context(nb, token_cache, c);
      band = *band_translate++;
      prob = coef_probs[band][ctx];
    }
    VP9_READ_BIT(r, prob[ONE_CONTEXT_NODE])
    if (VP9_READ_SET) {
      const vp9_prob *p = vp9_pareto8_full[prob[PIVOT_NODE] - 1];
      VP9_READ_ADJUST_FOR_ONE(r)
      INCREMENT_COUNT(TWO_TOKEN);
      VP9_READ_BIT(r, p[0]);
      if (VP9_READ_SET) {
        VP9_READ_ADJUST_FOR_ONE(r);
        VP9_READ_BIT(r, p[3]);
        if (VP9_READ_SET) {
          VP9_READ_ADJUST_FOR_ONE(r);
          VP9_READ_BIT(r, p[5]);
          if (VP9_READ_SET) {
            VP9_READ_ADJUST_FOR_ONE(r);
            VP9_READ_BIT(r, p[7]);
            if (VP9_READ_SET) {
              VP9_READ_ADJUST_FOR_ONE(r);
              token_cache[scan[c]] = vp9_pt_energy_class[CATEGORY6_TOKEN];
    #if CONFIG_VP9_HIGHBITDEPTH
              switch (xd->bd) {
                case VPX_BITS_8:
                  READ_COEFF(CAT6_MIN_VAL, cat6_prob, 14, r);
                  break;
                case VPX_BITS_10:
                  READ_COEFF(CAT6_MIN_VAL, cat6_prob, 16, r);
                  break;
                case VPX_BITS_12:
                  READ_COEFF(CAT6_MIN_VAL, cat6_prob, 18, r);
                  break;
                default:
                  assert(0);
                  return -1;
              }
    #else
              READ_COEFF(CAT6_MIN_VAL, cat6_prob, 14, r);
    #endif
            } else {
              VP9_READ_ADJUST_FOR_ZERO
              token_cache[scan[c]] = vp9_pt_energy_class[CATEGORY5_TOKEN];
              READ_COEFF(CAT5_MIN_VAL, cat5_prob, 5, r);
            }
          } else {
            VP9_READ_ADJUST_FOR_ZERO
            VP9_READ_BIT(r, p[6]);
            if (VP9_READ_SET) {
              VP9_READ_ADJUST_FOR_ONE(r);
              token_cache[scan[c]] = vp9_pt_energy_class[CATEGORY4_TOKEN];
              READ_COEFF(CAT4_MIN_VAL, cat4_prob, 4, r);
            } else {
              VP9_READ_ADJUST_FOR_ZERO
              token_cache[scan[c]] = vp9_pt_energy_class[CATEGORY3_TOKEN];
              READ_COEFF(CAT3_MIN_VAL, cat3_prob, 3, r);
            }
          }
        } else {
          VP9_READ_ADJUST_FOR_ZERO
          VP9_READ_BIT(r, p[4]);
          if (VP9_READ_SET) {
            VP9_READ_ADJUST_FOR_ONE(r);
            token_cache[scan[c]] = vp9_pt_energy_class[CATEGORY2_TOKEN];
            READ_COEFF(CAT2_MIN_VAL, cat2_prob, 2, r);
          } else {
            VP9_READ_ADJUST_FOR_ZERO
            token_cache[scan[c]] = vp9_pt_energy_class[CATEGORY1_TOKEN];
            READ_COEFF(CAT1_MIN_VAL, cat1_prob, 1, r);
          }
        }
      } else {
        VP9_READ_ADJUST_FOR_ZERO
        VP9_READ_BIT(r, p[1]);
        if (VP9_READ_SET) {
          VP9_READ_ADJUST_FOR_ONE(r);
          VP9_READ_BIT(r, p[2]);
          if (VP9_READ_SET) {
            VP9_READ_ADJUST_FOR_ONE(r);
            token_cache[scan[c]] = vp9_pt_energy_class[FOUR_TOKEN];
            val = 4;
          } else {
            VP9_READ_ADJUST_FOR_ZERO
            token_cache[scan[c]] = vp9_pt_energy_class[THREE_TOKEN];
            val = 3;
          }
        } else {
          VP9_READ_ADJUST_FOR_ZERO
          token_cache[scan[c]] = vp9_pt_energy_class[TWO_TOKEN];
          val = 2;
        }
      }
    } else {
      VP9_READ_ADJUST_FOR_ZERO
      token_cache[scan[c]] = vp9_pt_energy_class[ONE_TOKEN];
      INCREMENT_COUNT(ONE_TOKEN);
      val = 1;
    }
    v = (val * dqv) >> dq_shift;
    VP9_READ_BIT(r, 128)
#if CONFIG_COEFFICIENT_RANGE_CHECKING
#if CONFIG_VP9_HIGHBITDEPTH
    if (VP9_READ_SET) {
       VP9_READ_ADJUST_FOR_ONE(r)
       dqcoeff[scan[c]] = highbd_check_range(-v, cm->bit_depth);
    } else {
      VP9_READ_ADJUST_FOR_ZERO
      dqcoeff[scan[c]] = highbd_check_range(v, cm->bit_depth);
    }
#else
    if (VP9_READ_SET) {
       VP9_READ_ADJUST_FOR_ONE(r)
       dqcoeff[scan[c]] = check_range(-v, cm->bit_depth);
    } else {
      VP9_READ_ADJUST_FOR_ZERO
      dqcoeff[scan[c]] = check_range(v, cm->bit_depth);
    }
#endif  // CONFIG_VP9_HIGHBITDEPTH
#else
    if (VP9_READ_SET) {
       VP9_READ_ADJUST_FOR_ONE(r)
       dqcoeff[scan[c]] = -v;
    } else {
      VP9_READ_ADJUST_FOR_ZERO
      dqcoeff[scan[c]] = v;
    }
#endif  // CONFIG_COEFFICIENT_RANGE_CHECKING
    ++c;
    ctx = get_coef_context(nb, token_cache, c);
    dqv = dq[1];
  }
  VP9_READ_STORE(r)

  return c;
}

// TODO(slavarnway): Decode version of vp9_set_context.  Modify vp9_set_context
// after testing is complete, then delete this version.
static
void dec_set_contexts(const MACROBLOCKD *xd, struct macroblockd_plane *pd,
                      TX_SIZE tx_size, int has_eob,
                      int aoff, int loff) {
  ENTROPY_CONTEXT *const a = pd->above_context + aoff;
  ENTROPY_CONTEXT *const l = pd->left_context + loff;
  const int tx_size_in_blocks = 1 << tx_size;

  // above
  if (has_eob && xd->mb_to_right_edge < 0) {
    int i;
    const int blocks_wide = pd->n4_w +
                            (xd->mb_to_right_edge >> (5 + pd->subsampling_x));
    int above_contexts = tx_size_in_blocks;
    if (above_contexts + aoff > blocks_wide)
      above_contexts = blocks_wide - aoff;

    for (i = 0; i < above_contexts; ++i)
      a[i] = has_eob;
    for (i = above_contexts; i < tx_size_in_blocks; ++i)
      a[i] = 0;
  } else {
    memset(a, has_eob, sizeof(ENTROPY_CONTEXT) * tx_size_in_blocks);
  }

  // left
  if (has_eob && xd->mb_to_bottom_edge < 0) {
    int i;
    const int blocks_high = pd->n4_h +
                            (xd->mb_to_bottom_edge >> (5 + pd->subsampling_y));
    int left_contexts = tx_size_in_blocks;
    if (left_contexts + loff > blocks_high)
      left_contexts = blocks_high - loff;

    for (i = 0; i < left_contexts; ++i)
      l[i] = has_eob;
    for (i = left_contexts; i < tx_size_in_blocks; ++i)
      l[i] = 0;
  } else {
    memset(l, has_eob, sizeof(ENTROPY_CONTEXT) * tx_size_in_blocks);
  }
}

int vp9_decode_block_tokens(MACROBLOCKD *xd,
                            int plane, const scan_order *sc,
                            int x, int y,
                            TX_SIZE tx_size, vp9_reader *r,
                            int seg_id) {
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const int16_t *const dequant = pd->seg_dequant[seg_id];
  const int ctx = get_entropy_context(tx_size, pd->above_context + x,
                                               pd->left_context + y);
  const int eob = decode_coefs(xd, pd->plane_type,
                               pd->dqcoeff, tx_size,
                               dequant, ctx, sc->scan, sc->neighbors, r);
  dec_set_contexts(xd, pd, tx_size, eob > 0, x, y);
  return eob;
}


