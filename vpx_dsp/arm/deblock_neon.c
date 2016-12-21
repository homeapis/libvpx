/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <arm_neon.h>
#include <assert.h>

#include "./vpx_dsp_rtcd.h"
#include "vpx/vpx_integer.h"
#include "vpx_dsp/arm/transpose_neon.h"

static uint8x8_t average_k_out(const uint8x8_t a2, const uint8x8_t a1,
                               const uint8x8_t v0, const uint8x8_t b1,
                               const uint8x8_t b2) {
  const uint8x8_t k1 = vrhadd_u8(a2, a1);
  const uint8x8_t k2 = vrhadd_u8(b2, b1);
  const uint8x8_t k3 = vrhadd_u8(k1, k2);
  return vrhadd_u8(k3, v0);
}

static uint8x8_t generate_mask(const uint8x8_t a2, const uint8x8_t a1,
                               const uint8x8_t v0, const uint8x8_t b1,
                               const uint8x8_t b2, const uint8x8_t filter) {
  const uint8x8_t a2_v0 = vabd_u8(a2, v0);
  const uint8x8_t a1_v0 = vabd_u8(a1, v0);
  const uint8x8_t b1_v0 = vabd_u8(b1, v0);
  const uint8x8_t b2_v0 = vabd_u8(b2, v0);

  uint8x8_t max = vmax_u8(a2_v0, a1_v0);
  max = vmax_u8(b1_v0, max);
  max = vmax_u8(b2_v0, max);
  return vclt_u8(max, filter);
}

static uint8x8_t generate_output(const uint8x8_t a2, const uint8x8_t a1,
                                 const uint8x8_t v0, const uint8x8_t b1,
                                 const uint8x8_t b2, const uint8x8_t filter) {
  const uint8x8_t k_out = average_k_out(a2, a1, v0, b1, b2);
  const uint8x8_t mask = generate_mask(a2, a1, v0, b1, b2, filter);

  return vbsl_u8(mask, k_out, v0);
}

// Same functions but for uint8x16_t.
static uint8x16_t average_k_outq(const uint8x16_t a2, const uint8x16_t a1,
                                 const uint8x16_t v0, const uint8x16_t b1,
                                 const uint8x16_t b2) {
  const uint8x16_t k1 = vrhaddq_u8(a2, a1);
  const uint8x16_t k2 = vrhaddq_u8(b2, b1);
  const uint8x16_t k3 = vrhaddq_u8(k1, k2);
  return vrhaddq_u8(k3, v0);
}

static uint8x16_t generate_maskq(const uint8x16_t a2, const uint8x16_t a1,
                                 const uint8x16_t v0, const uint8x16_t b1,
                                 const uint8x16_t b2, const uint8x16_t filter) {
  const uint8x16_t a2_v0 = vabdq_u8(a2, v0);
  const uint8x16_t a1_v0 = vabdq_u8(a1, v0);
  const uint8x16_t b1_v0 = vabdq_u8(b1, v0);
  const uint8x16_t b2_v0 = vabdq_u8(b2, v0);

  uint8x16_t max = vmaxq_u8(a2_v0, a1_v0);
  max = vmaxq_u8(b1_v0, max);
  max = vmaxq_u8(b2_v0, max);
  return vcltq_u8(max, filter);
}

static uint8x16_t generate_outputq(const uint8x16_t a2, const uint8x16_t a1,
                                   const uint8x16_t v0, const uint8x16_t b1,
                                   const uint8x16_t b2,
                                   const uint8x16_t filter) {
  const uint8x16_t k_out = average_k_outq(a2, a1, v0, b1, b2);
  const uint8x16_t mask = generate_maskq(a2, a1, v0, b1, b2, filter);

  return vbslq_u8(mask, k_out, v0);
}

void vpx_post_proc_down_and_across_mb_row_neon(uint8_t *src_ptr,
                                               uint8_t *dst_ptr, int src_stride,
                                               int dst_stride, int cols,
                                               uint8_t *f, int size) {
  uint8_t *src, *dst;
  int row;
  int col;

  // Process a stripe of macroblocks. The stripe will be a multiple of 16 (for
  // Y) or 8 (for U/V) wide (cols) and the height (size) will be 16 (for Y) or 8
  // (for U/V).
  assert((size == 8 || size == 16) && cols % 8 == 0);

  // While columns of length 16 can be processed, load them.
  for (col = 0; col < cols - 8; col += 16) {
    uint8x16_t a0, a1, a2, a3, a4, a5, a6, a7;
    src = src_ptr - 2 * src_stride;
    dst = dst_ptr;

    a0 = vld1q_u8(src);
    src += src_stride;
    a1 = vld1q_u8(src);
    src += src_stride;
    a2 = vld1q_u8(src);
    src += src_stride;
    a3 = vld1q_u8(src);
    src += src_stride;

    for (row = 0; row < size; row += 4) {
      uint8x16_t v_out_0, v_out_1, v_out_2, v_out_3;
      const uint8x16_t filterq = vld1q_u8(f + col);

      a4 = vld1q_u8(src);
      src += src_stride;
      a5 = vld1q_u8(src);
      src += src_stride;
      a6 = vld1q_u8(src);
      src += src_stride;
      a7 = vld1q_u8(src);
      src += src_stride;

      v_out_0 = generate_outputq(a0, a1, a2, a3, a4, filterq);
      v_out_1 = generate_outputq(a1, a2, a3, a4, a5, filterq);
      v_out_2 = generate_outputq(a2, a3, a4, a5, a6, filterq);
      v_out_3 = generate_outputq(a3, a4, a5, a6, a7, filterq);

      vst1q_u8(dst, v_out_0);
      dst += dst_stride;
      vst1q_u8(dst, v_out_1);
      dst += dst_stride;
      vst1q_u8(dst, v_out_2);
      dst += dst_stride;
      vst1q_u8(dst, v_out_3);
      dst += dst_stride;

      // Rotate over to the next slot.
      a0 = a4;
      a1 = a5;
      a2 = a6;
      a3 = a7;
    }

    src_ptr += 16;
    dst_ptr += 16;
  }

  // Clean up any left over column of length 8.
  if (col != cols) {
    uint8x8_t a0, a1, a2, a3, a4, a5, a6, a7;
    src = src_ptr - 2 * src_stride;
    dst = dst_ptr;

    a0 = vld1_u8(src);
    src += src_stride;
    a1 = vld1_u8(src);
    src += src_stride;
    a2 = vld1_u8(src);
    src += src_stride;
    a3 = vld1_u8(src);
    src += src_stride;

    for (row = 0; row < size; row += 4) {
      uint8x8_t v_out_0, v_out_1, v_out_2, v_out_3;
      const uint8x8_t filter = vld1_u8(f + col);

      a4 = vld1_u8(src);
      src += src_stride;
      a5 = vld1_u8(src);
      src += src_stride;
      a6 = vld1_u8(src);
      src += src_stride;
      a7 = vld1_u8(src);
      src += src_stride;

      v_out_0 = generate_output(a0, a1, a2, a3, a4, filter);
      v_out_1 = generate_output(a1, a2, a3, a4, a5, filter);
      v_out_2 = generate_output(a2, a3, a4, a5, a6, filter);
      v_out_3 = generate_output(a3, a4, a5, a6, a7, filter);

      vst1_u8(dst, v_out_0);
      dst += dst_stride;
      vst1_u8(dst, v_out_1);
      dst += dst_stride;
      vst1_u8(dst, v_out_2);
      dst += dst_stride;
      vst1_u8(dst, v_out_3);
      dst += dst_stride;

      // Rotate over to the next slot.
      a0 = a4;
      a1 = a5;
      a2 = a6;
      a3 = a7;
    }

    // Not strictly necessary but makes resetting dst_ptr easier.
    dst_ptr += 8;
  }

  dst_ptr -= cols;

  for (row = 0; row < size; row += 8) {
    uint8x8_t a0, a1, a2, a3;
    uint8x8_t b0, b1, b2, b3, b4, b5, b6, b7;

    src = dst_ptr;
    dst = dst_ptr;

    // Load 8 values, transpose 4 of them, and discard 2 because they will be
    // reloaded later.
    load_and_transpose_u8_4x8(src, dst_stride, &a0, &a1, &a2, &a3);
    a3 = a1;
    a2 = a1 = a0;  // Extend left border.

    src += 2;

    for (col = 0; col < cols; col += 8) {
      uint8x8_t v_out_0, v_out_1, v_out_2, v_out_3, v_out_4, v_out_5, v_out_6,
          v_out_7;
      // Although the filter is meant to be applied vertically and is instead
      // being applied horizontally here it's OK because it's set in blocks of 8
      // (or 16).
      const uint8x8_t filter = vld1_u8(f + col);

      load_and_transpose_u8_8x8(src, dst_stride, &b0, &b1, &b2, &b3, &b4, &b5,
                                &b6, &b7);

      if (col + 8 == cols) {
        // Last row. Extend border (b5).
        b6 = b7 = b5;
      }

      v_out_0 = generate_output(a0, a1, a2, a3, b0, filter);
      v_out_1 = generate_output(a1, a2, a3, b0, b1, filter);
      v_out_2 = generate_output(a2, a3, b0, b1, b2, filter);
      v_out_3 = generate_output(a3, b0, b1, b2, b3, filter);
      v_out_4 = generate_output(b0, b1, b2, b3, b4, filter);
      v_out_5 = generate_output(b1, b2, b3, b4, b5, filter);
      v_out_6 = generate_output(b2, b3, b4, b5, b6, filter);
      v_out_7 = generate_output(b3, b4, b5, b6, b7, filter);

      transpose_and_store_u8_8x8(dst, dst_stride, v_out_0, v_out_1, v_out_2,
                                 v_out_3, v_out_4, v_out_5, v_out_6, v_out_7);

      a0 = b4;
      a1 = b5;
      a2 = b6;
      a3 = b7;

      src += 8;
      dst += 8;
    }

    dst_ptr += 8 * dst_stride;
  }
}

// sum += x;
// sumsq += x * y;
static void accumulate_sum_sumsq(int32x4_t *sum, int32x4_t *sumsq,
                                 const int16x4_t x, const int32x4_t xy) {
  const int16x4_t zero = vdup_n_s16(0);
  const int32x4_t zeroq = vdupq_n_s32(0);

  // Add in the first set because vext doesn't work with '0'.
  *sum = vaddw_s16(*sum, x);
  *sumsq = vaddq_s32(*sumsq, xy);

  // Shift x and xy to the right and sum. The compiler doesn't always unroll
  // this and use constants which causes a build failure because vext doesn't
  // accept variables.
  *sum = vaddw_s16(*sum, vext_s16(zero, x, 1));
  *sumsq = vaddq_s32(*sumsq, vextq_s32(zeroq, xy, 1));

  *sum = vaddw_s16(*sum, vext_s16(zero, x, 2));
  *sumsq = vaddq_s32(*sumsq, vextq_s32(zeroq, xy, 2));

  *sum = vaddw_s16(*sum, vext_s16(zero, x, 3));
  *sumsq = vaddq_s32(*sumsq, vextq_s32(zeroq, xy, 3));
}

// Generate mask based on (sumsq * 15 - sum * sum < flimit)
static uint16x4_t mask_math(const int32x4_t sum, const int32x4_t sumsq,
                            const int32x4_t f, const int32x4_t fifteen) {
  const int32x4_t a = vmulq_s32(sum, sum);
  const int32x4_t b = vmulq_s32(sumsq, fifteen);
  const int32x4_t b_a = vsubq_s32(b, a);
  const uint32x4_t mask32 = vcltq_s32(b_a, f);
  return vmovn_u32(mask32);
}

static uint8x8_t calculate_filter_mask(const int32x4_t sum_low,
                                       const int32x4_t sum_high,
                                       const int32x4_t sumsq_low,
                                       const int32x4_t sumsq_high,
                                       const int32x4_t f) {
  const int32x4_t fifteen = vdupq_n_s32(15);
  const uint16x4_t mask16_low = mask_math(sum_low, sumsq_low, f, fifteen);
  const uint16x4_t mask16_high = mask_math(sum_high, sumsq_high, f, fifteen);
  return vmovn_u16(vcombine_u16(mask16_low, mask16_high));
}

// Apply filter of (8 + sum + s[c]) >> 4.
static uint8x8_t filter_pixels(const int32x4_t sum_low,
                               const int32x4_t sum_high, const uint8x8_t s) {
  // Expand s to s16 and add to sum, expanding further to s32.
  const int16x8_t s16 = vreinterpretq_s16_u16(vmovl_u8(s));
  const int32x4_t sum_s_low = vaddw_s16(sum_low, vget_low_s16(s16));
  const int32x4_t sum_s_high = vaddw_s16(sum_high, vget_high_s16(s16));

  // Round, shift, narrow, saturate to unsigned, and narrow again.
  return vqmovn_u16(vcombine_u16(vqrshrun_n_s32(sum_s_low, 4),
                                 vqrshrun_n_s32(sum_s_high, 4)));
}

void vpx_mbpost_proc_across_ip_neon(uint8_t *src, int pitch, int rows, int cols,
                                    int flimit) {
  int row, col;
  const int32x4_t f = vdupq_n_s32(flimit);

  assert(cols % 8 == 0);

  for (row = 0; row < rows; ++row) {
    // Sum the first 8 elements, which are extended from s[0], and s[0].
    // sumsq gets primed with +16.
    int sumsq = src[0] * src[0] * 9 + 16;
    int sum = src[0] * 9;

    uint8x8_t left_context, s, right_context;
    int32x4_t sum_low, sum_high, sumsq_low, sumsq_high;

    // Sum (+square) the next 6 elements.
    // Skip [0] because it's included above.
    for (col = 1; col <= 6; ++col) {
      sumsq += src[col] * src[col];
      sum += src[col];
    }

    // Prime the sums. Later the loop uses the _high values to prime the new
    // vectors.
    sumsq_high = vdupq_n_s32(sumsq);
    sum_high = vdupq_n_s32(sum);

    // Manually extend the left border.
    left_context = vdup_n_u8(src[0]);

    for (col = 0; col < cols; col += 8) {
      uint8x8_t mask, output;
      int16x8_t x, y;
      int32x4_t xy_low, xy_high;

      s = vld1_u8(src + col);

      if (col + 8 == cols) {
        // Last row. Extend border.
        right_context = vdup_n_u8(src[7 + col]);
      } else {
        right_context = vld1_u8(src + 7 + col);
      }

      x = vreinterpretq_s16_u16(vsubl_u8(right_context, left_context));
      y = vreinterpretq_s16_u16(vaddl_u8(right_context, left_context));
      xy_low = vmull_s16(vget_low_s16(x), vget_low_s16(y));
      xy_high = vmull_s16(vget_high_s16(x), vget_high_s16(y));

      // Catch up to the last sum'd value.
      sum_low = vdupq_lane_s32(vget_high_s32(sum_high), 1);
      sumsq_low = vdupq_lane_s32(vget_high_s32(sumsq_high), 1);

      accumulate_sum_sumsq(&sum_low, &sumsq_low, vget_low_s16(x), xy_low);

      // Need to do this sequentially because we need the max value from
      // sum_low.
      sum_high = vdupq_lane_s32(vget_high_s32(sum_low), 1);
      sumsq_high = vdupq_lane_s32(vget_high_s32(sumsq_low), 1);

      accumulate_sum_sumsq(&sum_high, &sumsq_high, vget_high_s16(x), xy_high);

      mask = calculate_filter_mask(sum_low, sum_high, sumsq_low, sumsq_high, f);

      output = filter_pixels(sum_low, sum_high, s);

      output = vbsl_u8(mask, output, s);

      vst1_u8(src + col, output);

      left_context = s;
    }

    src += pitch;
  }
}
