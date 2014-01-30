/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <arm_neon.h>
#include "vp9/common/vp9_idct.h"

void vp9_idct4x4_1_add_neon(
        int16_t *input,
        uint8_t *dest,
        int dest_stride) {
    uint8x8_t d6u8, d7u8;
    uint32x2_t d2u32, d4u32;
    uint16x8_t q8u16, q9u16;
    int16x8_t q0s16;
    uint8_t *d;
    int16_t a1, cospi_16_64 = 11585;
    int16_t out = dct_const_round_shift(input[0] * cospi_16_64);
    out = dct_const_round_shift(out * cospi_16_64);
    a1 = ROUND_POWER_OF_TWO(out, 4);

    d2u32 = d4u32 = vdup_n_u32(0);
    q0s16 = vdupq_n_s16(a1);

    // dc_only_idct_add
    d = dest;
    d2u32 = vld1_lane_u32((const uint32_t *)d, d2u32, 0); d += dest_stride;
    d2u32 = vld1_lane_u32((const uint32_t *)d, d2u32, 1); d += dest_stride;
    d4u32 = vld1_lane_u32((const uint32_t *)d, d4u32, 0); d += dest_stride;
    d4u32 = vld1_lane_u32((const uint32_t *)d, d4u32, 1);

    q8u16 = vaddw_u8(vreinterpretq_u16_s16(q0s16), vreinterpret_u8_u32(d2u32));
    q9u16 = vaddw_u8(vreinterpretq_u16_s16(q0s16), vreinterpret_u8_u32(d4u32));

    d6u8 = vqmovun_s16(vreinterpretq_s16_u16(q8u16));
    d7u8 = vqmovun_s16(vreinterpretq_s16_u16(q9u16));

    d = dest;
    vst1_lane_u32((uint32_t *)d, vreinterpret_u32_u8(d6u8), 0);
    d += dest_stride;
    vst1_lane_u32((uint32_t *)d, vreinterpret_u32_u8(d6u8), 1);
    d += dest_stride;
    vst1_lane_u32((uint32_t *)d, vreinterpret_u32_u8(d7u8), 0);
    d += dest_stride;
    vst1_lane_u32((uint32_t *)d, vreinterpret_u32_u8(d7u8), 1);
    return;
}
