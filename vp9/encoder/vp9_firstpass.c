/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <limits.h>
#include <math.h>
#include <stdio.h>

#include "./vpx_dsp_rtcd.h"
#include "./vpx_scale_rtcd.h"

#include "vpx_dsp/vpx_dsp_common.h"
#include "vpx_mem/vpx_mem.h"
#include "vpx_ports/mem.h"
#include "vpx_ports/system_state.h"
#include "vpx_scale/vpx_scale.h"
#include "vpx_scale/yv12config.h"

#include "vp9/common/vp9_entropymv.h"
#include "vp9/common/vp9_quant_common.h"
#include "vp9/common/vp9_reconinter.h"  // vp9_setup_dst_planes()
#include "vp9/encoder/vp9_aq_variance.h"
#include "vp9/encoder/vp9_block.h"
#include "vp9/encoder/vp9_encodeframe.h"
#include "vp9/encoder/vp9_encodemb.h"
#include "vp9/encoder/vp9_encodemv.h"
#include "vp9/encoder/vp9_encoder.h"
#include "vp9/encoder/vp9_ethread.h"
#include "vp9/encoder/vp9_extend.h"
#include "vp9/encoder/vp9_firstpass.h"
#include "vp9/encoder/vp9_mcomp.h"
#include "vp9/encoder/vp9_quantize.h"
#include "vp9/encoder/vp9_rd.h"
#include "vpx_dsp/variance.h"

#define OUTPUT_FPF 0
#define ARF_STATS_OUTPUT 0
#define COMPLEXITY_STATS_OUTPUT 0

#define FIRST_PASS_Q 10.0
#define INTRA_MODE_PENALTY 1024
#define MIN_ARF_GF_BOOST 240
#define MIN_DECAY_FACTOR 0.01
#define NEW_MV_MODE_PENALTY 32
#define DARK_THRESH 64
#define DEFAULT_GRP_WEIGHT 1.0
#define RC_FACTOR_MIN 0.75
#define RC_FACTOR_MAX 1.75
#define SECTION_NOISE_DEF 250.0
#define LOW_I_THRESH 24000

#define NCOUNT_INTRA_THRESH 8192
#define NCOUNT_INTRA_FACTOR 3

#define DOUBLE_DIVIDE_CHECK(x) ((x) < 0 ? (x)-0.000001 : (x) + 0.000001)

#if ARF_STATS_OUTPUT
unsigned int arf_count = 0;
#endif

// Resets the first pass file to the given position using a relative seek from
// the current position.
static void reset_fpf_position(TWO_PASS *p, const FIRSTPASS_STATS *position) {
  p->stats_in = position;
}

// Read frame stats at an offset from the current position.
static const FIRSTPASS_STATS *read_frame_stats(const TWO_PASS *p, int offset) {
  if ((offset >= 0 && p->stats_in + offset >= p->stats_in_end) ||
      (offset < 0 && p->stats_in + offset < p->stats_in_start)) {
    return NULL;
  }

  return &p->stats_in[offset];
}

static int input_stats(TWO_PASS *p, FIRSTPASS_STATS *fps) {
  if (p->stats_in >= p->stats_in_end) return EOF;

  *fps = *p->stats_in;
  ++p->stats_in;
  return 1;
}

static void output_stats(FIRSTPASS_STATS *stats,
                         struct vpx_codec_pkt_list *pktlist) {
  struct vpx_codec_cx_pkt pkt;
  pkt.kind = VPX_CODEC_STATS_PKT;
  pkt.data.twopass_stats.buf = stats;
  pkt.data.twopass_stats.sz = sizeof(FIRSTPASS_STATS);
  vpx_codec_pkt_list_add(pktlist, &pkt);

// TEMP debug code
#if OUTPUT_FPF
  {
    FILE *fpfile;
    fpfile = fopen("firstpass.stt", "a");

    fprintf(fpfile,
            "%12.0lf %12.4lf %12.2lf %12.2lf %12.2lf %12.0lf %12.4lf %12.4lf"
            "%12.4lf %12.4lf %12.4lf %12.4lf %12.4lf %12.4lf %12.4lf %12.4lf"
            "%12.4lf %12.4lf %12.4lf %12.4lf %12.4lf %12.0lf %12.4lf %12.0lf"
            "%12.4lf"
            "\n",
            stats->frame, stats->weight, stats->intra_error, stats->coded_error,
            stats->sr_coded_error, stats->frame_noise_energy, stats->pcnt_inter,
            stats->pcnt_motion, stats->pcnt_second_ref, stats->pcnt_neutral,
            stats->pcnt_intra_low, stats->pcnt_intra_high,
            stats->intra_skip_pct, stats->intra_smooth_pct,
            stats->inactive_zone_rows, stats->inactive_zone_cols, stats->MVr,
            stats->mvr_abs, stats->MVc, stats->mvc_abs, stats->MVrv,
            stats->MVcv, stats->mv_in_out_count, stats->count, stats->duration);
    fclose(fpfile);
  }
#endif
}

#if CONFIG_FP_MB_STATS
static void output_fpmb_stats(uint8_t *this_frame_mb_stats, VP9_COMMON *cm,
                              struct vpx_codec_pkt_list *pktlist) {
  struct vpx_codec_cx_pkt pkt;
  pkt.kind = VPX_CODEC_FPMB_STATS_PKT;
  pkt.data.firstpass_mb_stats.buf = this_frame_mb_stats;
  pkt.data.firstpass_mb_stats.sz = cm->initial_mbs * sizeof(uint8_t);
  vpx_codec_pkt_list_add(pktlist, &pkt);
}
#endif

static void zero_stats(FIRSTPASS_STATS *section) {
  section->frame = 0.0;
  section->weight = 0.0;
  section->intra_error = 0.0;
  section->coded_error = 0.0;
  section->sr_coded_error = 0.0;
  section->frame_noise_energy = 0.0;
  section->pcnt_inter = 0.0;
  section->pcnt_motion = 0.0;
  section->pcnt_second_ref = 0.0;
  section->pcnt_neutral = 0.0;
  section->intra_skip_pct = 0.0;
  section->intra_smooth_pct = 0.0;
  section->pcnt_intra_low = 0.0;
  section->pcnt_intra_high = 0.0;
  section->inactive_zone_rows = 0.0;
  section->inactive_zone_cols = 0.0;
  section->MVr = 0.0;
  section->mvr_abs = 0.0;
  section->MVc = 0.0;
  section->mvc_abs = 0.0;
  section->MVrv = 0.0;
  section->MVcv = 0.0;
  section->mv_in_out_count = 0.0;
  section->count = 0.0;
  section->duration = 1.0;
  section->spatial_layer_id = 0;
}

static void accumulate_stats(FIRSTPASS_STATS *section,
                             const FIRSTPASS_STATS *frame) {
  section->frame += frame->frame;
  section->weight += frame->weight;
  section->spatial_layer_id = frame->spatial_layer_id;
  section->intra_error += frame->intra_error;
  section->coded_error += frame->coded_error;
  section->sr_coded_error += frame->sr_coded_error;
  section->frame_noise_energy += frame->frame_noise_energy;
  section->pcnt_inter += frame->pcnt_inter;
  section->pcnt_motion += frame->pcnt_motion;
  section->pcnt_second_ref += frame->pcnt_second_ref;
  section->pcnt_neutral += frame->pcnt_neutral;
  section->intra_skip_pct += frame->intra_skip_pct;
  section->intra_smooth_pct += frame->intra_smooth_pct;
  section->pcnt_intra_low += frame->pcnt_intra_low;
  section->pcnt_intra_high += frame->pcnt_intra_high;
  section->inactive_zone_rows += frame->inactive_zone_rows;
  section->inactive_zone_cols += frame->inactive_zone_cols;
  section->MVr += frame->MVr;
  section->mvr_abs += frame->mvr_abs;
  section->MVc += frame->MVc;
  section->mvc_abs += frame->mvc_abs;
  section->MVrv += frame->MVrv;
  section->MVcv += frame->MVcv;
  section->mv_in_out_count += frame->mv_in_out_count;
  section->count += frame->count;
  section->duration += frame->duration;
}

static void subtract_stats(FIRSTPASS_STATS *section,
                           const FIRSTPASS_STATS *frame) {
  section->frame -= frame->frame;
  section->weight -= frame->weight;
  section->intra_error -= frame->intra_error;
  section->coded_error -= frame->coded_error;
  section->sr_coded_error -= frame->sr_coded_error;
  section->frame_noise_energy -= frame->frame_noise_energy;
  section->pcnt_inter -= frame->pcnt_inter;
  section->pcnt_motion -= frame->pcnt_motion;
  section->pcnt_second_ref -= frame->pcnt_second_ref;
  section->pcnt_neutral -= frame->pcnt_neutral;
  section->intra_skip_pct -= frame->intra_skip_pct;
  section->intra_smooth_pct -= frame->intra_smooth_pct;
  section->pcnt_intra_low -= frame->pcnt_intra_low;
  section->pcnt_intra_high -= frame->pcnt_intra_high;
  section->inactive_zone_rows -= frame->inactive_zone_rows;
  section->inactive_zone_cols -= frame->inactive_zone_cols;
  section->MVr -= frame->MVr;
  section->mvr_abs -= frame->mvr_abs;
  section->MVc -= frame->MVc;
  section->mvc_abs -= frame->mvc_abs;
  section->MVrv -= frame->MVrv;
  section->MVcv -= frame->MVcv;
  section->mv_in_out_count -= frame->mv_in_out_count;
  section->count -= frame->count;
  section->duration -= frame->duration;
}

// Calculate an active area of the image that discounts formatting
// bars and partially discounts other 0 energy areas.
#define MIN_ACTIVE_AREA 0.5
#define MAX_ACTIVE_AREA 1.0
static double calculate_active_area(const VP9_COMP *cpi,
                                    const FIRSTPASS_STATS *this_frame) {
  double active_pct;

  active_pct =
      1.0 -
      ((this_frame->intra_skip_pct / 2) +
       ((this_frame->inactive_zone_rows * 2) / (double)cpi->common.mb_rows));
  return fclamp(active_pct, MIN_ACTIVE_AREA, MAX_ACTIVE_AREA);
}

// Get the average weighted error for the clip (or corpus)
static double get_distribution_av_err(VP9_COMP *cpi, TWO_PASS *const twopass) {
  const double av_weight =
      twopass->total_stats.weight / twopass->total_stats.count;

  if (cpi->oxcf.vbr_corpus_complexity)
    return av_weight * twopass->mean_mod_score;
  else
    return (twopass->total_stats.coded_error * av_weight) /
           twopass->total_stats.count;
}

#define ACT_AREA_CORRECTION 0.5
// Calculate a modified Error used in distributing bits between easier and
// harder frames.
static double calculate_mod_frame_score(const VP9_COMP *cpi,
                                        const VP9EncoderConfig *oxcf,
                                        const FIRSTPASS_STATS *this_frame,
                                        const double av_err) {
  double modified_score =
      av_err * pow(this_frame->coded_error * this_frame->weight /
                       DOUBLE_DIVIDE_CHECK(av_err),
                   oxcf->two_pass_vbrbias / 100.0);

  // Correction for active area. Frames with a reduced active area
  // (eg due to formatting bars) have a higher error per mb for the
  // remaining active MBs. The correction here assumes that coding
  // 0.5N blocks of complexity 2X is a little easier than coding N
  // blocks of complexity X.
  modified_score *=
      pow(calculate_active_area(cpi, this_frame), ACT_AREA_CORRECTION);

  return modified_score;
}

static double calculate_norm_frame_score(const VP9_COMP *cpi,
                                         const TWO_PASS *twopass,
                                         const VP9EncoderConfig *oxcf,
                                         const FIRSTPASS_STATS *this_frame,
                                         const double av_err) {
  double modified_score =
      av_err * pow(this_frame->coded_error * this_frame->weight /
                       DOUBLE_DIVIDE_CHECK(av_err),
                   oxcf->two_pass_vbrbias / 100.0);

  const double min_score = (double)(oxcf->two_pass_vbrmin_section) / 100.0;
  const double max_score = (double)(oxcf->two_pass_vbrmax_section) / 100.0;

  // Correction for active area. Frames with a reduced active area
  // (eg due to formatting bars) have a higher error per mb for the
  // remaining active MBs. The correction here assumes that coding
  // 0.5N blocks of complexity 2X is a little easier than coding N
  // blocks of complexity X.
  modified_score *=
      pow(calculate_active_area(cpi, this_frame), ACT_AREA_CORRECTION);

  // Normalize to a midpoint score.
  modified_score /= DOUBLE_DIVIDE_CHECK(twopass->mean_mod_score);

  return fclamp(modified_score, min_score, max_score);
}

// This function returns the maximum target rate per frame.
static int frame_max_bits(const RATE_CONTROL *rc,
                          const VP9EncoderConfig *oxcf) {
  int64_t max_bits = ((int64_t)rc->avg_frame_bandwidth *
                      (int64_t)oxcf->two_pass_vbrmax_section) /
                     100;
  if (max_bits < 0)
    max_bits = 0;
  else if (max_bits > rc->max_frame_bandwidth)
    max_bits = rc->max_frame_bandwidth;

  return (int)max_bits;
}

void vp9_init_first_pass(VP9_COMP *cpi) {
  zero_stats(&cpi->twopass.total_stats);
}

void vp9_end_first_pass(VP9_COMP *cpi) {
  output_stats(&cpi->twopass.total_stats, cpi->output_pkt_list);
  vpx_free(cpi->twopass.fp_mb_float_stats);
  cpi->twopass.fp_mb_float_stats = NULL;
}

static vpx_variance_fn_t get_block_variance_fn(BLOCK_SIZE bsize) {
  switch (bsize) {
    case BLOCK_8X8: return vpx_mse8x8;
    case BLOCK_16X8: return vpx_mse16x8;
    case BLOCK_8X16: return vpx_mse8x16;
    default: return vpx_mse16x16;
  }
}

static unsigned int get_prediction_error(BLOCK_SIZE bsize,
                                         const struct buf_2d *src,
                                         const struct buf_2d *ref) {
  unsigned int sse;
  const vpx_variance_fn_t fn = get_block_variance_fn(bsize);
  fn(src->buf, src->stride, ref->buf, ref->stride, &sse);
  return sse;
}

#if CONFIG_VP9_HIGHBITDEPTH
static vpx_variance_fn_t highbd_get_block_variance_fn(BLOCK_SIZE bsize,
                                                      int bd) {
  switch (bd) {
    default:
      switch (bsize) {
        case BLOCK_8X8: return vpx_highbd_8_mse8x8;
        case BLOCK_16X8: return vpx_highbd_8_mse16x8;
        case BLOCK_8X16: return vpx_highbd_8_mse8x16;
        default: return vpx_highbd_8_mse16x16;
      }
      break;
    case 10:
      switch (bsize) {
        case BLOCK_8X8: return vpx_highbd_10_mse8x8;
        case BLOCK_16X8: return vpx_highbd_10_mse16x8;
        case BLOCK_8X16: return vpx_highbd_10_mse8x16;
        default: return vpx_highbd_10_mse16x16;
      }
      break;
    case 12:
      switch (bsize) {
        case BLOCK_8X8: return vpx_highbd_12_mse8x8;
        case BLOCK_16X8: return vpx_highbd_12_mse16x8;
        case BLOCK_8X16: return vpx_highbd_12_mse8x16;
        default: return vpx_highbd_12_mse16x16;
      }
      break;
  }
}

static unsigned int highbd_get_prediction_error(BLOCK_SIZE bsize,
                                                const struct buf_2d *src,
                                                const struct buf_2d *ref,
                                                int bd) {
  unsigned int sse;
  const vpx_variance_fn_t fn = highbd_get_block_variance_fn(bsize, bd);
  fn(src->buf, src->stride, ref->buf, ref->stride, &sse);
  return sse;
}
#endif  // CONFIG_VP9_HIGHBITDEPTH

// Refine the motion search range according to the frame dimension
// for first pass test.
static int get_search_range(const VP9_COMP *cpi) {
  int sr = 0;
  const int dim = VPXMIN(cpi->initial_width, cpi->initial_height);

  while ((dim << sr) < MAX_FULL_PEL_VAL) ++sr;
  return sr;
}

static void first_pass_motion_search(VP9_COMP *cpi, MACROBLOCK *x,
                                     const MV *ref_mv, MV *best_mv,
                                     int *best_motion_err) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MV tmp_mv = { 0, 0 };
  MV ref_mv_full = { ref_mv->row >> 3, ref_mv->col >> 3 };
  int num00, tmp_err, n;
  const BLOCK_SIZE bsize = xd->mi[0]->sb_type;
  vp9_variance_fn_ptr_t v_fn_ptr = cpi->fn_ptr[bsize];
  const int new_mv_mode_penalty = NEW_MV_MODE_PENALTY;

  int step_param = 3;
  int further_steps = (MAX_MVSEARCH_STEPS - 1) - step_param;
  const int sr = get_search_range(cpi);
  step_param += sr;
  further_steps -= sr;

  // Override the default variance function to use MSE.
  v_fn_ptr.vf = get_block_variance_fn(bsize);
#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    v_fn_ptr.vf = highbd_get_block_variance_fn(bsize, xd->bd);
  }
#endif  // CONFIG_VP9_HIGHBITDEPTH

  // Center the initial step/diamond search on best mv.
  tmp_err = cpi->diamond_search_sad(x, &cpi->ss_cfg, &ref_mv_full, &tmp_mv,
                                    step_param, x->sadperbit16, &num00,
                                    &v_fn_ptr, ref_mv);
  if (tmp_err < INT_MAX)
    tmp_err = vp9_get_mvpred_var(x, &tmp_mv, ref_mv, &v_fn_ptr, 1);
  if (tmp_err < INT_MAX - new_mv_mode_penalty) tmp_err += new_mv_mode_penalty;

  if (tmp_err < *best_motion_err) {
    *best_motion_err = tmp_err;
    *best_mv = tmp_mv;
  }

  // Carry out further step/diamond searches as necessary.
  n = num00;
  num00 = 0;

  while (n < further_steps) {
    ++n;

    if (num00) {
      --num00;
    } else {
      tmp_err = cpi->diamond_search_sad(x, &cpi->ss_cfg, &ref_mv_full, &tmp_mv,
                                        step_param + n, x->sadperbit16, &num00,
                                        &v_fn_ptr, ref_mv);
      if (tmp_err < INT_MAX)
        tmp_err = vp9_get_mvpred_var(x, &tmp_mv, ref_mv, &v_fn_ptr, 1);
      if (tmp_err < INT_MAX - new_mv_mode_penalty)
        tmp_err += new_mv_mode_penalty;

      if (tmp_err < *best_motion_err) {
        *best_motion_err = tmp_err;
        *best_mv = tmp_mv;
      }
    }
  }
}

static BLOCK_SIZE get_bsize(const VP9_COMMON *cm, int mb_row, int mb_col) {
  if (2 * mb_col + 1 < cm->mi_cols) {
    return 2 * mb_row + 1 < cm->mi_rows ? BLOCK_16X16 : BLOCK_16X8;
  } else {
    return 2 * mb_row + 1 < cm->mi_rows ? BLOCK_8X16 : BLOCK_8X8;
  }
}

static int find_fp_qindex(vpx_bit_depth_t bit_depth) {
  int i;

  for (i = 0; i < QINDEX_RANGE; ++i)
    if (vp9_convert_qindex_to_q(i, bit_depth) >= FIRST_PASS_Q) break;

  if (i == QINDEX_RANGE) i--;

  return i;
}

static void set_first_pass_params(VP9_COMP *cpi) {
  VP9_COMMON *const cm = &cpi->common;
  if (!cpi->refresh_alt_ref_frame &&
      (cm->current_video_frame == 0 || (cpi->frame_flags & FRAMEFLAGS_KEY))) {
    cm->frame_type = KEY_FRAME;
  } else {
    cm->frame_type = INTER_FRAME;
  }
  // Do not use periodic key frames.
  cpi->rc.frames_to_key = INT_MAX;
}

// Scale an sse threshold to account for 8/10/12 bit.
static int scale_sse_threshold(VP9_COMMON *cm, int thresh) {
  int ret_val = thresh;
#if CONFIG_VP9_HIGHBITDEPTH
  if (cm->use_highbitdepth) {
    switch (cm->bit_depth) {
      case VPX_BITS_8: ret_val = thresh; break;
      case VPX_BITS_10: ret_val = thresh << 4; break;
      default:
        assert(cm->bit_depth == VPX_BITS_12);
        ret_val = thresh << 8;
        break;
    }
  }
#else
  (void)cm;
#endif  // CONFIG_VP9_HIGHBITDEPTH
  return ret_val;
}

// This threshold is used to track blocks where to all intents and purposes
// the intra prediction error 0. Though the metric we test against
// is technically a sse we are mainly interested in blocks where all the pixels
// in the 8 bit domain have an error of <= 1 (where error = sse) so a
// linear scaling for 10 and 12 bit gives similar results.
#define UL_INTRA_THRESH 50
static int get_ul_intra_threshold(VP9_COMMON *cm) {
  int ret_val = UL_INTRA_THRESH;
#if CONFIG_VP9_HIGHBITDEPTH
  if (cm->use_highbitdepth) {
    switch (cm->bit_depth) {
      case VPX_BITS_8: ret_val = UL_INTRA_THRESH; break;
      case VPX_BITS_10: ret_val = UL_INTRA_THRESH << 2; break;
      default:
        assert(cm->bit_depth == VPX_BITS_12);
        ret_val = UL_INTRA_THRESH << 4;
        break;
    }
  }
#else
  (void)cm;
#endif  // CONFIG_VP9_HIGHBITDEPTH
  return ret_val;
}

#define SMOOTH_INTRA_THRESH 4000
static int get_smooth_intra_threshold(VP9_COMMON *cm) {
  int ret_val = SMOOTH_INTRA_THRESH;
#if CONFIG_VP9_HIGHBITDEPTH
  if (cm->use_highbitdepth) {
    switch (cm->bit_depth) {
      case VPX_BITS_8: ret_val = SMOOTH_INTRA_THRESH; break;
      case VPX_BITS_10: ret_val = SMOOTH_INTRA_THRESH << 4; break;
      default:
        assert(cm->bit_depth == VPX_BITS_12);
        ret_val = SMOOTH_INTRA_THRESH << 8;
        break;
    }
  }
#else
  (void)cm;
#endif  // CONFIG_VP9_HIGHBITDEPTH
  return ret_val;
}

#define FP_DN_THRESH 8
#define FP_MAX_DN_THRESH 16
#define KERNEL_SIZE 3

// Baseline Kernal weights for first pass noise metric
static uint8_t fp_dn_kernal_3[KERNEL_SIZE * KERNEL_SIZE] = { 1, 2, 1, 2, 4,
                                                             2, 1, 2, 1 };

// Estimate noise at a single point based on the impace of a spatial kernal
// on the point value
static int fp_estimate_point_noise(uint8_t *src_ptr, const int stride) {
  int sum_weight = 0;
  int sum_val = 0;
  int i, j;
  int max_diff = 0;
  int diff;
  int dn_diff;
  uint8_t *tmp_ptr;
  uint8_t *kernal_ptr;
  uint8_t dn_val;
  uint8_t centre_val = *src_ptr;

  kernal_ptr = fp_dn_kernal_3;

  // Apply the kernal
  tmp_ptr = src_ptr - stride - 1;
  for (i = 0; i < KERNEL_SIZE; ++i) {
    for (j = 0; j < KERNEL_SIZE; ++j) {
      diff = abs((int)centre_val - (int)tmp_ptr[j]);
      max_diff = VPXMAX(max_diff, diff);
      if (diff <= FP_DN_THRESH) {
        sum_weight += *kernal_ptr;
        sum_val += (int)tmp_ptr[j] * (int)*kernal_ptr;
      }
      ++kernal_ptr;
    }
    tmp_ptr += stride;
  }

  if (max_diff < FP_MAX_DN_THRESH)
    // Update the source value with the new filtered value
    dn_val = (sum_val + (sum_weight >> 1)) / sum_weight;
  else
    dn_val = *src_ptr;

  // return the noise energy as the square of the difference between the
  // denoised and raw value.
  dn_diff = (int)*src_ptr - (int)dn_val;
  return dn_diff * dn_diff;
}
#if CONFIG_VP9_HIGHBITDEPTH
static int fp_highbd_estimate_point_noise(uint8_t *src_ptr, const int stride) {
  int sum_weight = 0;
  int sum_val = 0;
  int i, j;
  int max_diff = 0;
  int diff;
  int dn_diff;
  uint8_t *tmp_ptr;
  uint16_t *tmp_ptr16;
  uint8_t *kernal_ptr;
  uint16_t dn_val;
  uint16_t centre_val = *CONVERT_TO_SHORTPTR(src_ptr);

  kernal_ptr = fp_dn_kernal_3;

  // Apply the kernal
  tmp_ptr = src_ptr - stride - 1;
  for (i = 0; i < KERNEL_SIZE; ++i) {
    tmp_ptr16 = CONVERT_TO_SHORTPTR(tmp_ptr);
    for (j = 0; j < KERNEL_SIZE; ++j) {
      diff = abs((int)centre_val - (int)tmp_ptr16[j]);
      max_diff = VPXMAX(max_diff, diff);
      if (diff <= FP_DN_THRESH) {
        sum_weight += *kernal_ptr;
        sum_val += (int)tmp_ptr16[j] * (int)*kernal_ptr;
      }
      ++kernal_ptr;
    }
    tmp_ptr += stride;
  }

  if (max_diff < FP_MAX_DN_THRESH)
    // Update the source value with the new filtered value
    dn_val = (sum_val + (sum_weight >> 1)) / sum_weight;
  else
    dn_val = *CONVERT_TO_SHORTPTR(src_ptr);

  // return the noise energy as the square of the difference between the
  // denoised and raw value.
  dn_diff = (int)(*CONVERT_TO_SHORTPTR(src_ptr)) - (int)dn_val;
  return dn_diff * dn_diff;
}
#endif

// Estimate noise for a block.
static int fp_estimate_block_noise(MACROBLOCK *x, BLOCK_SIZE bsize) {
#if CONFIG_VP9_HIGHBITDEPTH
  MACROBLOCKD *xd = &x->e_mbd;
#endif
  uint8_t *src_ptr = &x->plane[0].src.buf[0];
  const int width = num_4x4_blocks_wide_lookup[bsize] * 4;
  const int height = num_4x4_blocks_high_lookup[bsize] * 4;
  int w, h;
  int stride = x->plane[0].src.stride;
  int block_noise = 0;

  // Sampled points to reduce cost overhead.
  for (h = 0; h < height; h += 2) {
    for (w = 0; w < width; w += 2) {
#if CONFIG_VP9_HIGHBITDEPTH
      if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH)
        block_noise += fp_highbd_estimate_point_noise(src_ptr, stride);
      else
        block_noise += fp_estimate_point_noise(src_ptr, stride);
#else
      block_noise += fp_estimate_point_noise(src_ptr, stride);
#endif
      ++src_ptr;
    }
    src_ptr += (stride - width);
  }
  return block_noise << 2;  // Scale << 2 to account for sampling.
}

// This function is called to test the functionality of row based
// multi-threading in unit tests for bit-exactness
static void accumulate_floating_point_stats(VP9_COMP *cpi,
                                            TileDataEnc *first_tile_col) {
  VP9_COMMON *const cm = &cpi->common;
  int mb_row, mb_col;
  first_tile_col->fp_data.intra_factor = 0;
  first_tile_col->fp_data.brightness_factor = 0;
  first_tile_col->fp_data.neutral_count = 0;
  for (mb_row = 0; mb_row < cm->mb_rows; ++mb_row) {
    for (mb_col = 0; mb_col < cm->mb_cols; ++mb_col) {
      const int mb_index = mb_row * cm->mb_cols + mb_col;
      first_tile_col->fp_data.intra_factor +=
          cpi->twopass.fp_mb_float_stats[mb_index].frame_mb_intra_factor;
      first_tile_col->fp_data.brightness_factor +=
          cpi->twopass.fp_mb_float_stats[mb_index].frame_mb_brightness_factor;
      first_tile_col->fp_data.neutral_count +=
          cpi->twopass.fp_mb_float_stats[mb_index].frame_mb_neutral_count;
    }
  }
}

static void first_pass_stat_calc(VP9_COMP *cpi, FIRSTPASS_STATS *fps,
                                 FIRSTPASS_DATA *fp_acc_data) {
  VP9_COMMON *const cm = &cpi->common;
  // The minimum error here insures some bit allocation to frames even
  // in static regions. The allocation per MB declines for larger formats
  // where the typical "real" energy per MB also falls.
  // Initial estimate here uses sqrt(mbs) to define the min_err, where the
  // number of mbs is proportional to the image area.
  const int num_mbs = (cpi->oxcf.resize_mode != RESIZE_NONE) ? cpi->initial_mbs
                                                             : cpi->common.MBs;
  const double min_err = 200 * sqrt(num_mbs);

  // Clamp the image start to rows/2. This number of rows is discarded top
  // and bottom as dead data so rows / 2 means the frame is blank.
  if ((fp_acc_data->image_data_start_row > cm->mb_rows / 2) ||
      (fp_acc_data->image_data_start_row == INVALID_ROW)) {
    fp_acc_data->image_data_start_row = cm->mb_rows / 2;
  }
  // Exclude any image dead zone
  if (fp_acc_data->image_data_start_row > 0) {
    fp_acc_data->intra_skip_count =
        VPXMAX(0, fp_acc_data->intra_skip_count -
                      (fp_acc_data->image_data_start_row * cm->mb_cols * 2));
  }

  fp_acc_data->intra_factor = fp_acc_data->intra_factor / (double)num_mbs;
  fp_acc_data->brightness_factor =
      fp_acc_data->brightness_factor / (double)num_mbs;
  fps->weight = fp_acc_data->intra_factor * fp_acc_data->brightness_factor;

  fps->frame = cm->current_video_frame;
  fps->spatial_layer_id = cpi->svc.spatial_layer_id;

  fps->coded_error =
      ((double)(fp_acc_data->coded_error >> 8) + min_err) / num_mbs;
  fps->sr_coded_error =
      ((double)(fp_acc_data->sr_coded_error >> 8) + min_err) / num_mbs;
  fps->intra_error =
      ((double)(fp_acc_data->intra_error >> 8) + min_err) / num_mbs;

  fps->frame_noise_energy =
      (double)(fp_acc_data->frame_noise_energy) / (double)num_mbs;
  fps->count = 1.0;
  fps->pcnt_inter = (double)(fp_acc_data->intercount) / num_mbs;
  fps->pcnt_second_ref = (double)(fp_acc_data->second_ref_count) / num_mbs;
  fps->pcnt_neutral = (double)(fp_acc_data->neutral_count) / num_mbs;
  fps->pcnt_intra_low = (double)(fp_acc_data->intra_count_low) / num_mbs;
  fps->pcnt_intra_high = (double)(fp_acc_data->intra_count_high) / num_mbs;
  fps->intra_skip_pct = (double)(fp_acc_data->intra_skip_count) / num_mbs;
  fps->intra_smooth_pct = (double)(fp_acc_data->intra_smooth_count) / num_mbs;
  fps->inactive_zone_rows = (double)(fp_acc_data->image_data_start_row);
  // Currently set to 0 as most issues relate to letter boxing.
  fps->inactive_zone_cols = (double)0;

  if (fp_acc_data->mvcount > 0) {
    fps->MVr = (double)(fp_acc_data->sum_mvr) / fp_acc_data->mvcount;
    fps->mvr_abs = (double)(fp_acc_data->sum_mvr_abs) / fp_acc_data->mvcount;
    fps->MVc = (double)(fp_acc_data->sum_mvc) / fp_acc_data->mvcount;
    fps->mvc_abs = (double)(fp_acc_data->sum_mvc_abs) / fp_acc_data->mvcount;
    fps->MVrv = ((double)(fp_acc_data->sum_mvrs) -
                 ((double)(fp_acc_data->sum_mvr) * (fp_acc_data->sum_mvr) /
                  fp_acc_data->mvcount)) /
                fp_acc_data->mvcount;
    fps->MVcv = ((double)(fp_acc_data->sum_mvcs) -
                 ((double)(fp_acc_data->sum_mvc) * (fp_acc_data->sum_mvc) /
                  fp_acc_data->mvcount)) /
                fp_acc_data->mvcount;
    fps->mv_in_out_count =
        (double)(fp_acc_data->sum_in_vectors) / (fp_acc_data->mvcount * 2);
    fps->pcnt_motion = (double)(fp_acc_data->mvcount) / num_mbs;
  } else {
    fps->MVr = 0.0;
    fps->mvr_abs = 0.0;
    fps->MVc = 0.0;
    fps->mvc_abs = 0.0;
    fps->MVrv = 0.0;
    fps->MVcv = 0.0;
    fps->mv_in_out_count = 0.0;
    fps->pcnt_motion = 0.0;
  }
}

static void accumulate_fp_mb_row_stat(TileDataEnc *this_tile,
                                      FIRSTPASS_DATA *fp_acc_data) {
  this_tile->fp_data.intra_factor += fp_acc_data->intra_factor;
  this_tile->fp_data.brightness_factor += fp_acc_data->brightness_factor;
  this_tile->fp_data.coded_error += fp_acc_data->coded_error;
  this_tile->fp_data.sr_coded_error += fp_acc_data->sr_coded_error;
  this_tile->fp_data.frame_noise_energy += fp_acc_data->frame_noise_energy;
  this_tile->fp_data.intra_error += fp_acc_data->intra_error;
  this_tile->fp_data.intercount += fp_acc_data->intercount;
  this_tile->fp_data.second_ref_count += fp_acc_data->second_ref_count;
  this_tile->fp_data.neutral_count += fp_acc_data->neutral_count;
  this_tile->fp_data.intra_count_low += fp_acc_data->intra_count_low;
  this_tile->fp_data.intra_count_high += fp_acc_data->intra_count_high;
  this_tile->fp_data.intra_skip_count += fp_acc_data->intra_skip_count;
  this_tile->fp_data.mvcount += fp_acc_data->mvcount;
  this_tile->fp_data.sum_mvr += fp_acc_data->sum_mvr;
  this_tile->fp_data.sum_mvr_abs += fp_acc_data->sum_mvr_abs;
  this_tile->fp_data.sum_mvc += fp_acc_data->sum_mvc;
  this_tile->fp_data.sum_mvc_abs += fp_acc_data->sum_mvc_abs;
  this_tile->fp_data.sum_mvrs += fp_acc_data->sum_mvrs;
  this_tile->fp_data.sum_mvcs += fp_acc_data->sum_mvcs;
  this_tile->fp_data.sum_in_vectors += fp_acc_data->sum_in_vectors;
  this_tile->fp_data.intra_smooth_count += fp_acc_data->intra_smooth_count;
  this_tile->fp_data.image_data_start_row =
      VPXMIN(this_tile->fp_data.image_data_start_row,
             fp_acc_data->image_data_start_row) == INVALID_ROW
          ? VPXMAX(this_tile->fp_data.image_data_start_row,
                   fp_acc_data->image_data_start_row)
          : VPXMIN(this_tile->fp_data.image_data_start_row,
                   fp_acc_data->image_data_start_row);
}

void vp9_first_pass_encode_tile_mb_row(VP9_COMP *cpi, ThreadData *td,
                                       FIRSTPASS_DATA *fp_acc_data,
                                       TileDataEnc *tile_data, MV *best_ref_mv,
                                       int mb_row) {
  int mb_col;
  MACROBLOCK *const x = &td->mb;
  VP9_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  TileInfo tile = tile_data->tile_info;
  struct macroblock_plane *const p = x->plane;
  struct macroblockd_plane *const pd = xd->plane;
  const PICK_MODE_CONTEXT *ctx = &td->pc_root->none;
  int i, c;
  int num_mb_cols = get_num_cols(tile_data->tile_info, 1);

  int recon_yoffset, recon_uvoffset;
  const int intrapenalty = INTRA_MODE_PENALTY;
  const MV zero_mv = { 0, 0 };
  int recon_y_stride, recon_uv_stride, uv_mb_height;

  YV12_BUFFER_CONFIG *const lst_yv12 = get_ref_frame_buffer(cpi, LAST_FRAME);
  YV12_BUFFER_CONFIG *gld_yv12 = get_ref_frame_buffer(cpi, GOLDEN_FRAME);
  YV12_BUFFER_CONFIG *const new_yv12 = get_frame_new_buffer(cm);
  const YV12_BUFFER_CONFIG *first_ref_buf = lst_yv12;

  MODE_INFO mi_above, mi_left;

  double mb_intra_factor;
  double mb_brightness_factor;
  double mb_neutral_count;

  // First pass code requires valid last and new frame buffers.
  assert(new_yv12 != NULL);
  assert(frame_is_intra_only(cm) || (lst_yv12 != NULL));

  xd->mi = cm->mi_grid_visible + xd->mi_stride * (mb_row << 1) +
           (tile.mi_col_start >> 1);
  xd->mi[0] = cm->mi + xd->mi_stride * (mb_row << 1) + (tile.mi_col_start >> 1);

  for (i = 0; i < MAX_MB_PLANE; ++i) {
    p[i].coeff = ctx->coeff_pbuf[i][1];
    p[i].qcoeff = ctx->qcoeff_pbuf[i][1];
    pd[i].dqcoeff = ctx->dqcoeff_pbuf[i][1];
    p[i].eobs = ctx->eobs_pbuf[i][1];
  }

  recon_y_stride = new_yv12->y_stride;
  recon_uv_stride = new_yv12->uv_stride;
  uv_mb_height = 16 >> (new_yv12->y_height > new_yv12->uv_height);

  // Reset above block coeffs.
  recon_yoffset =
      (mb_row * recon_y_stride * 16) + (tile.mi_col_start >> 1) * 16;
  recon_uvoffset = (mb_row * recon_uv_stride * uv_mb_height) +
                   (tile.mi_col_start >> 1) * uv_mb_height;

  // Set up limit values for motion vectors to prevent them extending
  // outside the UMV borders.
  x->mv_limits.row_min = -((mb_row * 16) + BORDER_MV_PIXELS_B16);
  x->mv_limits.row_max =
      ((cm->mb_rows - 1 - mb_row) * 16) + BORDER_MV_PIXELS_B16;

  for (mb_col = tile.mi_col_start >> 1, c = 0; mb_col < (tile.mi_col_end >> 1);
       ++mb_col, c++) {
    int this_error;
    int this_intra_error;
    const int use_dc_pred = (mb_col || mb_row) && (!mb_col || !mb_row);
    const BLOCK_SIZE bsize = get_bsize(cm, mb_row, mb_col);
    double log_intra;
    int level_sample;
    const int mb_index = mb_row * cm->mb_cols + mb_col;

#if CONFIG_FP_MB_STATS
    const int mb_index = mb_row * cm->mb_cols + mb_col;
#endif

    (*(cpi->row_mt_sync_read_ptr))(&tile_data->row_mt_sync, mb_row, c);

    // Adjust to the next column of MBs.
    x->plane[0].src.buf = cpi->Source->y_buffer +
                          mb_row * 16 * x->plane[0].src.stride + mb_col * 16;
    x->plane[1].src.buf = cpi->Source->u_buffer +
                          mb_row * uv_mb_height * x->plane[1].src.stride +
                          mb_col * uv_mb_height;
    x->plane[2].src.buf = cpi->Source->v_buffer +
                          mb_row * uv_mb_height * x->plane[1].src.stride +
                          mb_col * uv_mb_height;

    vpx_clear_system_state();

    xd->plane[0].dst.buf = new_yv12->y_buffer + recon_yoffset;
    xd->plane[1].dst.buf = new_yv12->u_buffer + recon_uvoffset;
    xd->plane[2].dst.buf = new_yv12->v_buffer + recon_uvoffset;
    xd->mi[0]->sb_type = bsize;
    xd->mi[0]->ref_frame[0] = INTRA_FRAME;
    set_mi_row_col(xd, &tile, mb_row << 1, num_8x8_blocks_high_lookup[bsize],
                   mb_col << 1, num_8x8_blocks_wide_lookup[bsize], cm->mi_rows,
                   cm->mi_cols);
    // Are edges available for intra prediction?
    // Since the firstpass does not populate the mi_grid_visible,
    // above_mi/left_mi must be overwritten with a nonzero value when edges
    // are available.  Required by vp9_predict_intra_block().
    xd->above_mi = (mb_row != 0) ? &mi_above : NULL;
    xd->left_mi = ((mb_col << 1) > tile.mi_col_start) ? &mi_left : NULL;

    // Do intra 16x16 prediction.
    x->skip_encode = 0;
    x->fp_src_pred = 0;
    // Do intra prediction based on source pixels for tile boundaries
    if ((mb_col == (tile.mi_col_start >> 1)) && mb_col != 0) {
      xd->left_mi = &mi_left;
      x->fp_src_pred = 1;
    }
    xd->mi[0]->mode = DC_PRED;
    xd->mi[0]->tx_size =
        use_dc_pred ? (bsize >= BLOCK_16X16 ? TX_16X16 : TX_8X8) : TX_4X4;
    // Fix - zero the 16x16 block first. This ensures correct this_error for
    // block sizes smaller than 16x16.
    vp9_zero_array(x->plane[0].src_diff, 256);
    vp9_encode_intra_block_plane(x, bsize, 0, 0);
    this_error = vpx_get_mb_ss(x->plane[0].src_diff);
    this_intra_error = this_error;

    // Keep a record of blocks that have very low intra error residual
    // (i.e. are in effect completely flat and untextured in the intra
    // domain). In natural videos this is uncommon, but it is much more
    // common in animations, graphics and screen content, so may be used
    // as a signal to detect these types of content.
    if (this_error < get_ul_intra_threshold(cm)) {
      ++(fp_acc_data->intra_skip_count);
    } else if ((mb_col > 0) &&
               (fp_acc_data->image_data_start_row == INVALID_ROW)) {
      fp_acc_data->image_data_start_row = mb_row;
    }

    // Blocks that are mainly smooth in the intra domain.
    // Some special accounting for CQ but also these are better for testing
    // noise levels.
    if (this_error < get_smooth_intra_threshold(cm)) {
      ++(fp_acc_data->intra_smooth_count);
    }

    // Special case noise measurement for first frame.
    if (cm->current_video_frame == 0) {
      if (this_intra_error < scale_sse_threshold(cm, LOW_I_THRESH)) {
        fp_acc_data->frame_noise_energy += fp_estimate_block_noise(x, bsize);
      } else {
        fp_acc_data->frame_noise_energy += (int64_t)SECTION_NOISE_DEF;
      }
    }

#if CONFIG_VP9_HIGHBITDEPTH
    if (cm->use_highbitdepth) {
      switch (cm->bit_depth) {
        case VPX_BITS_8: break;
        case VPX_BITS_10: this_error >>= 4; break;
        default:
          assert(cm->bit_depth == VPX_BITS_12);
          this_error >>= 8;
          break;
      }
    }
#endif  // CONFIG_VP9_HIGHBITDEPTH

    vpx_clear_system_state();
    log_intra = log(this_error + 1.0);
    if (log_intra < 10.0) {
      mb_intra_factor = 1.0 + ((10.0 - log_intra) * 0.05);
      fp_acc_data->intra_factor += mb_intra_factor;
      if (cpi->row_mt_bit_exact)
        cpi->twopass.fp_mb_float_stats[mb_index].frame_mb_intra_factor =
            mb_intra_factor;
    } else {
      fp_acc_data->intra_factor += 1.0;
      if (cpi->row_mt_bit_exact)
        cpi->twopass.fp_mb_float_stats[mb_index].frame_mb_intra_factor = 1.0;
    }

#if CONFIG_VP9_HIGHBITDEPTH
    if (cm->use_highbitdepth)
      level_sample = CONVERT_TO_SHORTPTR(x->plane[0].src.buf)[0];
    else
      level_sample = x->plane[0].src.buf[0];
#else
    level_sample = x->plane[0].src.buf[0];
#endif
    if ((level_sample < DARK_THRESH) && (log_intra < 9.0)) {
      mb_brightness_factor = 1.0 + (0.01 * (DARK_THRESH - level_sample));
      fp_acc_data->brightness_factor += mb_brightness_factor;
      if (cpi->row_mt_bit_exact)
        cpi->twopass.fp_mb_float_stats[mb_index].frame_mb_brightness_factor =
            mb_brightness_factor;
    } else {
      fp_acc_data->brightness_factor += 1.0;
      if (cpi->row_mt_bit_exact)
        cpi->twopass.fp_mb_float_stats[mb_index].frame_mb_brightness_factor =
            1.0;
    }

    // Intrapenalty below deals with situations where the intra and inter
    // error scores are very low (e.g. a plain black frame).
    // We do not have special cases in first pass for 0,0 and nearest etc so
    // all inter modes carry an overhead cost estimate for the mv.
    // When the error score is very low this causes us to pick all or lots of
    // INTRA modes and throw lots of key frames.
    // This penalty adds a cost matching that of a 0,0 mv to the intra case.
    this_error += intrapenalty;

    // Accumulate the intra error.
    fp_acc_data->intra_error += (int64_t)this_error;

#if CONFIG_FP_MB_STATS
    if (cpi->use_fp_mb_stats) {
      // initialization
      cpi->twopass.frame_mb_stats_buf[mb_index] = 0;
    }
#endif

    // Set up limit values for motion vectors to prevent them extending
    // outside the UMV borders.
    x->mv_limits.col_min = -((mb_col * 16) + BORDER_MV_PIXELS_B16);
    x->mv_limits.col_max =
        ((cm->mb_cols - 1 - mb_col) * 16) + BORDER_MV_PIXELS_B16;

    // Other than for the first frame do a motion search.
    if (cm->current_video_frame > 0) {
      int tmp_err, motion_error, raw_motion_error;
      // Assume 0,0 motion with no mv overhead.
      MV mv = { 0, 0 }, tmp_mv = { 0, 0 };
      struct buf_2d unscaled_last_source_buf_2d;

      xd->plane[0].pre[0].buf = first_ref_buf->y_buffer + recon_yoffset;
#if CONFIG_VP9_HIGHBITDEPTH
      if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
        motion_error = highbd_get_prediction_error(
            bsize, &x->plane[0].src, &xd->plane[0].pre[0], xd->bd);
      } else {
        motion_error =
            get_prediction_error(bsize, &x->plane[0].src, &xd->plane[0].pre[0]);
      }
#else
      motion_error =
          get_prediction_error(bsize, &x->plane[0].src, &xd->plane[0].pre[0]);
#endif  // CONFIG_VP9_HIGHBITDEPTH

      // Compute the motion error of the 0,0 motion using the last source
      // frame as the reference. Skip the further motion search on
      // reconstructed frame if this error is small.
      unscaled_last_source_buf_2d.buf =
          cpi->unscaled_last_source->y_buffer + recon_yoffset;
      unscaled_last_source_buf_2d.stride = cpi->unscaled_last_source->y_stride;
#if CONFIG_VP9_HIGHBITDEPTH
      if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
        raw_motion_error = highbd_get_prediction_error(
            bsize, &x->plane[0].src, &unscaled_last_source_buf_2d, xd->bd);
      } else {
        raw_motion_error = get_prediction_error(bsize, &x->plane[0].src,
                                                &unscaled_last_source_buf_2d);
      }
#else
      raw_motion_error = get_prediction_error(bsize, &x->plane[0].src,
                                              &unscaled_last_source_buf_2d);
#endif  // CONFIG_VP9_HIGHBITDEPTH

      // TODO(pengchong): Replace the hard-coded threshold
      if (raw_motion_error > 25) {
        // Test last reference frame using the previous best mv as the
        // starting point (best reference) for the search.
        first_pass_motion_search(cpi, x, best_ref_mv, &mv, &motion_error);

        // If the current best reference mv is not centered on 0,0 then do a
        // 0,0 based search as well.
        if (!is_zero_mv(best_ref_mv)) {
          tmp_err = INT_MAX;
          first_pass_motion_search(cpi, x, &zero_mv, &tmp_mv, &tmp_err);

          if (tmp_err < motion_error) {
            motion_error = tmp_err;
            mv = tmp_mv;
          }
        }

        // Search in an older reference frame.
        if ((cm->current_video_frame > 1) && gld_yv12 != NULL) {
          // Assume 0,0 motion with no mv overhead.
          int gf_motion_error;

          xd->plane[0].pre[0].buf = gld_yv12->y_buffer + recon_yoffset;
#if CONFIG_VP9_HIGHBITDEPTH
          if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
            gf_motion_error = highbd_get_prediction_error(
                bsize, &x->plane[0].src, &xd->plane[0].pre[0], xd->bd);
          } else {
            gf_motion_error = get_prediction_error(bsize, &x->plane[0].src,
                                                   &xd->plane[0].pre[0]);
          }
#else
          gf_motion_error = get_prediction_error(bsize, &x->plane[0].src,
                                                 &xd->plane[0].pre[0]);
#endif  // CONFIG_VP9_HIGHBITDEPTH

          first_pass_motion_search(cpi, x, &zero_mv, &tmp_mv, &gf_motion_error);

          if (gf_motion_error < motion_error && gf_motion_error < this_error)
            ++(fp_acc_data->second_ref_count);

          // Reset to last frame as reference buffer.
          xd->plane[0].pre[0].buf = first_ref_buf->y_buffer + recon_yoffset;
          xd->plane[1].pre[0].buf = first_ref_buf->u_buffer + recon_uvoffset;
          xd->plane[2].pre[0].buf = first_ref_buf->v_buffer + recon_uvoffset;

          // In accumulating a score for the older reference frame take the
          // best of the motion predicted score and the intra coded error
          // (just as will be done for) accumulation of "coded_error" for
          // the last frame.
          if (gf_motion_error < this_error)
            fp_acc_data->sr_coded_error += gf_motion_error;
          else
            fp_acc_data->sr_coded_error += this_error;
        } else {
          fp_acc_data->sr_coded_error += motion_error;
        }
      } else {
        fp_acc_data->sr_coded_error += motion_error;
      }

      // Start by assuming that intra mode is best.
      best_ref_mv->row = 0;
      best_ref_mv->col = 0;

#if CONFIG_FP_MB_STATS
      if (cpi->use_fp_mb_stats) {
        // intra prediction statistics
        cpi->twopass.frame_mb_stats_buf[mb_index] = 0;
        cpi->twopass.frame_mb_stats_buf[mb_index] |= FPMB_DCINTRA_MASK;
        cpi->twopass.frame_mb_stats_buf[mb_index] |= FPMB_MOTION_ZERO_MASK;
        if (this_error > FPMB_ERROR_LARGE_TH) {
          cpi->twopass.frame_mb_stats_buf[mb_index] |= FPMB_ERROR_LARGE_MASK;
        } else if (this_error < FPMB_ERROR_SMALL_TH) {
          cpi->twopass.frame_mb_stats_buf[mb_index] |= FPMB_ERROR_SMALL_MASK;
        }
      }
#endif

      if (motion_error <= this_error) {
        vpx_clear_system_state();

        // Keep a count of cases where the inter and intra were very close
        // and very low. This helps with scene cut detection for example in
        // cropped clips with black bars at the sides or top and bottom.
        if (((this_error - intrapenalty) * 9 <= motion_error * 10) &&
            (this_error < (2 * intrapenalty))) {
          fp_acc_data->neutral_count += 1.0;
          if (cpi->row_mt_bit_exact)
            cpi->twopass.fp_mb_float_stats[mb_index].frame_mb_neutral_count =
                1.0;
          // Also track cases where the intra is not much worse than the inter
          // and use this in limiting the GF/arf group length.
        } else if ((this_error > NCOUNT_INTRA_THRESH) &&
                   (this_error < (NCOUNT_INTRA_FACTOR * motion_error))) {
          mb_neutral_count =
              (double)motion_error / DOUBLE_DIVIDE_CHECK((double)this_error);
          fp_acc_data->neutral_count += mb_neutral_count;
          if (cpi->row_mt_bit_exact)
            cpi->twopass.fp_mb_float_stats[mb_index].frame_mb_neutral_count =
                mb_neutral_count;
        }

        mv.row *= 8;
        mv.col *= 8;
        this_error = motion_error;
        xd->mi[0]->mode = NEWMV;
        xd->mi[0]->mv[0].as_mv = mv;
        xd->mi[0]->tx_size = TX_4X4;
        xd->mi[0]->ref_frame[0] = LAST_FRAME;
        xd->mi[0]->ref_frame[1] = NONE;
        vp9_build_inter_predictors_sby(xd, mb_row << 1, mb_col << 1, bsize);
        vp9_encode_sby_pass1(x, bsize);
        fp_acc_data->sum_mvr += mv.row;
        fp_acc_data->sum_mvr_abs += abs(mv.row);
        fp_acc_data->sum_mvc += mv.col;
        fp_acc_data->sum_mvc_abs += abs(mv.col);
        fp_acc_data->sum_mvrs += mv.row * mv.row;
        fp_acc_data->sum_mvcs += mv.col * mv.col;
        ++(fp_acc_data->intercount);

        *best_ref_mv = mv;

#if CONFIG_FP_MB_STATS
        if (cpi->use_fp_mb_stats) {
          // inter prediction statistics
          cpi->twopass.frame_mb_stats_buf[mb_index] = 0;
          cpi->twopass.frame_mb_stats_buf[mb_index] &= ~FPMB_DCINTRA_MASK;
          cpi->twopass.frame_mb_stats_buf[mb_index] |= FPMB_MOTION_ZERO_MASK;
          if (this_error > FPMB_ERROR_LARGE_TH) {
            cpi->twopass.frame_mb_stats_buf[mb_index] |= FPMB_ERROR_LARGE_MASK;
          } else if (this_error < FPMB_ERROR_SMALL_TH) {
            cpi->twopass.frame_mb_stats_buf[mb_index] |= FPMB_ERROR_SMALL_MASK;
          }
        }
#endif

        if (!is_zero_mv(&mv)) {
          ++(fp_acc_data->mvcount);

#if CONFIG_FP_MB_STATS
          if (cpi->use_fp_mb_stats) {
            cpi->twopass.frame_mb_stats_buf[mb_index] &= ~FPMB_MOTION_ZERO_MASK;
            // check estimated motion direction
            if (mv.as_mv.col > 0 && mv.as_mv.col >= abs(mv.as_mv.row)) {
              // right direction
              cpi->twopass.frame_mb_stats_buf[mb_index] |=
                  FPMB_MOTION_RIGHT_MASK;
            } else if (mv.as_mv.row < 0 &&
                       abs(mv.as_mv.row) >= abs(mv.as_mv.col)) {
              // up direction
              cpi->twopass.frame_mb_stats_buf[mb_index] |= FPMB_MOTION_UP_MASK;
            } else if (mv.as_mv.col < 0 &&
                       abs(mv.as_mv.col) >= abs(mv.as_mv.row)) {
              // left direction
              cpi->twopass.frame_mb_stats_buf[mb_index] |=
                  FPMB_MOTION_LEFT_MASK;
            } else {
              // down direction
              cpi->twopass.frame_mb_stats_buf[mb_index] |=
                  FPMB_MOTION_DOWN_MASK;
            }
          }
#endif

          // Does the row vector point inwards or outwards?
          if (mb_row < cm->mb_rows / 2) {
            if (mv.row > 0)
              --(fp_acc_data->sum_in_vectors);
            else if (mv.row < 0)
              ++(fp_acc_data->sum_in_vectors);
          } else if (mb_row > cm->mb_rows / 2) {
            if (mv.row > 0)
              ++(fp_acc_data->sum_in_vectors);
            else if (mv.row < 0)
              --(fp_acc_data->sum_in_vectors);
          }

          // Does the col vector point inwards or outwards?
          if (mb_col < cm->mb_cols / 2) {
            if (mv.col > 0)
              --(fp_acc_data->sum_in_vectors);
            else if (mv.col < 0)
              ++(fp_acc_data->sum_in_vectors);
          } else if (mb_col > cm->mb_cols / 2) {
            if (mv.col > 0)
              ++(fp_acc_data->sum_in_vectors);
            else if (mv.col < 0)
              --(fp_acc_data->sum_in_vectors);
          }
          fp_acc_data->frame_noise_energy += (int64_t)SECTION_NOISE_DEF;
        } else if (this_intra_error < scale_sse_threshold(cm, LOW_I_THRESH)) {
          fp_acc_data->frame_noise_energy += fp_estimate_block_noise(x, bsize);
        } else {  // 0,0 mv but high error
          fp_acc_data->frame_noise_energy += (int64_t)SECTION_NOISE_DEF;
        }
      } else {  // Intra < inter error
        int scaled_low_intra_thresh = scale_sse_threshold(cm, LOW_I_THRESH);
        if (this_intra_error < scaled_low_intra_thresh) {
          fp_acc_data->frame_noise_energy += fp_estimate_block_noise(x, bsize);
          if (motion_error < scaled_low_intra_thresh) {
            fp_acc_data->intra_count_low += 1.0;
          } else {
            fp_acc_data->intra_count_high += 1.0;
          }
        } else {
          fp_acc_data->frame_noise_energy += (int64_t)SECTION_NOISE_DEF;
          fp_acc_data->intra_count_high += 1.0;
        }
      }
    } else {
      fp_acc_data->sr_coded_error += (int64_t)this_error;
    }
    fp_acc_data->coded_error += (int64_t)this_error;

    recon_yoffset += 16;
    recon_uvoffset += uv_mb_height;

    // Accumulate row level stats to the corresponding tile stats
    if (cpi->row_mt && mb_col == (tile.mi_col_end >> 1) - 1)
      accumulate_fp_mb_row_stat(tile_data, fp_acc_data);

    (*(cpi->row_mt_sync_write_ptr))(&tile_data->row_mt_sync, mb_row, c,
                                    num_mb_cols);
  }
  vpx_clear_system_state();
}

static void first_pass_encode(VP9_COMP *cpi, FIRSTPASS_DATA *fp_acc_data) {
  VP9_COMMON *const cm = &cpi->common;
  int mb_row;
  TileDataEnc tile_data;
  TileInfo *tile = &tile_data.tile_info;
  MV zero_mv = { 0, 0 };
  MV best_ref_mv;
  // Tiling is ignored in the first pass.
  vp9_tile_init(tile, cm, 0, 0);

  for (mb_row = 0; mb_row < cm->mb_rows; ++mb_row) {
    best_ref_mv = zero_mv;
    vp9_first_pass_encode_tile_mb_row(cpi, &cpi->td, fp_acc_data, &tile_data,
                                      &best_ref_mv, mb_row);
  }
}

void vp9_first_pass(VP9_COMP *cpi, const struct lookahead_entry *source) {
  MACROBLOCK *const x = &cpi->td.mb;
  VP9_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  TWO_PASS *twopass = &cpi->twopass;

  YV12_BUFFER_CONFIG *const lst_yv12 = get_ref_frame_buffer(cpi, LAST_FRAME);
  YV12_BUFFER_CONFIG *gld_yv12 = get_ref_frame_buffer(cpi, GOLDEN_FRAME);
  YV12_BUFFER_CONFIG *const new_yv12 = get_frame_new_buffer(cm);
  const YV12_BUFFER_CONFIG *first_ref_buf = lst_yv12;

  BufferPool *const pool = cm->buffer_pool;

  FIRSTPASS_DATA fp_temp_data;
  FIRSTPASS_DATA *fp_acc_data = &fp_temp_data;

  vpx_clear_system_state();
  vp9_zero(fp_temp_data);
  fp_acc_data->image_data_start_row = INVALID_ROW;

  // First pass code requires valid last and new frame buffers.
  assert(new_yv12 != NULL);
  assert(frame_is_intra_only(cm) || (lst_yv12 != NULL));

#if CONFIG_FP_MB_STATS
  if (cpi->use_fp_mb_stats) {
    vp9_zero_array(cpi->twopass.frame_mb_stats_buf, cm->initial_mbs);
  }
#endif

  set_first_pass_params(cpi);
  vp9_set_quantizer(cm, find_fp_qindex(cm->bit_depth));

  vp9_setup_block_planes(&x->e_mbd, cm->subsampling_x, cm->subsampling_y);

  vp9_setup_src_planes(x, cpi->Source, 0, 0);
  vp9_setup_dst_planes(xd->plane, new_yv12, 0, 0);

  if (!frame_is_intra_only(cm)) {
    vp9_setup_pre_planes(xd, 0, first_ref_buf, 0, 0, NULL);
  }

  xd->mi = cm->mi_grid_visible;
  xd->mi[0] = cm->mi;

  vp9_frame_init_quantizer(cpi);

  x->skip_recode = 0;

  vp9_init_mv_probs(cm);
  vp9_initialize_rd_consts(cpi);

  cm->log2_tile_rows = 0;

  if (cpi->row_mt_bit_exact && cpi->twopass.fp_mb_float_stats == NULL)
    CHECK_MEM_ERROR(
        cm, cpi->twopass.fp_mb_float_stats,
        vpx_calloc(cm->MBs * sizeof(*cpi->twopass.fp_mb_float_stats), 1));

  {
    FIRSTPASS_STATS fps;
    TileDataEnc *first_tile_col;
    if (!cpi->row_mt) {
      cm->log2_tile_cols = 0;
      cpi->row_mt_sync_read_ptr = vp9_row_mt_sync_read_dummy;
      cpi->row_mt_sync_write_ptr = vp9_row_mt_sync_write_dummy;
      first_pass_encode(cpi, fp_acc_data);
      first_pass_stat_calc(cpi, &fps, fp_acc_data);
    } else {
      cpi->row_mt_sync_read_ptr = vp9_row_mt_sync_read;
      cpi->row_mt_sync_write_ptr = vp9_row_mt_sync_write;
      if (cpi->row_mt_bit_exact) {
        cm->log2_tile_cols = 0;
        vp9_zero_array(cpi->twopass.fp_mb_float_stats, cm->MBs);
      }
      vp9_encode_fp_row_mt(cpi);
      first_tile_col = &cpi->tile_data[0];
      if (cpi->row_mt_bit_exact)
        accumulate_floating_point_stats(cpi, first_tile_col);
      first_pass_stat_calc(cpi, &fps, &(first_tile_col->fp_data));
    }

    // Dont allow a value of 0 for duration.
    // (Section duration is also defaulted to minimum of 1.0).
    fps.duration = VPXMAX(1.0, (double)(source->ts_end - source->ts_start));

    // Don't want to do output stats with a stack variable!
    twopass->this_frame_stats = fps;
    output_stats(&twopass->this_frame_stats, cpi->output_pkt_list);
    accumulate_stats(&twopass->total_stats, &fps);

#if CONFIG_FP_MB_STATS
    if (cpi->use_fp_mb_stats) {
      output_fpmb_stats(twopass->frame_mb_stats_buf, cm, cpi->output_pkt_list);
    }
#endif
  }

  // Copy the previous Last Frame back into gf and and arf buffers if
  // the prediction is good enough... but also don't allow it to lag too far.
  if ((twopass->sr_update_lag > 3) ||
      ((cm->current_video_frame > 0) &&
       (twopass->this_frame_stats.pcnt_inter > 0.20) &&
       ((twopass->this_frame_stats.intra_error /
         DOUBLE_DIVIDE_CHECK(twopass->this_frame_stats.coded_error)) > 2.0))) {
    if (gld_yv12 != NULL) {
      ref_cnt_fb(pool->frame_bufs, &cm->ref_frame_map[cpi->gld_fb_idx],
                 cm->ref_frame_map[cpi->lst_fb_idx]);
    }
    twopass->sr_update_lag = 1;
  } else {
    ++twopass->sr_update_lag;
  }

  vpx_extend_frame_borders(new_yv12);

  // The frame we just compressed now becomes the last frame.
  ref_cnt_fb(pool->frame_bufs, &cm->ref_frame_map[cpi->lst_fb_idx],
             cm->new_fb_idx);

  // Special case for the first frame. Copy into the GF buffer as a second
  // reference.
  if (cm->current_video_frame == 0 && cpi->gld_fb_idx != INVALID_IDX) {
    ref_cnt_fb(pool->frame_bufs, &cm->ref_frame_map[cpi->gld_fb_idx],
               cm->ref_frame_map[cpi->lst_fb_idx]);
  }

  // Use this to see what the first pass reconstruction looks like.
  if (0) {
    char filename[512];
    FILE *recon_file;
    snprintf(filename, sizeof(filename), "enc%04d.yuv",
             (int)cm->current_video_frame);

    if (cm->current_video_frame == 0)
      recon_file = fopen(filename, "wb");
    else
      recon_file = fopen(filename, "ab");

    (void)fwrite(lst_yv12->buffer_alloc, lst_yv12->frame_size, 1, recon_file);
    fclose(recon_file);
  }

  ++cm->current_video_frame;
  if (cpi->use_svc) vp9_inc_frame_in_layer(cpi);
}

static const double q_pow_term[(QINDEX_RANGE >> 5) + 1] = {
  0.65, 0.70, 0.75, 0.85, 0.90, 0.90, 0.90, 1.00, 1.25
};

static double calc_correction_factor(double err_per_mb, double err_divisor,
                                     int q) {
  const double error_term = err_per_mb / DOUBLE_DIVIDE_CHECK(err_divisor);
  const int index = q >> 5;
  double power_term;

  assert((index >= 0) && (index < (QINDEX_RANGE >> 5)));

  // Adjustment based on quantizer to the power term.
  power_term =
      q_pow_term[index] +
      (((q_pow_term[index + 1] - q_pow_term[index]) * (q % 32)) / 32.0);

  // Calculate correction factor.
  if (power_term < 1.0) assert(error_term >= 0.0);

  return fclamp(pow(error_term, power_term), 0.05, 5.0);
}

static double wq_err_divisor(VP9_COMP *cpi) {
  const VP9_COMMON *const cm = &cpi->common;
  unsigned int screen_area = (cm->width * cm->height);

  // Use a different error per mb factor for calculating boost for
  //  different formats.
  if (screen_area <= 640 * 360) {
    return 115.0;
  } else if (screen_area < 1280 * 720) {
    return 125.0;
  } else if (screen_area <= 1920 * 1080) {
    return 130.0;
  } else if (screen_area < 3840 * 2160) {
    return 150.0;
  }

  // Fall through to here only for 4K and above.
  return 200.0;
}

#define NOISE_FACTOR_MIN 0.9
#define NOISE_FACTOR_MAX 1.1
static int get_twopass_worst_quality(VP9_COMP *cpi, const double section_err,
                                     double inactive_zone, double section_noise,
                                     int section_target_bandwidth) {
  const RATE_CONTROL *const rc = &cpi->rc;
  const VP9EncoderConfig *const oxcf = &cpi->oxcf;
  TWO_PASS *const twopass = &cpi->twopass;
  double last_group_rate_err;

  // Clamp the target rate to VBR min / max limts.
  const int target_rate =
      vp9_rc_clamp_pframe_target_size(cpi, section_target_bandwidth);
  double noise_factor = pow((section_noise / SECTION_NOISE_DEF), 0.5);
  noise_factor = fclamp(noise_factor, NOISE_FACTOR_MIN, NOISE_FACTOR_MAX);
  inactive_zone = fclamp(inactive_zone, 0.0, 1.0);

// TODO(jimbankoski): remove #if here or below when this has been
// well tested.
#if CONFIG_ALWAYS_ADJUST_BPM
  // based on recent history adjust expectations of bits per macroblock.
  last_group_rate_err =
      (double)twopass->rolling_arf_group_actual_bits /
      DOUBLE_DIVIDE_CHECK((double)twopass->rolling_arf_group_target_bits);
  last_group_rate_err = VPXMAX(0.25, VPXMIN(4.0, last_group_rate_err));
  twopass->bpm_factor *= (3.0 + last_group_rate_err) / 4.0;
  twopass->bpm_factor = VPXMAX(0.25, VPXMIN(4.0, twopass->bpm_factor));
#endif

  if (target_rate <= 0) {
    return rc->worst_quality;  // Highest value allowed
  } else {
    const int num_mbs = (cpi->oxcf.resize_mode != RESIZE_NONE)
                            ? cpi->initial_mbs
                            : cpi->common.MBs;
    const double active_pct = VPXMAX(0.01, 1.0 - inactive_zone);
    const int active_mbs = (int)VPXMAX(1, (double)num_mbs * active_pct);
    const double av_err_per_mb = section_err / active_pct;
    const double speed_term = 1.0 + 0.04 * oxcf->speed;
    const int target_norm_bits_per_mb =
        (int)(((uint64_t)target_rate << BPER_MB_NORMBITS) / active_mbs);
    int q;

// TODO(jimbankoski): remove #if here or above when this has been
// well tested.
#if !CONFIG_ALWAYS_ADJUST_BPM
    // based on recent history adjust expectations of bits per macroblock.
    last_group_rate_err =
        (double)twopass->rolling_arf_group_actual_bits /
        DOUBLE_DIVIDE_CHECK((double)twopass->rolling_arf_group_target_bits);
    last_group_rate_err = VPXMAX(0.25, VPXMIN(4.0, last_group_rate_err));
    twopass->bpm_factor *= (3.0 + last_group_rate_err) / 4.0;
    twopass->bpm_factor = VPXMAX(0.25, VPXMIN(4.0, twopass->bpm_factor));
#endif

    // Try and pick a max Q that will be high enough to encode the
    // content at the given rate.
    for (q = rc->best_quality; q < rc->worst_quality; ++q) {
      const double factor =
          calc_correction_factor(av_err_per_mb, wq_err_divisor(cpi), q);
      const int bits_per_mb = vp9_rc_bits_per_mb(
          INTER_FRAME, q,
          factor * speed_term * cpi->twopass.bpm_factor * noise_factor,
          cpi->common.bit_depth);
      if (bits_per_mb <= target_norm_bits_per_mb) break;
    }

    // Restriction on active max q for constrained quality mode.
    if (cpi->oxcf.rc_mode == VPX_CQ) q = VPXMAX(q, oxcf->cq_level);
    return q;
  }
}

static void setup_rf_level_maxq(VP9_COMP *cpi) {
  int i;
  RATE_CONTROL *const rc = &cpi->rc;
  for (i = INTER_NORMAL; i < RATE_FACTOR_LEVELS; ++i) {
    int qdelta = vp9_frame_type_qdelta(cpi, i, rc->worst_quality);
    rc->rf_level_maxq[i] = VPXMAX(rc->worst_quality + qdelta, rc->best_quality);
  }
}

static void init_subsampling(VP9_COMP *cpi) {
  const VP9_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;
  const int w = cm->width;
  const int h = cm->height;
  int i;

  for (i = 0; i < FRAME_SCALE_STEPS; ++i) {
    // Note: Frames with odd-sized dimensions may result from this scaling.
    rc->frame_width[i] = (w * 16) / frame_scale_factor[i];
    rc->frame_height[i] = (h * 16) / frame_scale_factor[i];
  }

  setup_rf_level_maxq(cpi);
}

void calculate_coded_size(VP9_COMP *cpi, int *scaled_frame_width,
                          int *scaled_frame_height) {
  RATE_CONTROL *const rc = &cpi->rc;
  *scaled_frame_width = rc->frame_width[rc->frame_size_selector];
  *scaled_frame_height = rc->frame_height[rc->frame_size_selector];
}

void vp9_init_second_pass(VP9_COMP *cpi) {
  VP9EncoderConfig *const oxcf = &cpi->oxcf;
  RATE_CONTROL *const rc = &cpi->rc;
  TWO_PASS *const twopass = &cpi->twopass;
  double frame_rate;
  FIRSTPASS_STATS *stats;

  zero_stats(&twopass->total_stats);
  zero_stats(&twopass->total_left_stats);

  if (!twopass->stats_in_end) return;

  stats = &twopass->total_stats;

  *stats = *twopass->stats_in_end;
  twopass->total_left_stats = *stats;

  // Scan the first pass file and calculate a modified score for each
  // frame that is used to distribute bits. The modified score is assumed
  // to provide a linear basis for bit allocation. I.e a frame A with a score
  // that is double that of frame B will be allocated 2x as many bits.
  {
    double modified_score_total = 0.0;
    const FIRSTPASS_STATS *s = twopass->stats_in;
    double av_err;

    if (oxcf->vbr_corpus_complexity) {
      twopass->mean_mod_score = (double)oxcf->vbr_corpus_complexity / 10.0;
      av_err = get_distribution_av_err(cpi, twopass);
    } else {
      av_err = get_distribution_av_err(cpi, twopass);
      // The first scan is unclamped and gives a raw average.
      while (s < twopass->stats_in_end) {
        modified_score_total += calculate_mod_frame_score(cpi, oxcf, s, av_err);
        ++s;
      }

      // The average error from this first scan is used to define the midpoint
      // error for the rate distribution function.
      twopass->mean_mod_score =
          modified_score_total / DOUBLE_DIVIDE_CHECK(stats->count);
    }

    // Second scan using clamps based on the previous cycle average.
    // This may modify the total and average somewhat but we dont bother with
    // further itterations.
    modified_score_total = 0.0;
    s = twopass->stats_in;
    while (s < twopass->stats_in_end) {
      modified_score_total +=
          calculate_norm_frame_score(cpi, twopass, oxcf, s, av_err);
      ++s;
    }
    twopass->normalized_score_left = modified_score_total;

    // If using Corpus wide VBR mode then update the clip target bandwidth to
    // reflect how the clip compares to the rest of the corpus.
    if (oxcf->vbr_corpus_complexity) {
      oxcf->target_bandwidth =
          (int64_t)((double)oxcf->target_bandwidth *
                    (twopass->normalized_score_left / stats->count));
    }

#if COMPLEXITY_STATS_OUTPUT
    {
      FILE *compstats;
      compstats = fopen("complexity_stats.stt", "a");
      fprintf(compstats, "%10.3lf\n",
              twopass->normalized_score_left / stats->count);
      fclose(compstats);
    }
#endif
  }

  frame_rate = 10000000.0 * stats->count / stats->duration;
  // Each frame can have a different duration, as the frame rate in the source
  // isn't guaranteed to be constant. The frame rate prior to the first frame
  // encoded in the second pass is a guess. However, the sum duration is not.
  // It is calculated based on the actual durations of all frames from the
  // first pass.
  vp9_new_framerate(cpi, frame_rate);
  twopass->bits_left =
      (int64_t)(stats->duration * oxcf->target_bandwidth / 10000000.0);

  // This variable monitors how far behind the second ref update is lagging.
  twopass->sr_update_lag = 1;

  // Reset the vbr bits off target counters
  rc->vbr_bits_off_target = 0;
  rc->vbr_bits_off_target_fast = 0;
  rc->rate_error_estimate = 0;

  // Static sequence monitor variables.
  twopass->kf_zeromotion_pct = 100;
  twopass->last_kfgroup_zeromotion_pct = 100;

  // Initialize bits per macro_block estimate correction factor.
  twopass->bpm_factor = 1.0;
  // Initialize actual and target bits counters for ARF groups so that
  // at the start we have a neutral bpm adjustment.
  twopass->rolling_arf_group_target_bits = 1;
  twopass->rolling_arf_group_actual_bits = 1;

  if (oxcf->resize_mode != RESIZE_NONE) {
    init_subsampling(cpi);
  }

  // Initialize the arnr strangth adjustment to 0
  twopass->arnr_strength_adjustment = 0;
}

#define SR_DIFF_PART 0.0015
#define INTRA_PART 0.005
#define DEFAULT_DECAY_LIMIT 0.75
#define LOW_SR_DIFF_TRHESH 0.1
#define SR_DIFF_MAX 128.0
#define LOW_CODED_ERR_PER_MB 10.0
#define NCOUNT_FRAME_II_THRESH 6.0

static double get_sr_decay_rate(const VP9_COMP *cpi,
                                const FIRSTPASS_STATS *frame) {
  double sr_diff = (frame->sr_coded_error - frame->coded_error);
  double sr_decay = 1.0;
  double modified_pct_inter;
  double modified_pcnt_intra;
  const double motion_amplitude_part =
      frame->pcnt_motion * ((frame->mvc_abs + frame->mvr_abs) /
                            (cpi->initial_height + cpi->initial_width));

  modified_pct_inter = frame->pcnt_inter;
  if ((frame->coded_error > LOW_CODED_ERR_PER_MB) &&
      ((frame->intra_error / DOUBLE_DIVIDE_CHECK(frame->coded_error)) <
       (double)NCOUNT_FRAME_II_THRESH)) {
    modified_pct_inter =
        frame->pcnt_inter + frame->pcnt_intra_low - frame->pcnt_neutral;
  }
  modified_pcnt_intra = 100 * (1.0 - modified_pct_inter);

  if ((sr_diff > LOW_SR_DIFF_TRHESH)) {
    sr_diff = VPXMIN(sr_diff, SR_DIFF_MAX);
    sr_decay = 1.0 - (SR_DIFF_PART * sr_diff) - motion_amplitude_part -
               (INTRA_PART * modified_pcnt_intra);
  }
  return VPXMAX(sr_decay, DEFAULT_DECAY_LIMIT);
}

// This function gives an estimate of how badly we believe the prediction
// quality is decaying from frame to frame.
static double get_zero_motion_factor(const VP9_COMP *cpi,
                                     const FIRSTPASS_STATS *frame) {
  const double zero_motion_pct = frame->pcnt_inter - frame->pcnt_motion;
  double sr_decay = get_sr_decay_rate(cpi, frame);
  return VPXMIN(sr_decay, zero_motion_pct);
}

#define ZM_POWER_FACTOR 0.75

static double get_prediction_decay_rate(const VP9_COMP *cpi,
                                        const FIRSTPASS_STATS *next_frame) {
  const double sr_decay_rate = get_sr_decay_rate(cpi, next_frame);
  const double zero_motion_factor =
      (0.95 * pow((next_frame->pcnt_inter - next_frame->pcnt_motion),
                  ZM_POWER_FACTOR));

  return VPXMAX(zero_motion_factor,
                (sr_decay_rate + ((1.0 - sr_decay_rate) * zero_motion_factor)));
}

// Function to test for a condition where a complex transition is followed
// by a static section. For example in slide shows where there is a fade
// between slides. This is to help with more optimal kf and gf positioning.
static int detect_transition_to_still(VP9_COMP *cpi, int frame_interval,
                                      int still_interval,
                                      double loop_decay_rate,
                                      double last_decay_rate) {
  TWO_PASS *const twopass = &cpi->twopass;
  RATE_CONTROL *const rc = &cpi->rc;

  // Break clause to detect very still sections after motion
  // For example a static image after a fade or other transition
  // instead of a clean scene cut.
  if (frame_interval > rc->min_gf_interval && loop_decay_rate >= 0.999 &&
      last_decay_rate < 0.9) {
    int j;

    // Look ahead a few frames to see if static condition persists...
    for (j = 0; j < still_interval; ++j) {
      const FIRSTPASS_STATS *stats = &twopass->stats_in[j];
      if (stats >= twopass->stats_in_end) break;

      if (stats->pcnt_inter - stats->pcnt_motion < 0.999) break;
    }

    // Only if it does do we signal a transition to still.
    return j == still_interval;
  }

  return 0;
}

// This function detects a flash through the high relative pcnt_second_ref
// score in the frame following a flash frame. The offset passed in should
// reflect this.
static int detect_flash(const TWO_PASS *twopass, int offset) {
  const FIRSTPASS_STATS *const next_frame = read_frame_stats(twopass, offset);

  // What we are looking for here is a situation where there is a
  // brief break in prediction (such as a flash) but subsequent frames
  // are reasonably well predicted by an earlier (pre flash) frame.
  // The recovery after a flash is indicated by a high pcnt_second_ref
  // compared to pcnt_inter.
  return next_frame != NULL &&
         next_frame->pcnt_second_ref > next_frame->pcnt_inter &&
         next_frame->pcnt_second_ref >= 0.5;
}

// Update the motion related elements to the GF arf boost calculation.
static void accumulate_frame_motion_stats(const FIRSTPASS_STATS *stats,
                                          double *mv_in_out,
                                          double *mv_in_out_accumulator,
                                          double *abs_mv_in_out_accumulator,
                                          double *mv_ratio_accumulator) {
  const double pct = stats->pcnt_motion;

  // Accumulate Motion In/Out of frame stats.
  *mv_in_out = stats->mv_in_out_count * pct;
  *mv_in_out_accumulator += *mv_in_out;
  *abs_mv_in_out_accumulator += fabs(*mv_in_out);

  // Accumulate a measure of how uniform (or conversely how random) the motion
  // field is (a ratio of abs(mv) / mv).
  if (pct > 0.05) {
    const double mvr_ratio =
        fabs(stats->mvr_abs) / DOUBLE_DIVIDE_CHECK(fabs(stats->MVr));
    const double mvc_ratio =
        fabs(stats->mvc_abs) / DOUBLE_DIVIDE_CHECK(fabs(stats->MVc));

    *mv_ratio_accumulator +=
        pct * (mvr_ratio < stats->mvr_abs ? mvr_ratio : stats->mvr_abs);
    *mv_ratio_accumulator +=
        pct * (mvc_ratio < stats->mvc_abs ? mvc_ratio : stats->mvc_abs);
  }
}

#define BASELINE_ERR_PER_MB 12500.0
#define GF_MAX_BOOST 96.0
static double calc_frame_boost(VP9_COMP *cpi, const FIRSTPASS_STATS *this_frame,
                               double this_frame_mv_in_out) {
  double frame_boost;
  const double lq = vp9_convert_qindex_to_q(
      cpi->rc.avg_frame_qindex[INTER_FRAME], cpi->common.bit_depth);
  const double boost_q_correction = VPXMIN((0.5 + (lq * 0.015)), 1.5);
  const double active_area = calculate_active_area(cpi, this_frame);

  // Underlying boost factor is based on inter error ratio.
  frame_boost = (BASELINE_ERR_PER_MB * active_area) /
                DOUBLE_DIVIDE_CHECK(this_frame->coded_error);

  // Small adjustment for cases where there is a zoom out
  if (this_frame_mv_in_out > 0.0)
    frame_boost += frame_boost * (this_frame_mv_in_out * 2.0);

  // Q correction and scalling
  frame_boost = frame_boost * boost_q_correction;

  return VPXMIN(frame_boost, GF_MAX_BOOST * boost_q_correction);
}

static double kf_err_per_mb(VP9_COMP *cpi) {
  const VP9_COMMON *const cm = &cpi->common;
  unsigned int screen_area = (cm->width * cm->height);

  // Use a different error per mb factor for calculating boost for
  //  different formats.
  if (screen_area < 1280 * 720) {
    return 2000.0;
  } else if (screen_area < 1920 * 1080) {
    return 500.0;
  }
  return 250.0;
}

static double calc_kf_frame_boost(VP9_COMP *cpi,
                                  const FIRSTPASS_STATS *this_frame,
                                  double *sr_accumulator,
                                  double this_frame_mv_in_out,
                                  double max_boost) {
  double frame_boost;
  const double lq = vp9_convert_qindex_to_q(
      cpi->rc.avg_frame_qindex[INTER_FRAME], cpi->common.bit_depth);
  const double boost_q_correction = VPXMIN((0.50 + (lq * 0.015)), 2.00);
  const double active_area = calculate_active_area(cpi, this_frame);

  // Underlying boost factor is based on inter error ratio.
  frame_boost = (kf_err_per_mb(cpi) * active_area) /
                DOUBLE_DIVIDE_CHECK(this_frame->coded_error + *sr_accumulator);

  // Update the accumulator for second ref error difference.
  // This is intended to give an indication of how much the coded error is
  // increasing over time.
  *sr_accumulator += (this_frame->sr_coded_error - this_frame->coded_error);
  *sr_accumulator = VPXMAX(0.0, *sr_accumulator);

  // Small adjustment for cases where there is a zoom out
  if (this_frame_mv_in_out > 0.0)
    frame_boost += frame_boost * (this_frame_mv_in_out * 2.0);

  // Q correction and scaling
  // The 40.0 value here is an experimentally derived baseline minimum.
  // This value is in line with the minimum per frame boost in the alt_ref
  // boost calculation.
  frame_boost = ((frame_boost + 40.0) * boost_q_correction);

  return VPXMIN(frame_boost, max_boost * boost_q_correction);
}

static int calc_arf_boost(VP9_COMP *cpi, int f_frames, int b_frames) {
  TWO_PASS *const twopass = &cpi->twopass;
  int i;
  double boost_score = 0.0;
  double mv_ratio_accumulator = 0.0;
  double decay_accumulator = 1.0;
  double this_frame_mv_in_out = 0.0;
  double mv_in_out_accumulator = 0.0;
  double abs_mv_in_out_accumulator = 0.0;
  int arf_boost;
  int flash_detected = 0;

  // Search forward from the proposed arf/next gf position.
  for (i = 0; i < f_frames; ++i) {
    const FIRSTPASS_STATS *this_frame = read_frame_stats(twopass, i);
    if (this_frame == NULL) break;

    // Update the motion related elements to the boost calculation.
    accumulate_frame_motion_stats(
        this_frame, &this_frame_mv_in_out, &mv_in_out_accumulator,
        &abs_mv_in_out_accumulator, &mv_ratio_accumulator);

    // We want to discount the flash frame itself and the recovery
    // frame that follows as both will have poor scores.
    flash_detected = detect_flash(twopass, i) || detect_flash(twopass, i + 1);

    // Accumulate the effect of prediction quality decay.
    if (!flash_detected) {
      decay_accumulator *= get_prediction_decay_rate(cpi, this_frame);
      decay_accumulator = decay_accumulator < MIN_DECAY_FACTOR
                              ? MIN_DECAY_FACTOR
                              : decay_accumulator;
    }
    boost_score += decay_accumulator *
                   calc_frame_boost(cpi, this_frame, this_frame_mv_in_out);
  }

  arf_boost = (int)boost_score;

  // Reset for backward looking loop.
  boost_score = 0.0;
  mv_ratio_accumulator = 0.0;
  decay_accumulator = 1.0;
  this_frame_mv_in_out = 0.0;
  mv_in_out_accumulator = 0.0;
  abs_mv_in_out_accumulator = 0.0;

  // Search backward towards last gf position.
  for (i = -1; i >= -b_frames; --i) {
    const FIRSTPASS_STATS *this_frame = read_frame_stats(twopass, i);
    if (this_frame == NULL) break;

    // Update the motion related elements to the boost calculation.
    accumulate_frame_motion_stats(
        this_frame, &this_frame_mv_in_out, &mv_in_out_accumulator,
        &abs_mv_in_out_accumulator, &mv_ratio_accumulator);

    // We want to discount the the flash frame itself and the recovery
    // frame that follows as both will have poor scores.
    flash_detected = detect_flash(twopass, i) || detect_flash(twopass, i + 1);

    // Cumulative effect of prediction quality decay.
    if (!flash_detected) {
      decay_accumulator *= get_prediction_decay_rate(cpi, this_frame);
      decay_accumulator = decay_accumulator < MIN_DECAY_FACTOR
                              ? MIN_DECAY_FACTOR
                              : decay_accumulator;
    }
    boost_score += decay_accumulator *
                   calc_frame_boost(cpi, this_frame, this_frame_mv_in_out);
  }
  arf_boost += (int)boost_score;

  if (arf_boost < ((b_frames + f_frames) * 40))
    arf_boost = ((b_frames + f_frames) * 40);
  arf_boost = VPXMAX(arf_boost, MIN_ARF_GF_BOOST);

  return arf_boost;
}

// Calculate a section intra ratio used in setting max loop filter.
static int calculate_section_intra_ratio(const FIRSTPASS_STATS *begin,
                                         const FIRSTPASS_STATS *end,
                                         int section_length) {
  const FIRSTPASS_STATS *s = begin;
  double intra_error = 0.0;
  double coded_error = 0.0;
  int i = 0;

  while (s < end && i < section_length) {
    intra_error += s->intra_error;
    coded_error += s->coded_error;
    ++s;
    ++i;
  }

  return (int)(intra_error / DOUBLE_DIVIDE_CHECK(coded_error));
}

// Calculate the total bits to allocate in this GF/ARF group.
static int64_t calculate_total_gf_group_bits(VP9_COMP *cpi,
                                             double gf_group_err) {
  const RATE_CONTROL *const rc = &cpi->rc;
  const TWO_PASS *const twopass = &cpi->twopass;
  const int max_bits = frame_max_bits(rc, &cpi->oxcf);
  int64_t total_group_bits;

  // Calculate the bits to be allocated to the group as a whole.
  if ((twopass->kf_group_bits > 0) && (twopass->kf_group_error_left > 0.0)) {
    total_group_bits = (int64_t)(twopass->kf_group_bits *
                                 (gf_group_err / twopass->kf_group_error_left));
  } else {
    total_group_bits = 0;
  }

  // Clamp odd edge cases.
  total_group_bits = (total_group_bits < 0)
                         ? 0
                         : (total_group_bits > twopass->kf_group_bits)
                               ? twopass->kf_group_bits
                               : total_group_bits;

  // Clip based on user supplied data rate variability limit.
  if (total_group_bits > (int64_t)max_bits * rc->baseline_gf_interval)
    total_group_bits = (int64_t)max_bits * rc->baseline_gf_interval;

  return total_group_bits;
}

// Calculate the number bits extra to assign to boosted frames in a group.
static int calculate_boost_bits(int frame_count, int boost,
                                int64_t total_group_bits) {
  int allocation_chunks;

  // return 0 for invalid inputs (could arise e.g. through rounding errors)
  if (!boost || (total_group_bits <= 0) || (frame_count < 0)) return 0;

  allocation_chunks = (frame_count * 100) + boost;

  // Prevent overflow.
  if (boost > 1023) {
    int divisor = boost >> 10;
    boost /= divisor;
    allocation_chunks /= divisor;
  }

  // Calculate the number of extra bits for use in the boosted frame or frames.
  return VPXMAX((int)(((int64_t)boost * total_group_bits) / allocation_chunks),
                0);
}

// Current limit on maximum number of active arfs in a GF/ARF group.
#define MAX_ACTIVE_ARFS 2
#define ARF_SLOT1 2
#define ARF_SLOT2 3
// This function indirects the choice of buffers for arfs.
// At the moment the values are fixed but this may change as part of
// the integration process with other codec features that swap buffers around.
static void get_arf_buffer_indices(unsigned char *arf_buffer_indices) {
  arf_buffer_indices[0] = ARF_SLOT1;
  arf_buffer_indices[1] = ARF_SLOT2;
}

// Used in corpus vbr: Calculates the total normalized group complexity score
// for a given number of frames starting at the current position in the stats
// file.
static double calculate_group_score(VP9_COMP *cpi, double av_score,
                                    int frame_count) {
  VP9EncoderConfig *const oxcf = &cpi->oxcf;
  TWO_PASS *const twopass = &cpi->twopass;
  const FIRSTPASS_STATS *s = twopass->stats_in;
  double score_total = 0.0;
  int i = 0;

  // We dont ever want to return a 0 score here.
  if (frame_count == 0) return 1.0;

  while ((i < frame_count) && (s < twopass->stats_in_end)) {
    score_total += calculate_norm_frame_score(cpi, twopass, oxcf, s, av_score);
    ++s;
    ++i;
  }
  assert(i == frame_count);

  return score_total;
}

static void define_gf_multi_arf_structure(VP9_COMP *cpi) {
  RATE_CONTROL *const rc = &cpi->rc;
  TWO_PASS *const twopass = &cpi->twopass;
  GF_GROUP *const gf_group = &twopass->gf_group;
  int i;
  int frame_index = 0;
  const int key_frame = cpi->common.frame_type == KEY_FRAME;

  // The use of bi-predictive frames are only enabled when following 3
  // conditions are met:
  // (1) ALTREF is enabled;
  // (2) The bi-predictive group interval is at least 2; and
  // (3) The bi-predictive group interval is strictly smaller than the
  //     golden group interval.
  const int is_bipred_enabled =
      cpi->extra_arf_allowed && rc->source_alt_ref_pending &&
      rc->bipred_group_interval &&
      rc->bipred_group_interval <=
          (rc->baseline_gf_interval - rc->source_alt_ref_pending);
  int bipred_group_end = 0;
  int bipred_frame_index = 0;

  const unsigned char ext_arf_interval =
      (unsigned char)(rc->baseline_gf_interval / (cpi->num_extra_arfs + 1) - 1);
  int which_arf = cpi->num_extra_arfs;
  int subgroup_interval[MAX_EXT_ARFS + 1];
  int is_sg_bipred_enabled = is_bipred_enabled;
  int accumulative_subgroup_interval = 0;

  // For key frames the frame target rate is already set and it
  // is also the golden frame.
  // === [frame_index == 0] ===
  if (!key_frame) {
    if (rc->source_alt_ref_active) {
      gf_group->update_type[frame_index] = OVERLAY_UPDATE;
      gf_group->rf_level[frame_index] = INTER_NORMAL;
    } else {
      gf_group->update_type[frame_index] = GF_UPDATE;
      gf_group->rf_level[frame_index] = GF_ARF_STD;
    }
    gf_group->arf_update_idx[frame_index] = 0;
    gf_group->arf_ref_idx[frame_index] = 0;
  }

  gf_group->bidir_pred_enabled[frame_index] = 0;
  gf_group->brf_src_offset[frame_index] = 0;

  frame_index++;

  bipred_frame_index++;

  // === [frame_index == 1] ===
  if (rc->source_alt_ref_pending) {
    gf_group->update_type[frame_index] = ARF_UPDATE;
    gf_group->rf_level[frame_index] = GF_ARF_STD;
    gf_group->arf_src_offset[frame_index] =
        (unsigned char)(rc->baseline_gf_interval - 1);

    gf_group->arf_update_idx[frame_index] = 0;
    gf_group->arf_ref_idx[frame_index] = 0;

    gf_group->bidir_pred_enabled[frame_index] = 0;
    gf_group->brf_src_offset[frame_index] = 0;
    // NOTE: "bidir_pred_frame_index" stays unchanged for ARF_UPDATE frames.

    // Work out the ARFs' positions in this gf group
    // NOTE: ALT_REFs' are indexed inversely, but coded in display order
    // (except for the original ARF). In the example of three ALT_REF's,
    // We index ALTREF's as: KEY ----- ALT2 ----- ALT1 ----- ALT0
    // but code them in the following order:
    // KEY-ALT0-ALT2 ----- OVERLAY2-ALT1 ----- OVERLAY1 ----- OVERLAY0
    //
    // arf_pos_for_ovrly[]: Position for OVERLAY
    // arf_pos_in_gf[]:     Position for ALTREF
    cpi->arf_pos_for_ovrly[0] = frame_index + cpi->num_extra_arfs +
                                gf_group->arf_src_offset[frame_index] + 1;
    for (i = 0; i < cpi->num_extra_arfs; ++i) {
      cpi->arf_pos_for_ovrly[i + 1] =
          frame_index + (cpi->num_extra_arfs - i) * (ext_arf_interval + 2);
      subgroup_interval[i] = cpi->arf_pos_for_ovrly[i] -
                             cpi->arf_pos_for_ovrly[i + 1] - (i == 0 ? 1 : 2);
    }
    subgroup_interval[cpi->num_extra_arfs] =
        cpi->arf_pos_for_ovrly[cpi->num_extra_arfs] - frame_index -
        (cpi->num_extra_arfs == 0 ? 1 : 2);

    ++frame_index;

    // Insert an extra ARF
    // === [frame_index == 2] ===
    if (cpi->num_extra_arfs) {
      gf_group->update_type[frame_index] = INTNL_ARF_UPDATE;
      gf_group->rf_level[frame_index] = GF_ARF_LOW;
      gf_group->arf_src_offset[frame_index] = ext_arf_interval;

      gf_group->arf_update_idx[frame_index] = which_arf;
      gf_group->arf_ref_idx[frame_index] = 0;
      ++frame_index;
    }
    accumulative_subgroup_interval += subgroup_interval[cpi->num_extra_arfs];
  }

  for (i = 0; i < rc->baseline_gf_interval - rc->source_alt_ref_pending; ++i) {
    gf_group->arf_update_idx[frame_index] = which_arf;
    gf_group->arf_ref_idx[frame_index] = which_arf;

    // If we are going to have ARFs, check whether we can have BWDREF in this
    // subgroup, and further, whether we can have ARF subgroup which contains
    // the BWDREF subgroup but contained within the GF group:
    //
    // GF group --> ARF subgroup --> BWDREF subgroup
    if (rc->source_alt_ref_pending) {
      is_sg_bipred_enabled =
          is_bipred_enabled &&
          (subgroup_interval[which_arf] > rc->bipred_group_interval);
    }

    // NOTE: 1. BIDIR_PRED is only enabled when the length of the bi-predictive
    //       frame group interval is strictly smaller than that of the GOLDEN
    //       FRAME group interval.
    //       2. Currently BIDIR_PRED is only enabled when alt-ref is on.
    if (is_sg_bipred_enabled && !bipred_group_end) {
      const int cur_brf_src_offset = rc->bipred_group_interval - 1;

      if (bipred_frame_index == 1) {
        // --- BRF_UPDATE ---
        gf_group->update_type[frame_index] = BRF_UPDATE;
        gf_group->rf_level[frame_index] = GF_ARF_LOW;
        gf_group->brf_src_offset[frame_index] = cur_brf_src_offset;
      } else if (bipred_frame_index == rc->bipred_group_interval) {
        // --- LAST_BIPRED_UPDATE ---
        gf_group->update_type[frame_index] = LAST_BIPRED_UPDATE;
        gf_group->rf_level[frame_index] = INTER_NORMAL;
        gf_group->brf_src_offset[frame_index] = 0;

        // Reset the bi-predictive frame index.
        bipred_frame_index = 0;
      } else {
        // --- BIPRED_UPDATE ---
        gf_group->update_type[frame_index] = BIPRED_UPDATE;
        gf_group->rf_level[frame_index] = INTER_NORMAL;
        gf_group->brf_src_offset[frame_index] = 0;
      }
      gf_group->bidir_pred_enabled[frame_index] = 1;

      bipred_frame_index++;
      // Check whether the next bi-predictive frame group would entirely be
      // included within the current golden frame group.
      // In addition, we need to avoid coding a BRF right before an ARF.
      if (bipred_frame_index == 1 &&
          (i + 2 + cur_brf_src_offset) >= accumulative_subgroup_interval) {
        bipred_group_end = 1;
      }
    } else {
      gf_group->update_type[frame_index] = LF_UPDATE;
      gf_group->rf_level[frame_index] = INTER_NORMAL;
      gf_group->bidir_pred_enabled[frame_index] = 0;
      gf_group->brf_src_offset[frame_index] = 0;
    }

    ++frame_index;

    // Check if we need to update the ARF.
    if (is_sg_bipred_enabled && cpi->num_extra_arfs && which_arf > 0 &&
        frame_index > cpi->arf_pos_for_ovrly[which_arf]) {
      --which_arf;
      accumulative_subgroup_interval += subgroup_interval[which_arf] + 1;

      // Meet the new subgroup; Reset the bipred_group_end flag.
      bipred_group_end = 0;
      // Insert another extra ARF after the overlay frame
      if (which_arf) {
        gf_group->update_type[frame_index] = INTNL_ARF_UPDATE;
        gf_group->rf_level[frame_index] = GF_ARF_LOW;
        gf_group->arf_src_offset[frame_index] = ext_arf_interval;

        gf_group->arf_update_idx[frame_index] = which_arf;
        gf_group->arf_ref_idx[frame_index] = 0;
        ++frame_index;
      }
    }
  }

  // NOTE: We need to configure the frame at the end of the sequence + 1 that
  //       is the start frame for the next group. Otherwise prior to the call to
  //       av1_rc_get_second_pass_params() the data will be undefined.
  gf_group->arf_update_idx[frame_index] = 0;
  gf_group->arf_ref_idx[frame_index] = 0;

  if (rc->source_alt_ref_pending) {
    gf_group->update_type[frame_index] = OVERLAY_UPDATE;
    gf_group->rf_level[frame_index] = INTER_NORMAL;

    cpi->arf_pos_in_gf[0] = 1;
    if (cpi->num_extra_arfs) {
      // Overwrite the update_type for extra-ARF's corresponding internal
      // OVERLAY's: Change from LF_UPDATE to INTNL_OVERLAY_UPDATE.
      for (i = cpi->num_extra_arfs; i > 0; --i) {
        cpi->arf_pos_in_gf[i] =
            (i == cpi->num_extra_arfs ? 2 : cpi->arf_pos_for_ovrly[i + 1] + 1);

        gf_group->update_type[cpi->arf_pos_for_ovrly[i]] = INTNL_OVERLAY_UPDATE;
        gf_group->rf_level[cpi->arf_pos_for_ovrly[i]] = INTER_NORMAL;
      }
    }
  } else {
    gf_group->update_type[frame_index] = GF_UPDATE;
    gf_group->rf_level[frame_index] = GF_ARF_STD;
  }

  gf_group->bidir_pred_enabled[frame_index] = 0;
  gf_group->brf_src_offset[frame_index] = 0;
}

static void define_gf_group_structure(VP9_COMP *cpi) {
  RATE_CONTROL *const rc = &cpi->rc;
  TWO_PASS *const twopass = &cpi->twopass;
  GF_GROUP *const gf_group = &twopass->gf_group;
  int i;
  int frame_index = 0;
  int key_frame;
  int mid_frame_idx;
  unsigned char arf_buffer_indices[MAX_ACTIVE_ARFS];
  int normal_frames;

  key_frame = cpi->common.frame_type == KEY_FRAME;

  get_arf_buffer_indices(arf_buffer_indices);

  // For key frames the frame target rate is already set and it
  // is also the golden frame.
  // === [frame_index == 0] ===
  if (!key_frame) {
    if (rc->source_alt_ref_active) {
      gf_group->update_type[frame_index] = OVERLAY_UPDATE;
      gf_group->rf_level[frame_index] = INTER_NORMAL;
    } else {
      gf_group->update_type[frame_index] = GF_UPDATE;
      gf_group->rf_level[frame_index] = GF_ARF_STD;
    }
    gf_group->arf_update_idx[frame_index] = arf_buffer_indices[0];
    gf_group->arf_ref_idx[frame_index] = arf_buffer_indices[0];
  }

  ++frame_index;

  // === [frame_index == 1] ===
  if (rc->source_alt_ref_pending) {
    gf_group->update_type[frame_index] = ARF_UPDATE;
    gf_group->rf_level[frame_index] = GF_ARF_STD;

    gf_group->arf_src_offset[frame_index] =
        (unsigned char)(rc->baseline_gf_interval - 1);

    gf_group->arf_update_idx[frame_index] = arf_buffer_indices[0];
    gf_group->arf_ref_idx[frame_index] =
        arf_buffer_indices[cpi->multi_arf_last_grp_enabled &&
                           rc->source_alt_ref_active];
    ++frame_index;

    if (cpi->multi_arf_enabled) {
      // Set aside a slot for a level 1 arf.
      gf_group->update_type[frame_index] = ARF_UPDATE;
      gf_group->rf_level[frame_index] = GF_ARF_LOW;
      gf_group->arf_src_offset[frame_index] =
          (unsigned char)((rc->baseline_gf_interval >> 1) - 1);
      gf_group->arf_update_idx[frame_index] = arf_buffer_indices[1];
      gf_group->arf_ref_idx[frame_index] = arf_buffer_indices[0];
      ++frame_index;
    }
  }

  // Note index of the first normal inter frame int eh group (not gf kf arf)
  gf_group->first_inter_index = frame_index;

  // Define middle frame
  mid_frame_idx = frame_index + (rc->baseline_gf_interval >> 1) - 1;

  normal_frames = (rc->baseline_gf_interval - rc->source_alt_ref_pending);
  for (i = 0; i < normal_frames; ++i) {
    int arf_idx = 0;
    if (twopass->stats_in >= twopass->stats_in_end) break;

    if (rc->source_alt_ref_pending && cpi->multi_arf_enabled) {
      if (frame_index <= mid_frame_idx) arf_idx = 1;
    }

    gf_group->arf_update_idx[frame_index] = arf_buffer_indices[arf_idx];
    gf_group->arf_ref_idx[frame_index] = arf_buffer_indices[arf_idx];

    gf_group->update_type[frame_index] = LF_UPDATE;
    gf_group->rf_level[frame_index] = INTER_NORMAL;

    ++frame_index;
  }

  // Note:
  // We need to configure the frame at the end of the sequence + 1 that will be
  // the start frame for the next group. Otherwise prior to the call to
  // vp9_rc_get_second_pass_params() the data will be undefined.
  gf_group->arf_update_idx[frame_index] = arf_buffer_indices[0];
  gf_group->arf_ref_idx[frame_index] = arf_buffer_indices[0];

  if (rc->source_alt_ref_pending) {
    gf_group->update_type[frame_index] = OVERLAY_UPDATE;
    gf_group->rf_level[frame_index] = INTER_NORMAL;

    // Final setup for second arf and its overlay.
    if (cpi->multi_arf_enabled)
      gf_group->update_type[mid_frame_idx] = OVERLAY_UPDATE;
  } else {
    gf_group->update_type[frame_index] = GF_UPDATE;
    gf_group->rf_level[frame_index] = GF_ARF_STD;
  }

  // Note whether multi-arf was enabled this group for next time.
  cpi->multi_arf_last_grp_enabled = cpi->multi_arf_enabled;
}

static void allocate_gf_multi_arf_bits(VP9_COMP *cpi, int64_t gf_group_bits,
                                       int gf_arf_bits) {
  VP9EncoderConfig *const oxcf = &cpi->oxcf;
  RATE_CONTROL *const rc = &cpi->rc;
  TWO_PASS *const twopass = &cpi->twopass;
  GF_GROUP *const gf_group = &twopass->gf_group;
  FIRSTPASS_STATS frame_stats;
  int i;
  int frame_index = 0;
  int target_frame_size;
  int key_frame;
  const int max_bits = frame_max_bits(&cpi->rc, oxcf);
  int64_t total_group_bits = gf_group_bits;
  int normal_frames;
  int normal_frame_bits;
  int last_frame_reduction = 0;
  double av_score = 1.0;
  double tot_norm_frame_score = 1.0;
  double this_frame_score = 1.0;

  // Define the GF structure and specify
  define_gf_multi_arf_structure(cpi);

  //========================================

  key_frame = cpi->common.frame_type == KEY_FRAME;

  // For key frames the frame target rate is already set and it
  // is also the golden frame.
  // === [frame_index == 0] ===
  if (!key_frame) {
    gf_group->bit_allocation[frame_index] =
        rc->source_alt_ref_active ? 0 : gf_arf_bits;
  }

  // Deduct the boost bits for arf (or gf if it is not a key frame)
  // from the group total.
  if (rc->source_alt_ref_pending || !key_frame) total_group_bits -= gf_arf_bits;

  ++frame_index;

  // === [frame_index == 1] ===
  // Store the bits to spend on the ARF if there is one.
  if (rc->source_alt_ref_pending) {
    gf_group->bit_allocation[frame_index] = gf_arf_bits;

    ++frame_index;

    // Skip all the extra-ARF's right after ARF at the starting segment of
    // the current GF group.
    if (cpi->num_extra_arfs) {
      while (gf_group->update_type[frame_index] == INTNL_ARF_UPDATE)
        ++frame_index;
    }
  }

  normal_frames = (rc->baseline_gf_interval - rc->source_alt_ref_pending);
  if (normal_frames > 1)
    normal_frame_bits = (int)(total_group_bits / normal_frames);
  else
    normal_frame_bits = (int)total_group_bits;

  if (oxcf->vbr_corpus_complexity) {
    av_score = get_distribution_av_err(cpi, twopass);
    tot_norm_frame_score = calculate_group_score(cpi, av_score, normal_frames);
  }

  // Allocate bits to the other frames in the group.
  for (i = 0; i < normal_frames; ++i) {
    if (EOF == input_stats(twopass, &frame_stats)) break;

    if (oxcf->vbr_corpus_complexity) {
      this_frame_score = calculate_norm_frame_score(cpi, twopass, oxcf,
                                                    &frame_stats, av_score);
      normal_frame_bits = (int)((double)total_group_bits *
                                (this_frame_score / tot_norm_frame_score));
    }

    target_frame_size = normal_frame_bits;
    if ((i == (normal_frames - 1)) && (i >= 1)) {
      last_frame_reduction = normal_frame_bits / 16;
      target_frame_size -= last_frame_reduction;
    }

    // TODO(zoeliu): Further check whether following is needed for
    //               hierarchical GF group structure.
    if (rc->source_alt_ref_pending && cpi->multi_arf_enabled) {
      target_frame_size -= (target_frame_size >> 4);
    }

    target_frame_size =
        clamp(target_frame_size, 0, VPXMIN(max_bits, (int)total_group_bits));

    if (gf_group->update_type[frame_index] == BRF_UPDATE) {
      // Boost up the allocated bits on BWDREF_FRAME
      gf_group->bit_allocation[frame_index] =
          target_frame_size + (target_frame_size >> 2);
    } else if (gf_group->update_type[frame_index] == LAST_BIPRED_UPDATE) {
      // Press down the allocated bits on LAST_BIPRED_UPDATE frames
      gf_group->bit_allocation[frame_index] =
          target_frame_size - (target_frame_size >> 1);
    } else if (gf_group->update_type[frame_index] == BIPRED_UPDATE) {
      // TODO(zoeliu): Investigate whether the allocated bits on BIPRED_UPDATE
      //               frames need to be further adjusted.
      gf_group->bit_allocation[frame_index] = target_frame_size;
    } else {
      assert(gf_group->update_type[frame_index] == LF_UPDATE ||
             gf_group->update_type[frame_index] == INTNL_OVERLAY_UPDATE);
      gf_group->bit_allocation[frame_index] = target_frame_size;
    }

    ++frame_index;

    // Skip all the extra-ARF's.
    if (cpi->num_extra_arfs) {
      while (gf_group->update_type[frame_index] == INTNL_ARF_UPDATE)
        ++frame_index;
    }
  }

  // NOTE: We need to configure the frame at the end of the sequence + 1 that
  //       will be the start frame for the next group. Otherwise prior to the
  //       call to av1_rc_get_second_pass_params() the data will be undefined.
  if (rc->source_alt_ref_pending) {
    if (cpi->num_extra_arfs) {
      // NOTE: For bit allocation, move the allocated bits associated with
      //       INTNL_OVERLAY_UPDATE to the corresponding INTNL_ARF_UPDATE.
      //       i > 0 for extra-ARF's and i == 0 for ARF:
      //         arf_pos_for_ovrly[i]: Position for INTNL_OVERLAY_UPDATE
      //         arf_pos_in_gf[i]: Position for INTNL_ARF_UPDATE
      for (i = cpi->num_extra_arfs; i > 0; --i) {
        assert(gf_group->update_type[cpi->arf_pos_for_ovrly[i]] ==
               INTNL_OVERLAY_UPDATE);

        // Encoder's choice:
        //   Set show_existing_frame == 1 for all extra-ARF's, and hence
        //   allocate zero bit for both all internal OVERLAY frames.
        gf_group->bit_allocation[cpi->arf_pos_in_gf[i]] =
            gf_group->bit_allocation[cpi->arf_pos_for_ovrly[i]];
        gf_group->bit_allocation[cpi->arf_pos_for_ovrly[i]] = 0;
      }
    }
  }
}

static void allocate_gf_group_bits(VP9_COMP *cpi, int64_t gf_group_bits,
                                   int gf_arf_bits) {
  VP9EncoderConfig *const oxcf = &cpi->oxcf;
  RATE_CONTROL *const rc = &cpi->rc;
  TWO_PASS *const twopass = &cpi->twopass;
  GF_GROUP *const gf_group = &twopass->gf_group;
  FIRSTPASS_STATS frame_stats;
  int i;
  int frame_index = 0;
  int target_frame_size;
  int key_frame;
  const int max_bits = frame_max_bits(&cpi->rc, oxcf);
  int64_t total_group_bits = gf_group_bits;
  int mid_boost_bits = 0;
  int mid_frame_idx;
  int normal_frames;
  int normal_frame_bits;
  int last_frame_reduction = 0;
  double av_score = 1.0;
  double tot_norm_frame_score = 1.0;
  double this_frame_score = 1.0;

  // Define the GF structure and specify
  define_gf_group_structure(cpi);

  key_frame = cpi->common.frame_type == KEY_FRAME;

  // For key frames the frame target rate is already set and it
  // is also the golden frame.
  // === [frame_index == 0] ===
  if (!key_frame) {
    gf_group->bit_allocation[frame_index] =
        rc->source_alt_ref_active ? 0 : gf_arf_bits;
  }

  // Deduct the boost bits for arf (or gf if it is not a key frame)
  // from the group total.
  if (rc->source_alt_ref_pending || !key_frame) total_group_bits -= gf_arf_bits;

  ++frame_index;

  // === [frame_index == 1] ===
  // Store the bits to spend on the ARF if there is one.
  if (rc->source_alt_ref_pending) {
    gf_group->bit_allocation[frame_index] = gf_arf_bits;

    ++frame_index;

    // Set aside a slot for a level 1 arf.
    if (cpi->multi_arf_enabled) ++frame_index;
  }

  // Define middle frame
  mid_frame_idx = frame_index + (rc->baseline_gf_interval >> 1) - 1;

  normal_frames = (rc->baseline_gf_interval - rc->source_alt_ref_pending);
  if (normal_frames > 1)
    normal_frame_bits = (int)(total_group_bits / normal_frames);
  else
    normal_frame_bits = (int)total_group_bits;

  if (oxcf->vbr_corpus_complexity) {
    av_score = get_distribution_av_err(cpi, twopass);
    tot_norm_frame_score = calculate_group_score(cpi, av_score, normal_frames);
  }

  // Allocate bits to the other frames in the group.
  for (i = 0; i < normal_frames; ++i) {
    if (EOF == input_stats(twopass, &frame_stats)) break;
    if (oxcf->vbr_corpus_complexity) {
      this_frame_score = calculate_norm_frame_score(cpi, twopass, oxcf,
                                                    &frame_stats, av_score);
      normal_frame_bits = (int)((double)total_group_bits *
                                (this_frame_score / tot_norm_frame_score));
    }

    target_frame_size = normal_frame_bits;
    if ((i == (normal_frames - 1)) && (i >= 1)) {
      last_frame_reduction = normal_frame_bits / 16;
      target_frame_size -= last_frame_reduction;
    }

    if (rc->source_alt_ref_pending && cpi->multi_arf_enabled) {
      mid_boost_bits += (target_frame_size >> 4);
      target_frame_size -= (target_frame_size >> 4);
    }

    target_frame_size =
        clamp(target_frame_size, 0, VPXMIN(max_bits, (int)total_group_bits));

    gf_group->bit_allocation[frame_index] = target_frame_size;
    ++frame_index;
  }

  // Add in some extra bits for the middle frame in the group.
  gf_group->bit_allocation[mid_frame_idx] += last_frame_reduction;

  // Note:
  // We need to configure the frame at the end of the sequence + 1 that will be
  // the start frame for the next group. Otherwise prior to the call to
  // vp9_rc_get_second_pass_params() the data will be undefined.

  if (rc->source_alt_ref_pending) {
    // Final setup for second arf and its overlay.
    if (cpi->multi_arf_enabled) {
      gf_group->bit_allocation[2] =
          gf_group->bit_allocation[mid_frame_idx] + mid_boost_bits;
      gf_group->bit_allocation[mid_frame_idx] = 0;
    }
  }
}

// Adjusts the ARNF filter for a GF group.
static void adjust_group_arnr_filter(VP9_COMP *cpi, double section_noise,
                                     double section_inter,
                                     double section_motion) {
  TWO_PASS *const twopass = &cpi->twopass;
  double section_zeromv = section_inter - section_motion;

  twopass->arnr_strength_adjustment = 0;

  if ((section_zeromv < 0.10) || (section_noise <= (SECTION_NOISE_DEF * 0.75)))
    twopass->arnr_strength_adjustment -= 1;
  if (section_zeromv > 0.50) twopass->arnr_strength_adjustment += 1;
}

// Analyse and define a gf/arf group.
#define ARF_DECAY_BREAKOUT 0.10
#define ARF_ABS_ZOOM_THRESH 4.0

static void define_gf_group(VP9_COMP *cpi, FIRSTPASS_STATS *this_frame) {
  VP9_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;
  VP9EncoderConfig *const oxcf = &cpi->oxcf;
  TWO_PASS *const twopass = &cpi->twopass;
  FIRSTPASS_STATS next_frame;
  const FIRSTPASS_STATS *const start_pos = twopass->stats_in;
  int i;

  double gf_group_err = 0.0;
  double gf_group_raw_error = 0.0;
  double gf_group_noise = 0.0;
  double gf_group_skip_pct = 0.0;
  double gf_group_inactive_zone_rows = 0.0;
  double gf_group_inter = 0.0;
  double gf_group_motion = 0.0;
  double gf_first_frame_err = 0.0;
  double mod_frame_err = 0.0;

  double mv_ratio_accumulator = 0.0;
  double zero_motion_accumulator = 1.0;
  double loop_decay_rate = 1.00;
  double last_loop_decay_rate = 1.00;

  double this_frame_mv_in_out = 0.0;
  double mv_in_out_accumulator = 0.0;
  double abs_mv_in_out_accumulator = 0.0;
  double mv_ratio_accumulator_thresh;
  double abs_mv_in_out_thresh;
  double sr_accumulator = 0.0;
  const double av_err = get_distribution_av_err(cpi, twopass);
  unsigned int allow_alt_ref = is_altref_enabled(cpi);

  int flash_detected;
  int active_max_gf_interval;
  int active_min_gf_interval;
  int64_t gf_group_bits;
  int gf_arf_bits;
  const int is_key_frame = frame_is_intra_only(cm);
  const int arf_active_or_kf = is_key_frame || rc->source_alt_ref_active;

  int disable_bwd_extarf;

  // Reset the GF group data structures unless this is a key
  // frame in which case it will already have been done.
  if (is_key_frame == 0) {
    vp9_zero(twopass->gf_group);
  }

  vpx_clear_system_state();
  vp9_zero(next_frame);

  // Load stats for the current frame.
  mod_frame_err =
      calculate_norm_frame_score(cpi, twopass, oxcf, this_frame, av_err);

  // Note the error of the frame at the start of the group. This will be
  // the GF frame error if we code a normal gf.
  gf_first_frame_err = mod_frame_err;

  // If this is a key frame or the overlay from a previous arf then
  // the error score / cost of this frame has already been accounted for.
  if (arf_active_or_kf) {
    gf_group_err -= gf_first_frame_err;
    gf_group_raw_error -= this_frame->coded_error;
    gf_group_noise -= this_frame->frame_noise_energy;
    gf_group_skip_pct -= this_frame->intra_skip_pct;
    gf_group_inactive_zone_rows -= this_frame->inactive_zone_rows;
    gf_group_inter -= this_frame->pcnt_inter;
    gf_group_motion -= this_frame->pcnt_motion;
  }

  // Motion breakout threshold for loop below depends on image size.
  mv_ratio_accumulator_thresh =
      (cpi->initial_height + cpi->initial_width) / 4.0;
  abs_mv_in_out_thresh = ARF_ABS_ZOOM_THRESH;

  // Set a maximum and minimum interval for the GF group.
  // If the image appears almost completely static we can extend beyond this.
  {
    int int_max_q = (int)(vp9_convert_qindex_to_q(twopass->active_worst_quality,
                                                  cpi->common.bit_depth));
    int int_lbq = (int)(vp9_convert_qindex_to_q(rc->last_boosted_qindex,
                                                cpi->common.bit_depth));
    active_min_gf_interval =
        rc->min_gf_interval + arf_active_or_kf + VPXMIN(2, int_max_q / 200);
    active_min_gf_interval =
        VPXMIN(active_min_gf_interval, rc->max_gf_interval + arf_active_or_kf);

    if (cpi->multi_arf_allowed) {
      active_max_gf_interval = rc->max_gf_interval;
    } else {
      // The value chosen depends on the active Q range. At low Q we have
      // bits to spare and are better with a smaller interval and smaller boost.
      // At high Q when there are few bits to spare we are better with a longer
      // interval to spread the cost of the GF.
      active_max_gf_interval = 12 + arf_active_or_kf + VPXMIN(4, (int_lbq / 6));

      // We have: active_min_gf_interval <=
      // rc->max_gf_interval + arf_active_or_kf.
      if (active_max_gf_interval < active_min_gf_interval) {
        active_max_gf_interval = active_min_gf_interval;
      } else {
        active_max_gf_interval = VPXMIN(active_max_gf_interval,
                                        rc->max_gf_interval + arf_active_or_kf);
      }

      // Would the active max drop us out just before the near the next kf?
      if ((active_max_gf_interval <= rc->frames_to_key) &&
          (active_max_gf_interval >= (rc->frames_to_key - rc->min_gf_interval)))
        active_max_gf_interval = rc->frames_to_key / 2;
    }
  }

  i = 0;
  while (i < rc->static_scene_max_gf_interval && i < rc->frames_to_key) {
    ++i;

    // Accumulate error score of frames in this gf group.
    mod_frame_err =
        calculate_norm_frame_score(cpi, twopass, oxcf, this_frame, av_err);
    gf_group_err += mod_frame_err;
    gf_group_raw_error += this_frame->coded_error;
    gf_group_noise += this_frame->frame_noise_energy;
    gf_group_skip_pct += this_frame->intra_skip_pct;
    gf_group_inactive_zone_rows += this_frame->inactive_zone_rows;
    gf_group_inter += this_frame->pcnt_inter;
    gf_group_motion += this_frame->pcnt_motion;

    if (EOF == input_stats(twopass, &next_frame)) break;

    // Test for the case where there is a brief flash but the prediction
    // quality back to an earlier frame is then restored.
    flash_detected = detect_flash(twopass, 0);

    // Update the motion related elements to the boost calculation.
    accumulate_frame_motion_stats(
        &next_frame, &this_frame_mv_in_out, &mv_in_out_accumulator,
        &abs_mv_in_out_accumulator, &mv_ratio_accumulator);

    // Accumulate the effect of prediction quality decay.
    if (!flash_detected) {
      last_loop_decay_rate = loop_decay_rate;
      loop_decay_rate = get_prediction_decay_rate(cpi, &next_frame);

      // Monitor for static sections.
      zero_motion_accumulator = VPXMIN(
          zero_motion_accumulator, get_zero_motion_factor(cpi, &next_frame));

      // Break clause to detect very still sections after motion. For example,
      // a static image after a fade or other transition.
      if (detect_transition_to_still(cpi, i, 5, loop_decay_rate,
                                     last_loop_decay_rate)) {
        allow_alt_ref = 0;
        break;
      }

      // Update the accumulator for second ref error difference.
      // This is intended to give an indication of how much the coded error is
      // increasing over time.
      if (i == 1) {
        sr_accumulator += next_frame.coded_error;
      } else {
        sr_accumulator += (next_frame.sr_coded_error - next_frame.coded_error);
      }
    }

    // Break out conditions.
    if (
        // Break at active_max_gf_interval unless almost totally static.
        ((i >= active_max_gf_interval) && (zero_motion_accumulator < 0.995)) ||
        (
            // Don't break out with a very short interval.
            (i >= active_min_gf_interval) &&
            // If possible dont break very close to a kf
            ((rc->frames_to_key - i) >= rc->min_gf_interval) &&
            (!flash_detected) &&
            ((mv_ratio_accumulator > mv_ratio_accumulator_thresh) ||
             (abs_mv_in_out_accumulator > abs_mv_in_out_thresh) ||
             (sr_accumulator > next_frame.intra_error)))) {
      break;
    }

    *this_frame = next_frame;
  }

  // Was the group length constrained by the requirement for a new KF?
  rc->constrained_gf_group = (i >= rc->frames_to_key) ? 1 : 0;

  // Should we use the alternate reference frame.
  if (allow_alt_ref && (i < cpi->oxcf.lag_in_frames) &&
      (i >= rc->min_gf_interval)) {
    const int forward_frames = (rc->frames_to_key - i >= i - 1)
                                   ? i - 1
                                   : VPXMAX(0, rc->frames_to_key - i);

    // Calculate the boost for alt ref.
    rc->gfu_boost = calc_arf_boost(cpi, forward_frames, (i - 1));
    rc->source_alt_ref_pending = 1;

    // Test to see if multi arf is appropriate.
    cpi->multi_arf_enabled =
        (cpi->multi_arf_allowed && (rc->baseline_gf_interval >= 6) &&
         (zero_motion_accumulator < 0.995))
            ? 1
            : 0;
  } else {
    rc->gfu_boost = calc_arf_boost(cpi, 0, (i - 1));
    rc->source_alt_ref_pending = 0;
  }

#ifdef AGGRESSIVE_VBR
  // Limit maximum boost based on interval length.
  rc->gfu_boost = VPXMIN((int)rc->gfu_boost, i * 140);
#else
  rc->gfu_boost = VPXMIN((int)rc->gfu_boost, i * 200);
#endif

  // Set the interval until the next gf.
  rc->baseline_gf_interval = i - (is_key_frame || rc->source_alt_ref_pending);

  rc->frames_till_gf_update_due = rc->baseline_gf_interval;

  // TODO(zoeliu): Turn on the option to disable extra ALTREFs for still GF
  //               groups.
  // Disable extra altrefs for "still" gf group:
  //   zero_motion_accumulator: minimum percentage of (0,0) motion;
  //   avg_sr_coded_error:      average of the SSE per pixel of each frame;
  //   avg_raw_err_stdev:       average of the standard deviation of (0,0)
  //                            motion error per block of each frame.
#if 0
  assert(num_mbs > 0);
  disable_bwd_extarf =
      (zero_motion_accumulator > MIN_ZERO_MOTION &&
       avg_sr_coded_error / num_mbs < MAX_SR_CODED_ERROR &&
       avg_raw_err_stdev < MAX_RAW_ERR_VAR);
#else
  disable_bwd_extarf = 0;
#endif  // 0

  if (disable_bwd_extarf) cpi->extra_arf_allowed = 0;

  if (!cpi->extra_arf_allowed) {
    cpi->num_extra_arfs = 0;
  } else {
    // Compute how many extra alt_refs we can have
    cpi->num_extra_arfs = get_number_of_extra_arfs(rc->baseline_gf_interval,
                                                   rc->source_alt_ref_pending);
  }
  // Currently at maximum two extra ARFs' are allowed
  assert(cpi->num_extra_arfs <= MAX_EXT_ARFS);

  rc->bipred_group_interval = BFG_INTERVAL;
  // The minimum bi-predictive frame group interval is 2.
  if (rc->bipred_group_interval < 2) rc->bipred_group_interval = 0;

  // Reset the file position.
  reset_fpf_position(twopass, start_pos);

  // Calculate the bits to be allocated to the gf/arf group as a whole
  gf_group_bits = calculate_total_gf_group_bits(cpi, gf_group_err);

  // Calculate an estimate of the maxq needed for the group.
  // We are more aggressive about correcting for sections
  // where there could be significant overshoot than for easier
  // sections where we do not wish to risk creating an overshoot
  // of the allocated bit budget.
  if ((cpi->oxcf.rc_mode != VPX_Q) && (rc->baseline_gf_interval > 1)) {
    const int vbr_group_bits_per_frame =
        (int)(gf_group_bits / rc->baseline_gf_interval);
    const double group_av_err = gf_group_raw_error / rc->baseline_gf_interval;
    const double group_av_noise = gf_group_noise / rc->baseline_gf_interval;
    const double group_av_skip_pct =
        gf_group_skip_pct / rc->baseline_gf_interval;
    const double group_av_inactive_zone =
        ((gf_group_inactive_zone_rows * 2) /
         (rc->baseline_gf_interval * (double)cm->mb_rows));
    int tmp_q = get_twopass_worst_quality(
        cpi, group_av_err, (group_av_skip_pct + group_av_inactive_zone),
        group_av_noise, vbr_group_bits_per_frame);
    twopass->active_worst_quality =
        (tmp_q + (twopass->active_worst_quality * 3)) >> 2;

#if CONFIG_ALWAYS_ADJUST_BPM
    // Reset rolling actual and target bits counters for ARF groups.
    twopass->rolling_arf_group_target_bits = 0;
    twopass->rolling_arf_group_actual_bits = 0;
#endif
  }

  // Context Adjustment of ARNR filter strength
  if (rc->baseline_gf_interval > 1) {
    adjust_group_arnr_filter(cpi, (gf_group_noise / rc->baseline_gf_interval),
                             (gf_group_inter / rc->baseline_gf_interval),
                             (gf_group_motion / rc->baseline_gf_interval));
  } else {
    twopass->arnr_strength_adjustment = 0;
  }

  // Calculate the extra bits to be used for boosted frame(s)
  gf_arf_bits = calculate_boost_bits(rc->baseline_gf_interval, rc->gfu_boost,
                                     gf_group_bits);

  // Adjust KF group bits and error remaining.
  twopass->kf_group_error_left -= gf_group_err;

  // Allocate bits to each of the frames in the GF group.
  if (cpi->extra_arf_allowed) {
    allocate_gf_multi_arf_bits(cpi, gf_group_bits, gf_arf_bits);
  } else {
    allocate_gf_group_bits(cpi, gf_group_bits, gf_arf_bits);
  }

  // Reset the file position.
  reset_fpf_position(twopass, start_pos);

  // Calculate a section intra ratio used in setting max loop filter.
  if (cpi->common.frame_type != KEY_FRAME) {
    twopass->section_intra_rating = calculate_section_intra_ratio(
        start_pos, twopass->stats_in_end, rc->baseline_gf_interval);
  }

  if (oxcf->resize_mode == RESIZE_DYNAMIC) {
    // Default to starting GF groups at normal frame size.
    cpi->rc.next_frame_size_selector = UNSCALED;
  }
#if !CONFIG_ALWAYS_ADJUST_BPM
  // Reset rolling actual and target bits counters for ARF groups.
  twopass->rolling_arf_group_target_bits = 0;
  twopass->rolling_arf_group_actual_bits = 0;
#endif
}

// Intra / Inter threshold very low
#define VERY_LOW_II 1.5
// Clean slide transitions we expect a sharp single frame spike in error.
#define ERROR_SPIKE 5.0

// Slide show transition detection.
// Tests for case where there is very low error either side of the current frame
// but much higher just for this frame. This can help detect key frames in
// slide shows even where the slides are pictures of different sizes.
// Also requires that intra and inter errors are very similar to help eliminate
// harmful false positives.
// It will not help if the transition is a fade or other multi-frame effect.
static int slide_transition(const FIRSTPASS_STATS *this_frame,
                            const FIRSTPASS_STATS *last_frame,
                            const FIRSTPASS_STATS *next_frame) {
  return (this_frame->intra_error < (this_frame->coded_error * VERY_LOW_II)) &&
         (this_frame->coded_error > (last_frame->coded_error * ERROR_SPIKE)) &&
         (this_frame->coded_error > (next_frame->coded_error * ERROR_SPIKE));
}

// Threshold for use of the lagging second reference frame. High second ref
// usage may point to a transient event like a flash or occlusion rather than
// a real scene cut.
#define SECOND_REF_USEAGE_THRESH 0.1
// Minimum % intra coding observed in first pass (1.0 = 100%)
#define MIN_INTRA_LEVEL 0.25
// Minimum ratio between the % of intra coding and inter coding in the first
// pass after discounting neutral blocks (discounting neutral blocks in this
// way helps catch scene cuts in clips with very flat areas or letter box
// format clips with image padding.
#define INTRA_VS_INTER_THRESH 2.0
// Hard threshold where the first pass chooses intra for almost all blocks.
// In such a case even if the frame is not a scene cut coding a key frame
// may be a good option.
#define VERY_LOW_INTER_THRESH 0.05
// Maximum threshold for the relative ratio of intra error score vs best
// inter error score.
#define KF_II_ERR_THRESHOLD 2.5
// In real scene cuts there is almost always a sharp change in the intra
// or inter error score.
#define ERR_CHANGE_THRESHOLD 0.4
// For real scene cuts we expect an improvment in the intra inter error
// ratio in the next frame.
#define II_IMPROVEMENT_THRESHOLD 3.5
#define KF_II_MAX 128.0
#define II_FACTOR 12.5
// Test for very low intra complexity which could cause false key frames
#define V_LOW_INTRA 0.5

static int test_candidate_kf(TWO_PASS *twopass,
                             const FIRSTPASS_STATS *last_frame,
                             const FIRSTPASS_STATS *this_frame,
                             const FIRSTPASS_STATS *next_frame) {
  int is_viable_kf = 0;
  double pcnt_intra = 1.0 - this_frame->pcnt_inter;
  double modified_pcnt_inter =
      this_frame->pcnt_inter - this_frame->pcnt_neutral;

  // Does the frame satisfy the primary criteria of a key frame?
  // See above for an explanation of the test criteria.
  // If so, then examine how well it predicts subsequent frames.
  if ((this_frame->pcnt_second_ref < SECOND_REF_USEAGE_THRESH) &&
      (next_frame->pcnt_second_ref < SECOND_REF_USEAGE_THRESH) &&
      ((this_frame->pcnt_inter < VERY_LOW_INTER_THRESH) ||
       (slide_transition(this_frame, last_frame, next_frame)) ||
       ((pcnt_intra > MIN_INTRA_LEVEL) &&
        (pcnt_intra > (INTRA_VS_INTER_THRESH * modified_pcnt_inter)) &&
        ((this_frame->intra_error /
          DOUBLE_DIVIDE_CHECK(this_frame->coded_error)) <
         KF_II_ERR_THRESHOLD) &&
        ((fabs(last_frame->coded_error - this_frame->coded_error) /
              DOUBLE_DIVIDE_CHECK(this_frame->coded_error) >
          ERR_CHANGE_THRESHOLD) ||
         (fabs(last_frame->intra_error - this_frame->intra_error) /
              DOUBLE_DIVIDE_CHECK(this_frame->intra_error) >
          ERR_CHANGE_THRESHOLD) ||
         ((next_frame->intra_error /
           DOUBLE_DIVIDE_CHECK(next_frame->coded_error)) >
          II_IMPROVEMENT_THRESHOLD))))) {
    int i;
    const FIRSTPASS_STATS *start_pos = twopass->stats_in;
    FIRSTPASS_STATS local_next_frame = *next_frame;
    double boost_score = 0.0;
    double old_boost_score = 0.0;
    double decay_accumulator = 1.0;

    // Examine how well the key frame predicts subsequent frames.
    for (i = 0; i < 16; ++i) {
      double next_iiratio = (II_FACTOR * local_next_frame.intra_error /
                             DOUBLE_DIVIDE_CHECK(local_next_frame.coded_error));

      if (next_iiratio > KF_II_MAX) next_iiratio = KF_II_MAX;

      // Cumulative effect of decay in prediction quality.
      if (local_next_frame.pcnt_inter > 0.85)
        decay_accumulator *= local_next_frame.pcnt_inter;
      else
        decay_accumulator *= (0.85 + local_next_frame.pcnt_inter) / 2.0;

      // Keep a running total.
      boost_score += (decay_accumulator * next_iiratio);

      // Test various breakout clauses.
      if ((local_next_frame.pcnt_inter < 0.05) || (next_iiratio < 1.5) ||
          (((local_next_frame.pcnt_inter - local_next_frame.pcnt_neutral) <
            0.20) &&
           (next_iiratio < 3.0)) ||
          ((boost_score - old_boost_score) < 3.0) ||
          (local_next_frame.intra_error < V_LOW_INTRA)) {
        break;
      }

      old_boost_score = boost_score;

      // Get the next frame details
      if (EOF == input_stats(twopass, &local_next_frame)) break;
    }

    // If there is tolerable prediction for at least the next 3 frames then
    // break out else discard this potential key frame and move on
    if (boost_score > 30.0 && (i > 3)) {
      is_viable_kf = 1;
    } else {
      // Reset the file position
      reset_fpf_position(twopass, start_pos);

      is_viable_kf = 0;
    }
  }

  return is_viable_kf;
}

#define FRAMES_TO_CHECK_DECAY 8
#define MIN_KF_TOT_BOOST 300
#define KF_BOOST_SCAN_MAX_FRAMES 32
#define KF_ABS_ZOOM_THRESH 6.0

#ifdef AGGRESSIVE_VBR
#define KF_MAX_FRAME_BOOST 80.0
#define MAX_KF_TOT_BOOST 4800
#else
#define KF_MAX_FRAME_BOOST 96.0
#define MAX_KF_TOT_BOOST 5400
#endif

static void find_next_key_frame(VP9_COMP *cpi, FIRSTPASS_STATS *this_frame) {
  int i, j;
  RATE_CONTROL *const rc = &cpi->rc;
  TWO_PASS *const twopass = &cpi->twopass;
  GF_GROUP *const gf_group = &twopass->gf_group;
  const VP9EncoderConfig *const oxcf = &cpi->oxcf;
  const FIRSTPASS_STATS first_frame = *this_frame;
  const FIRSTPASS_STATS *const start_position = twopass->stats_in;
  FIRSTPASS_STATS next_frame;
  FIRSTPASS_STATS last_frame;
  int kf_bits = 0;
  double decay_accumulator = 1.0;
  double zero_motion_accumulator = 1.0;
  double boost_score = 0.0;
  double kf_mod_err = 0.0;
  double kf_raw_err = 0.0;
  double kf_group_err = 0.0;
  double recent_loop_decay[FRAMES_TO_CHECK_DECAY];
  double sr_accumulator = 0.0;
  double abs_mv_in_out_accumulator = 0.0;
  const double av_err = get_distribution_av_err(cpi, twopass);
  vp9_zero(next_frame);

  cpi->common.frame_type = KEY_FRAME;

  // Reset the GF group data structures.
  vp9_zero(*gf_group);

  // Is this a forced key frame by interval.
  rc->this_key_frame_forced = rc->next_key_frame_forced;

  // Clear the alt ref active flag and last group multi arf flags as they
  // can never be set for a key frame.
  rc->source_alt_ref_active = 0;
  cpi->multi_arf_last_grp_enabled = 0;

  // KF is always a GF so clear frames till next gf counter.
  rc->frames_till_gf_update_due = 0;

  rc->frames_to_key = 1;

  twopass->kf_group_bits = 0;          // Total bits available to kf group
  twopass->kf_group_error_left = 0.0;  // Group modified error score.

  kf_raw_err = this_frame->intra_error;
  kf_mod_err =
      calculate_norm_frame_score(cpi, twopass, oxcf, this_frame, av_err);

  // Initialize the decay rates for the recent frames to check
  for (j = 0; j < FRAMES_TO_CHECK_DECAY; ++j) recent_loop_decay[j] = 1.0;

  // Find the next keyframe.
  i = 0;
  while (twopass->stats_in < twopass->stats_in_end &&
         rc->frames_to_key < cpi->oxcf.key_freq) {
    // Accumulate kf group error.
    kf_group_err +=
        calculate_norm_frame_score(cpi, twopass, oxcf, this_frame, av_err);

    // Load the next frame's stats.
    last_frame = *this_frame;
    input_stats(twopass, this_frame);

    // Provided that we are not at the end of the file...
    if (cpi->oxcf.auto_key && twopass->stats_in < twopass->stats_in_end) {
      double loop_decay_rate;

      // Check for a scene cut.
      if (test_candidate_kf(twopass, &last_frame, this_frame,
                            twopass->stats_in))
        break;

      // How fast is the prediction quality decaying?
      loop_decay_rate = get_prediction_decay_rate(cpi, twopass->stats_in);

      // We want to know something about the recent past... rather than
      // as used elsewhere where we are concerned with decay in prediction
      // quality since the last GF or KF.
      recent_loop_decay[i % FRAMES_TO_CHECK_DECAY] = loop_decay_rate;
      decay_accumulator = 1.0;
      for (j = 0; j < FRAMES_TO_CHECK_DECAY; ++j)
        decay_accumulator *= recent_loop_decay[j];

      // Special check for transition or high motion followed by a
      // static scene.
      if (detect_transition_to_still(cpi, i, cpi->oxcf.key_freq - i,
                                     loop_decay_rate, decay_accumulator))
        break;

      // Step on to the next frame.
      ++rc->frames_to_key;

      // If we don't have a real key frame within the next two
      // key_freq intervals then break out of the loop.
      if (rc->frames_to_key >= 2 * cpi->oxcf.key_freq) break;
    } else {
      ++rc->frames_to_key;
    }
    ++i;
  }

  // If there is a max kf interval set by the user we must obey it.
  // We already breakout of the loop above at 2x max.
  // This code centers the extra kf if the actual natural interval
  // is between 1x and 2x.
  if (cpi->oxcf.auto_key && rc->frames_to_key > cpi->oxcf.key_freq) {
    FIRSTPASS_STATS tmp_frame = first_frame;

    rc->frames_to_key /= 2;

    // Reset to the start of the group.
    reset_fpf_position(twopass, start_position);

    kf_group_err = 0.0;

    // Rescan to get the correct error data for the forced kf group.
    for (i = 0; i < rc->frames_to_key; ++i) {
      kf_group_err +=
          calculate_norm_frame_score(cpi, twopass, oxcf, &tmp_frame, av_err);
      input_stats(twopass, &tmp_frame);
    }
    rc->next_key_frame_forced = 1;
  } else if (twopass->stats_in == twopass->stats_in_end ||
             rc->frames_to_key >= cpi->oxcf.key_freq) {
    rc->next_key_frame_forced = 1;
  } else {
    rc->next_key_frame_forced = 0;
  }

  // Special case for the last key frame of the file.
  if (twopass->stats_in >= twopass->stats_in_end) {
    // Accumulate kf group error.
    kf_group_err +=
        calculate_norm_frame_score(cpi, twopass, oxcf, this_frame, av_err);
  }

  // Calculate the number of bits that should be assigned to the kf group.
  if (twopass->bits_left > 0 && twopass->normalized_score_left > 0.0) {
    // Maximum number of bits for a single normal frame (not key frame).
    const int max_bits = frame_max_bits(rc, &cpi->oxcf);

    // Maximum number of bits allocated to the key frame group.
    int64_t max_grp_bits;

    // Default allocation based on bits left and relative
    // complexity of the section.
    twopass->kf_group_bits = (int64_t)(
        twopass->bits_left * (kf_group_err / twopass->normalized_score_left));

    // Clip based on maximum per frame rate defined by the user.
    max_grp_bits = (int64_t)max_bits * (int64_t)rc->frames_to_key;
    if (twopass->kf_group_bits > max_grp_bits)
      twopass->kf_group_bits = max_grp_bits;
  } else {
    twopass->kf_group_bits = 0;
  }
  twopass->kf_group_bits = VPXMAX(0, twopass->kf_group_bits);

  // Reset the first pass file position.
  reset_fpf_position(twopass, start_position);

  // Scan through the kf group collating various stats used to determine
  // how many bits to spend on it.
  boost_score = 0.0;

  for (i = 0; i < (rc->frames_to_key - 1); ++i) {
    if (EOF == input_stats(twopass, &next_frame)) break;

    if (i <= KF_BOOST_SCAN_MAX_FRAMES) {
      double frame_boost;
      double zm_factor;

      // Monitor for static sections.
      zero_motion_accumulator = VPXMIN(
          zero_motion_accumulator, get_zero_motion_factor(cpi, &next_frame));

      // Factor 0.75-1.25 based on how much of frame is static.
      zm_factor = (0.75 + (zero_motion_accumulator / 2.0));

      // The second (lagging) ref error is not valid immediately after
      // a key frame because either the lag has not built up (in the case of
      // the first key frame or it points to a refernce before the new key
      // frame.
      if (i < 2) sr_accumulator = 0.0;
      frame_boost = calc_kf_frame_boost(cpi, &next_frame, &sr_accumulator, 0,
                                        KF_MAX_FRAME_BOOST * zm_factor);

      boost_score += frame_boost;

      // Measure of zoom. Large zoom tends to indicate reduced boost.
      abs_mv_in_out_accumulator +=
          fabs(next_frame.mv_in_out_count * next_frame.pcnt_motion);

      if ((frame_boost < 25.00) ||
          (abs_mv_in_out_accumulator > KF_ABS_ZOOM_THRESH) ||
          (sr_accumulator > (kf_raw_err * 1.50)))
        break;
    } else {
      break;
    }
  }

  reset_fpf_position(twopass, start_position);

  // Store the zero motion percentage
  twopass->kf_zeromotion_pct = (int)(zero_motion_accumulator * 100.0);

  // Calculate a section intra ratio used in setting max loop filter.
  twopass->section_intra_rating = calculate_section_intra_ratio(
      start_position, twopass->stats_in_end, rc->frames_to_key);

  // Apply various clamps for min and max boost
  rc->kf_boost = VPXMAX((int)boost_score, (rc->frames_to_key * 3));
  rc->kf_boost = VPXMAX(rc->kf_boost, MIN_KF_TOT_BOOST);
  rc->kf_boost = VPXMIN(rc->kf_boost, MAX_KF_TOT_BOOST);

  // Work out how many bits to allocate for the key frame itself.
  kf_bits = calculate_boost_bits((rc->frames_to_key - 1), rc->kf_boost,
                                 twopass->kf_group_bits);

  twopass->kf_group_bits -= kf_bits;

  // Save the bits to spend on the key frame.
  gf_group->bit_allocation[0] = kf_bits;
  gf_group->update_type[0] = KF_UPDATE;
  gf_group->rf_level[0] = KF_STD;

  // Note the total error score of the kf group minus the key frame itself.
  twopass->kf_group_error_left = (kf_group_err - kf_mod_err);

  // Adjust the count of total modified error left.
  // The count of bits left is adjusted elsewhere based on real coded frame
  // sizes.
  twopass->normalized_score_left -= kf_group_err;

  if (oxcf->resize_mode == RESIZE_DYNAMIC) {
    // Default to normal-sized frame on keyframes.
    cpi->rc.next_frame_size_selector = UNSCALED;
  }
}

// Define the reference buffers that will be updated post encode.
static void configure_multi_arf_buffer_updates(VP9_COMP *cpi) {
  TWO_PASS *const twopass = &cpi->twopass;

  cpi->rc.is_src_frame_alt_ref = 0;
  cpi->rc.is_bwd_ref_frame = 0;
  cpi->rc.is_last_bipred_frame = 0;
  cpi->rc.is_bipred_frame = 0;
  cpi->rc.is_src_frame_ext_arf = 0;

  switch (twopass->gf_group.update_type[twopass->gf_group.index]) {
    case KF_UPDATE:
      cpi->refresh_last_frame = 1;
      cpi->refresh_golden_frame = 1;
      cpi->refresh_bwd_ref_frame = 1;
      cpi->refresh_alt2_ref_frame = 1;
      cpi->refresh_alt_ref_frame = 1;
      break;

    case LF_UPDATE:
      cpi->refresh_last_frame = 1;
      cpi->refresh_golden_frame = 0;
      cpi->refresh_bwd_ref_frame = 0;
      cpi->refresh_alt2_ref_frame = 0;
      cpi->refresh_alt_ref_frame = 0;
      break;

    case GF_UPDATE:
      cpi->refresh_last_frame = 1;
      cpi->refresh_golden_frame = 1;
      cpi->refresh_bwd_ref_frame = 0;
      cpi->refresh_alt2_ref_frame = 0;
      cpi->refresh_alt_ref_frame = 0;
      break;

    case OVERLAY_UPDATE:
      cpi->refresh_last_frame = 0;
      cpi->refresh_golden_frame = 1;
      cpi->refresh_bwd_ref_frame = 0;
      cpi->refresh_alt2_ref_frame = 0;
      cpi->refresh_alt_ref_frame = 0;

      cpi->rc.is_src_frame_alt_ref = 1;
      break;

    case ARF_UPDATE:
      cpi->refresh_last_frame = 0;
      cpi->refresh_golden_frame = 0;
      // NOTE: BWDREF does not get updated along with ALTREF_FRAME.
      cpi->refresh_bwd_ref_frame = 0;
      cpi->refresh_alt2_ref_frame = 0;
      cpi->refresh_alt_ref_frame = 1;
      break;

    case BRF_UPDATE:
      cpi->refresh_last_frame = 0;
      cpi->refresh_golden_frame = 0;
      cpi->refresh_bwd_ref_frame = 1;
      cpi->refresh_alt2_ref_frame = 0;
      cpi->refresh_alt_ref_frame = 0;

      cpi->rc.is_bwd_ref_frame = 1;
      break;

    case LAST_BIPRED_UPDATE:
      cpi->refresh_last_frame = 1;
      cpi->refresh_golden_frame = 0;
      cpi->refresh_bwd_ref_frame = 0;
      cpi->refresh_alt2_ref_frame = 0;
      cpi->refresh_alt_ref_frame = 0;

      cpi->rc.is_last_bipred_frame = 1;
      break;

    case BIPRED_UPDATE:
      cpi->refresh_last_frame = 1;
      cpi->refresh_golden_frame = 0;
      cpi->refresh_bwd_ref_frame = 0;
      cpi->refresh_alt2_ref_frame = 0;
      cpi->refresh_alt_ref_frame = 0;

      cpi->rc.is_bipred_frame = 1;
      break;

    case INTNL_OVERLAY_UPDATE:
      cpi->refresh_last_frame = 1;
      cpi->refresh_golden_frame = 0;
      cpi->refresh_bwd_ref_frame = 0;
      cpi->refresh_alt2_ref_frame = 0;
      cpi->refresh_alt_ref_frame = 0;

      cpi->rc.is_src_frame_alt_ref = 1;
      cpi->rc.is_src_frame_ext_arf = 1;
      break;

    case INTNL_ARF_UPDATE:
      cpi->refresh_last_frame = 0;
      cpi->refresh_golden_frame = 0;
      cpi->refresh_bwd_ref_frame = 0;
      cpi->refresh_alt2_ref_frame = 1;
      cpi->refresh_alt_ref_frame = 0;
      break;

    default:
      assert(0);
      break;
  }
}

static int is_skippable_frame(const VP9_COMP *cpi) {
  // If the current frame does not have non-zero motion vector detected in the
  // first  pass, and so do its previous and forward frames, then this frame
  // can be skipped for partition check, and the partition size is assigned
  // according to the variance
  const TWO_PASS *const twopass = &cpi->twopass;

  return (!frame_is_intra_only(&cpi->common) &&
          twopass->stats_in - 2 > twopass->stats_in_start &&
          twopass->stats_in < twopass->stats_in_end &&
          (twopass->stats_in - 1)->pcnt_inter -
                  (twopass->stats_in - 1)->pcnt_motion ==
              1 &&
          (twopass->stats_in - 2)->pcnt_inter -
                  (twopass->stats_in - 2)->pcnt_motion ==
              1 &&
          twopass->stats_in->pcnt_inter - twopass->stats_in->pcnt_motion == 1);
}

void vp9_rc_get_second_pass_params(VP9_COMP *cpi) {
  VP9_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;
  TWO_PASS *const twopass = &cpi->twopass;
  GF_GROUP *const gf_group = &twopass->gf_group;
  FIRSTPASS_STATS this_frame;

  if (!twopass->stats_in) return;

  // If this is an arf frame then we dont want to read the stats file or
  // advance the input pointer as we already have what we need.
  if (gf_group->update_type[gf_group->index] == ARF_UPDATE) {
    int target_rate;

    if (cpi->extra_arf_allowed) {
      configure_multi_arf_buffer_updates(cpi);
    } else {
      vp9_configure_buffer_updates(cpi, gf_group->index);
    }

    target_rate = gf_group->bit_allocation[gf_group->index];
    target_rate = vp9_rc_clamp_pframe_target_size(cpi, target_rate);
    rc->base_frame_target = target_rate;

    cm->frame_type = INTER_FRAME;

    // Do the firstpass stats indicate that this frame is skippable for the
    // partition search?
    if (cpi->sf.allow_partition_search_skip && cpi->oxcf.pass == 2 &&
        !cpi->use_svc) {
      cpi->partition_search_skippable_frame = is_skippable_frame(cpi);
    }

    return;
  }

  vpx_clear_system_state();

  if (cpi->oxcf.rc_mode == VPX_Q) {
    twopass->active_worst_quality = cpi->oxcf.cq_level;
  } else if (cm->current_video_frame == 0) {
    const int frames_left =
        (int)(twopass->total_stats.count - cm->current_video_frame);
    // Special case code for first frame.
    const int section_target_bandwidth =
        (int)(twopass->bits_left / frames_left);
    const double section_length = twopass->total_left_stats.count;
    const double section_error =
        twopass->total_left_stats.coded_error / section_length;
    const double section_intra_skip =
        twopass->total_left_stats.intra_skip_pct / section_length;
    const double section_inactive_zone =
        (twopass->total_left_stats.inactive_zone_rows * 2) /
        ((double)cm->mb_rows * section_length);
    const double section_noise =
        twopass->total_left_stats.frame_noise_energy / section_length;
    int tmp_q;

    tmp_q = get_twopass_worst_quality(
        cpi, section_error, section_intra_skip + section_inactive_zone,
        section_noise, section_target_bandwidth);

    twopass->active_worst_quality = tmp_q;
    twopass->baseline_active_worst_quality = tmp_q;
    rc->ni_av_qi = tmp_q;
    rc->last_q[INTER_FRAME] = tmp_q;
    rc->avg_q = vp9_convert_qindex_to_q(tmp_q, cm->bit_depth);
    rc->avg_frame_qindex[INTER_FRAME] = tmp_q;
    rc->last_q[KEY_FRAME] = (tmp_q + cpi->oxcf.best_allowed_q) / 2;
    rc->avg_frame_qindex[KEY_FRAME] = rc->last_q[KEY_FRAME];
  }
  vp9_zero(this_frame);
  if (EOF == input_stats(twopass, &this_frame)) return;

  // Set the frame content type flag.
  if (this_frame.intra_skip_pct >= FC_ANIMATION_THRESH)
    twopass->fr_content_type = FC_GRAPHICS_ANIMATION;
  else
    twopass->fr_content_type = FC_NORMAL;

  // Keyframe and section processing.
  if (rc->frames_to_key == 0 || (cpi->frame_flags & FRAMEFLAGS_KEY)) {
    FIRSTPASS_STATS this_frame_copy;
    this_frame_copy = this_frame;
    // Define next KF group and assign bits to it.
    find_next_key_frame(cpi, &this_frame);
    this_frame = this_frame_copy;
  } else {
    cm->frame_type = INTER_FRAME;
  }

  // Define a new GF/ARF group. (Should always enter here for key frames).
  if (rc->frames_till_gf_update_due == 0) {
    define_gf_group(cpi, &this_frame);

    rc->frames_till_gf_update_due = rc->baseline_gf_interval;

#if ARF_STATS_OUTPUT
    {
      FILE *fpfile;
      fpfile = fopen("arf.stt", "a");
      ++arf_count;
      fprintf(fpfile, "%10d %10ld %10d %10d %10ld\n", cm->current_video_frame,
              rc->frames_till_gf_update_due, rc->kf_boost, arf_count,
              rc->gfu_boost);

      fclose(fpfile);
    }
#endif
  }

  if (cpi->extra_arf_allowed) {
    configure_multi_arf_buffer_updates(cpi);
  } else {
    vp9_configure_buffer_updates(cpi, gf_group->index);
  }

  // Do the firstpass stats indicate that this frame is skippable for the
  // partition search?
  if (cpi->sf.allow_partition_search_skip && cpi->oxcf.pass == 2 &&
      !cpi->use_svc) {
    cpi->partition_search_skippable_frame = is_skippable_frame(cpi);
  }

  rc->base_frame_target = gf_group->bit_allocation[gf_group->index];

  // The multiplication by 256 reverses a scaling factor of (>> 8)
  // applied when combining MB error values for the frame.
  twopass->mb_av_energy = log((this_frame.intra_error * 256.0) + 1.0);
  twopass->mb_smooth_pct = this_frame.intra_smooth_pct;

  // Update the total stats remaining structure.
  subtract_stats(&twopass->total_left_stats, &this_frame);
}

#define MINQ_ADJ_LIMIT 48
#define MINQ_ADJ_LIMIT_CQ 20
#define HIGH_UNDERSHOOT_RATIO 2
void vp9_twopass_postencode_update(VP9_COMP *cpi) {
  TWO_PASS *const twopass = &cpi->twopass;
  RATE_CONTROL *const rc = &cpi->rc;
  VP9_COMMON *const cm = &cpi->common;
  const int bits_used = rc->base_frame_target;

  // VBR correction is done through rc->vbr_bits_off_target. Based on the
  // sign of this value, a limited % adjustment is made to the target rate
  // of subsequent frames, to try and push it back towards 0. This method
  // is designed to prevent extreme behaviour at the end of a clip
  // or group of frames.
  rc->vbr_bits_off_target += rc->base_frame_target - rc->projected_frame_size;
  twopass->bits_left = VPXMAX(twopass->bits_left - bits_used, 0);

  // Target vs actual bits for this arf group.
  twopass->rolling_arf_group_target_bits += rc->this_frame_target;
  twopass->rolling_arf_group_actual_bits += rc->projected_frame_size;

  // Calculate the pct rc error.
  if (rc->total_actual_bits) {
    rc->rate_error_estimate =
        (int)((rc->vbr_bits_off_target * 100) / rc->total_actual_bits);
    rc->rate_error_estimate = clamp(rc->rate_error_estimate, -100, 100);
  } else {
    rc->rate_error_estimate = 0;
  }

  if (cpi->common.frame_type != KEY_FRAME) {
    twopass->kf_group_bits -= bits_used;
    twopass->last_kfgroup_zeromotion_pct = twopass->kf_zeromotion_pct;
  }
  twopass->kf_group_bits = VPXMAX(twopass->kf_group_bits, 0);

  // Increment the gf group index ready for the next frame.
  ++twopass->gf_group.index;

  // If the rate control is drifting consider adjustment to min or maxq.
  if ((cpi->oxcf.rc_mode != VPX_Q) && !cpi->rc.is_src_frame_alt_ref) {
    const int maxq_adj_limit =
        rc->worst_quality - twopass->active_worst_quality;
    const int minq_adj_limit =
        (cpi->oxcf.rc_mode == VPX_CQ ? MINQ_ADJ_LIMIT_CQ : MINQ_ADJ_LIMIT);
    int aq_extend_min = 0;
    int aq_extend_max = 0;

    // Extend min or Max Q range to account for imbalance from the base
    // value when using AQ.
    if (cpi->oxcf.aq_mode != NO_AQ) {
      if (cm->seg.aq_av_offset < 0) {
        // The balance of the AQ map tends towarda lowering the average Q.
        aq_extend_min = 0;
        aq_extend_max = VPXMIN(maxq_adj_limit, -cm->seg.aq_av_offset);
      } else {
        // The balance of the AQ map tends towards raising the average Q.
        aq_extend_min = VPXMIN(minq_adj_limit, cm->seg.aq_av_offset);
        aq_extend_max = 0;
      }
    }

    // Undershoot.
    if (rc->rate_error_estimate > cpi->oxcf.under_shoot_pct) {
      --twopass->extend_maxq;
      if (rc->rolling_target_bits >= rc->rolling_actual_bits)
        ++twopass->extend_minq;
      // Overshoot.
    } else if (rc->rate_error_estimate < -cpi->oxcf.over_shoot_pct) {
      --twopass->extend_minq;
      if (rc->rolling_target_bits < rc->rolling_actual_bits)
        ++twopass->extend_maxq;
    } else {
      // Adjustment for extreme local overshoot.
      if (rc->projected_frame_size > (2 * rc->base_frame_target) &&
          rc->projected_frame_size > (2 * rc->avg_frame_bandwidth))
        ++twopass->extend_maxq;

      // Unwind undershoot or overshoot adjustment.
      if (rc->rolling_target_bits < rc->rolling_actual_bits)
        --twopass->extend_minq;
      else if (rc->rolling_target_bits > rc->rolling_actual_bits)
        --twopass->extend_maxq;
    }

    twopass->extend_minq =
        clamp(twopass->extend_minq, aq_extend_min, minq_adj_limit);
    twopass->extend_maxq =
        clamp(twopass->extend_maxq, aq_extend_max, maxq_adj_limit);

    // If there is a big and undexpected undershoot then feed the extra
    // bits back in quickly. One situation where this may happen is if a
    // frame is unexpectedly almost perfectly predicted by the ARF or GF
    // but not very well predcited by the previous frame.
    if (!frame_is_kf_gf_arf(cpi) && !cpi->rc.is_src_frame_alt_ref) {
      int fast_extra_thresh = rc->base_frame_target / HIGH_UNDERSHOOT_RATIO;
      if (rc->projected_frame_size < fast_extra_thresh) {
        rc->vbr_bits_off_target_fast +=
            fast_extra_thresh - rc->projected_frame_size;
        rc->vbr_bits_off_target_fast =
            VPXMIN(rc->vbr_bits_off_target_fast, (4 * rc->avg_frame_bandwidth));

        // Fast adaptation of minQ if necessary to use up the extra bits.
        if (rc->avg_frame_bandwidth) {
          twopass->extend_minq_fast =
              (int)(rc->vbr_bits_off_target_fast * 8 / rc->avg_frame_bandwidth);
        }
        twopass->extend_minq_fast = VPXMIN(
            twopass->extend_minq_fast, minq_adj_limit - twopass->extend_minq);
      } else if (rc->vbr_bits_off_target_fast) {
        twopass->extend_minq_fast = VPXMIN(
            twopass->extend_minq_fast, minq_adj_limit - twopass->extend_minq);
      } else {
        twopass->extend_minq_fast = 0;
      }
    }
  }
}
