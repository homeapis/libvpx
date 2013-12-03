/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include <math.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "vp9/encoder/vp9_onyx_int.h"
#include "vp9/encoder/vp9_tokenize.h"
#include "vpx_mem/vpx_mem.h"

#include "vp9/common/vp9_pred_common.h"
#include "vp9/common/vp9_seg_common.h"
#include "vp9/common/vp9_entropy.h"

static TOKENVALUE dct_value_tokens[DCT_MAX_VALUE * 2];
const TOKENVALUE *vp9_dct_value_tokens_ptr;
static int dct_value_cost[DCT_MAX_VALUE * 2];
const int *vp9_dct_value_cost_ptr;

// Array indices are identical to previously-existing CONTEXT_NODE indices
const vp9_tree_index vp9_coef_tree[TREE_SIZE(MAX_ENTROPY_TOKENS)] = {
  -DCT_EOB_TOKEN, 2,                          /* 0 = EOB */
  -ZERO_TOKEN, 4,                             /* 1 = ZERO */
  -ONE_TOKEN, 6,                              /* 2 = ONE */
  8, 12,                                      /* 3 = LOW_VAL */
  -TWO_TOKEN, 10,                            /* 4 = TWO */
  -THREE_TOKEN, -FOUR_TOKEN,                /* 5 = THREE */
  14, 16,                                   /* 6 = HIGH_LOW */
  -DCT_VAL_CATEGORY1, -DCT_VAL_CATEGORY2,   /* 7 = CAT_ONE */
  18, 20,                                   /* 8 = CAT_THREEFOUR */
  -DCT_VAL_CATEGORY3, -DCT_VAL_CATEGORY4,   /* 9 = CAT_THREE */
  -DCT_VAL_CATEGORY5, -DCT_VAL_CATEGORY6    /* 10 = CAT_FIVE */
};

// Unconstrained Node Tree
const vp9_tree_index vp9_coef_con_tree[TREE_SIZE(MAX_ENTROPY_TOKENS)] = {
  2, 6,                                     /* 0 = LOW_VAL */
  -TWO_TOKEN, 4,                            /* 1 = TWO */
  -THREE_TOKEN, -FOUR_TOKEN,                /* 2 = THREE */
  8, 10,                                    /* 3 = HIGH_LOW */
  -DCT_VAL_CATEGORY1, -DCT_VAL_CATEGORY2,   /* 4 = CAT_ONE */
  12, 14,                                   /* 5 = CAT_THREEFOUR */
  -DCT_VAL_CATEGORY3, -DCT_VAL_CATEGORY4,   /* 6 = CAT_THREE */
  -DCT_VAL_CATEGORY5, -DCT_VAL_CATEGORY6    /* 7 = CAT_FIVE */
};

static const vp9_prob Pcat1[] = { 159};
static const vp9_prob Pcat2[] = { 165, 145};
static const vp9_prob Pcat3[] = { 173, 148, 140};
static const vp9_prob Pcat4[] = { 176, 155, 140, 135};
static const vp9_prob Pcat5[] = { 180, 157, 141, 134, 130};
static const vp9_prob Pcat6[] = {
  254, 254, 254, 252, 249, 243, 230, 196, 177, 153, 140, 133, 130, 129
};

static vp9_tree_index cat1[2], cat2[4], cat3[6], cat4[8], cat5[10], cat6[28];

static void init_bit_tree(vp9_tree_index *p, int n) {
  int i = 0;

  while (++i < n) {
    p[0] = p[1] = i << 1;
    p += 2;
  }

  p[0] = p[1] = 0;
}

static void init_bit_trees() {
  init_bit_tree(cat1, 1);
  init_bit_tree(cat2, 2);
  init_bit_tree(cat3, 3);
  init_bit_tree(cat4, 4);
  init_bit_tree(cat5, 5);
  init_bit_tree(cat6, 14);
}

const vp9_extra_bit vp9_extra_bits[MAX_ENTROPY_TOKENS] = {
  {0, 0, 0, 0},           // ZERO_TOKEN
  {0, 0, 0, 1},           // ONE_TOKEN
  {0, 0, 0, 2},           // TWO_TOKEN
  {0, 0, 0, 3},           // THREE_TOKEN
  {0, 0, 0, 4},           // FOUR_TOKEN
  {cat1, Pcat1, 1, 5},    // DCT_VAL_CATEGORY1
  {cat2, Pcat2, 2, 7},    // DCT_VAL_CATEGORY2
  {cat3, Pcat3, 3, 11},   // DCT_VAL_CATEGORY3
  {cat4, Pcat4, 4, 19},   // DCT_VAL_CATEGORY4
  {cat5, Pcat5, 5, 35},   // DCT_VAL_CATEGORY5
  {cat6, Pcat6, 14, 67},  // DCT_VAL_CATEGORY6
  {0, 0, 0, 0}            // DCT_EOB_TOKEN
};

struct vp9_token vp9_coef_encodings[MAX_ENTROPY_TOKENS];

void vp9_coef_tree_initialize() {
  init_bit_trees();
  vp9_tokens_from_tree(vp9_coef_encodings, vp9_coef_tree);
}

static void fill_value_tokens() {
  TOKENVALUE *const t = dct_value_tokens + DCT_MAX_VALUE;
  const vp9_extra_bit *const e = vp9_extra_bits;

  int i = -DCT_MAX_VALUE;
  int sign = 1;

  do {
    if (!i)
      sign = 0;

    {
      const int a = sign ? -i : i;
      int eb = sign;

      if (a > 4) {
        int j = 4;

        while (++j < 11  &&  e[j].base_val <= a) {}

        t[i].token = --j;
        eb |= (a - e[j].base_val) << 1;
      } else {
        t[i].token = a;
      }
      t[i].extra = eb;
    }

    // initialize the cost for extra bits for all possible coefficient value.
    {
      int cost = 0;
      const vp9_extra_bit *p = &vp9_extra_bits[t[i].token];

      if (p->base_val) {
        const int extra = t[i].extra;
        const int length = p->len;

        if (length)
          cost += treed_cost(p->tree, p->prob, extra >> 1, length);

        cost += vp9_cost_bit(vp9_prob_half, extra & 1); /* sign */
        dct_value_cost[i + DCT_MAX_VALUE] = cost;
      }
    }
  } while (++i < DCT_MAX_VALUE);

  vp9_dct_value_tokens_ptr = dct_value_tokens + DCT_MAX_VALUE;
  vp9_dct_value_cost_ptr = dct_value_cost + DCT_MAX_VALUE;
}

struct tokenize_b_args {
  VP9_COMP *cpi;
  MACROBLOCKD *xd;
  TOKENEXTRA **tp;
  TX_SIZE tx_size;
  uint8_t *token_cache;
};

static void set_entropy_context_b(int plane, int block, BLOCK_SIZE plane_bsize,
                                  TX_SIZE tx_size, void *arg) {
  struct tokenize_b_args* const args = arg;
  MACROBLOCKD *const xd = args->xd;
  struct macroblockd_plane *pd = &xd->plane[plane];
  int aoff, loff;
  txfrm_block_to_raster_xy(plane_bsize, tx_size, block, &aoff, &loff);
  set_contexts(xd, pd, plane_bsize, tx_size, pd->eobs[block] > 0, aoff, loff);
}

static void tokenize_b(int plane, int block, BLOCK_SIZE plane_bsize,
                       TX_SIZE tx_size, void *arg) {
  struct tokenize_b_args* const args = arg;
  VP9_COMP *cpi = args->cpi;
  MACROBLOCKD *xd = args->xd;
  TOKENEXTRA **tp = args->tp;
  uint8_t *token_cache = args->token_cache;
  struct macroblockd_plane *pd = &xd->plane[plane];
  MB_MODE_INFO *mbmi = &xd->mi_8x8[0]->mbmi;
  int pt; /* near block/prev token context index */
  int c = 0, rc = 0;
  TOKENEXTRA *t = *tp;        /* store tokens starting here */
  const int eob = pd->eobs[block];
  const PLANE_TYPE type = pd->plane_type;
  struct macroblock_plane *p = &cpi->mb.plane[plane];
  const int16_t *qcoeff_ptr = BLOCK_OFFSET(p->qcoeff, block);

  const int segment_id = mbmi->segment_id;
  const int16_t *scan;
  const scan_order *so;
  vp9_coeff_count *const counts = cpi->coef_counts[tx_size];
  vp9_coeff_probs_model *const coef_probs = cpi->common.fc.coef_probs[tx_size];
  const int ref = is_inter_block(mbmi);
  const uint8_t *const band_translate = get_band_translate(tx_size);
  const int seg_eob = get_tx_eob(&cpi->common.seg, segment_id, tx_size);

  int aoff, loff;
  txfrm_block_to_raster_xy(plane_bsize, tx_size, block, &aoff, &loff);

  assert((!type && !plane) || (type && plane));

  pt = get_entropy_context(tx_size, pd->above_context + aoff,
                                    pd->left_context + loff);
  so = get_scan(xd, tx_size, type, block);
  scan = so->scan;

  c = 0;
  do {
    const int band = band_translate[c];
    int token;
    int v = 0;
    rc = scan[c];
    if (c)
      pt = get_coef_context(so->neighbors, token_cache, c);
    if (c < eob) {
      v = qcoeff_ptr[rc];
      assert(-DCT_MAX_VALUE <= v  &&  v < DCT_MAX_VALUE);

      t->extra = vp9_dct_value_tokens_ptr[v].extra;
      token    = vp9_dct_value_tokens_ptr[v].token;
    } else {
      token = DCT_EOB_TOKEN;
    }

    t->token = token;
    t->context_tree = coef_probs[type][ref][band][pt];
    t->skip_eob_node = (c > 0) && (token_cache[scan[c - 1]] == 0);

    assert(vp9_coef_encodings[t->token].len - t->skip_eob_node > 0);

    ++counts[type][ref][band][pt][token];
    if (!t->skip_eob_node)
      ++cpi->common.counts.eob_branch[tx_size][type][ref][band][pt];

    token_cache[rc] = vp9_pt_energy_class[token];
    ++t;
  } while (c < eob && ++c < seg_eob);

  *tp = t;

  set_contexts(xd, pd, plane_bsize, tx_size, c > 0, aoff, loff);
}

struct is_skippable_args {
  MACROBLOCKD *xd;
  int *skippable;
};

static void is_skippable(int plane, int block,
                         BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
                         void *argv) {
  struct is_skippable_args *args = argv;
  args->skippable[0] &= (!args->xd->plane[plane].eobs[block]);
}

int vp9_sb_is_skippable(MACROBLOCKD *xd, BLOCK_SIZE bsize) {
  int result = 1;
  struct is_skippable_args args = {xd, &result};
  foreach_transformed_block(xd, bsize, is_skippable, &args);
  return result;
}

int vp9_is_skippable_in_plane(MACROBLOCKD *xd, BLOCK_SIZE bsize,
                              int plane) {
  int result = 1;
  struct is_skippable_args args = {xd, &result};
  foreach_transformed_block_in_plane(xd, bsize, plane, is_skippable, &args);
  return result;
}

void vp9_tokenize_sb(VP9_COMP *cpi, TOKENEXTRA **t, int dry_run,
                     BLOCK_SIZE bsize) {
  VP9_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &cpi->mb.e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi_8x8[0]->mbmi;
  TOKENEXTRA *t_backup = *t;
  const int mb_skip_context = vp9_get_pred_context_mbskip(xd);
  const int skip_inc = !vp9_segfeature_active(&cm->seg, mbmi->segment_id,
                                              SEG_LVL_SKIP);
  struct tokenize_b_args arg = {cpi, xd, t, mbmi->tx_size, cpi->mb.token_cache};

  mbmi->skip_coeff = vp9_sb_is_skippable(xd, bsize);
  if (mbmi->skip_coeff) {
    if (!dry_run)
      cm->counts.mbskip[mb_skip_context][1] += skip_inc;
    reset_skip_context(xd, bsize);
    if (dry_run)
      *t = t_backup;
    return;
  }

  if (!dry_run) {
    cm->counts.mbskip[mb_skip_context][0] += skip_inc;
    foreach_transformed_block(xd, bsize, tokenize_b, &arg);
  } else {
    foreach_transformed_block(xd, bsize, set_entropy_context_b, &arg);
    *t = t_backup;
  }
}

void vp9_tokenize_initialize() {
  fill_value_tokens();
}
