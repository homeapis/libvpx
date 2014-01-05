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

static const uint16_t bilinear_taps_coeff[8][2] = {
    {128,   0},
    {112,  16},
    { 96,  32},
    { 80,  48},
    { 64,  64},
    { 48,  80},
    { 32,  96},
    { 16, 112}
};

unsigned int vp8_sub_pixel_variance16x16_neon_func(
        const unsigned char *src_ptr,
        int src_pixels_per_line,
        int xoffset,
        int yoffset,
        const unsigned char *dst_ptr,
        int dst_pixels_per_line,
        unsigned int *sse) {
    int i;
    unsigned char tmp[528];
    unsigned char *tmpp;
    uint8x8_t d0u8, d1u8, d2u8, d3u8, d4u8, d5u8, d6u8, d7u8, d8u8, d9u8;
    uint8x8_t d10u8, d11u8, d12u8, d13u8, d14u8, d15u8, d16u8, d17u8, d18u8;
    uint8x8_t d19u8, d20u8, d21u8;
    int16x4_t d22s16, d23s16, d24s16, d25s16, d26s16, d27s16, d28s16, d29s16;
    uint32x2_t d0u32, d10u32;
    int64_t d0s64, d1s64, d2s64, d3s64;
    uint8x16_t q0u8, q1u8, q2u8, q3u8, q4u8, q5u8, q6u8, q7u8, q8u8, q9u8;
    uint8x16_t q10u8, q11u8, q12u8, q13u8, q14u8, q15u8;
    uint16x8_t q1u16, q2u16, q3u16, q4u16, q5u16, q6u16, q7u16, q8u16;
    uint16x8_t q9u16, q10u16, q11u16, q12u16, q13u16, q14u16;
    int32x4_t q8s32, q9s32, q10s32;
    int64x2_t q0s64, q1s64, q5s64;

    tmpp = tmp;
    if (xoffset == 0) {  // secondpass_bfilter16x16_only
        d0u8 = vdup_n_u8(bilinear_taps_coeff[yoffset][0]);
        d1u8 = vdup_n_u8(bilinear_taps_coeff[yoffset][1]);

        q11u8 = vld1q_u8(src_ptr);
        src_ptr += src_pixels_per_line;
        for (i = 4; i > 0; i--) {
            q12u8 = vld1q_u8(src_ptr);
            src_ptr += src_pixels_per_line;
            q13u8 = vld1q_u8(src_ptr);
            src_ptr += src_pixels_per_line;
            q14u8 = vld1q_u8(src_ptr);
            src_ptr += src_pixels_per_line;
            q15u8 = vld1q_u8(src_ptr);
            src_ptr += src_pixels_per_line;

            __builtin_prefetch(src_ptr);
            __builtin_prefetch(src_ptr + src_pixels_per_line);
            __builtin_prefetch(src_ptr + src_pixels_per_line * 2);

            q1u16 = vmull_u8(vget_low_u8(q11u8), d0u8);
            q2u16 = vmull_u8(vget_high_u8(q11u8), d0u8);
            q3u16 = vmull_u8(vget_low_u8(q12u8), d0u8);
            q4u16 = vmull_u8(vget_high_u8(q12u8), d0u8);
            q5u16 = vmull_u8(vget_low_u8(q13u8), d0u8);
            q6u16 = vmull_u8(vget_high_u8(q13u8), d0u8);
            q7u16 = vmull_u8(vget_low_u8(q14u8), d0u8);
            q8u16 = vmull_u8(vget_high_u8(q14u8), d0u8);

            q1u16 = vmlal_u8(q1u16, vget_low_u8(q12u8), d1u8);
            q2u16 = vmlal_u8(q2u16, vget_high_u8(q12u8), d1u8);
            q3u16 = vmlal_u8(q3u16, vget_low_u8(q13u8), d1u8);
            q4u16 = vmlal_u8(q4u16, vget_high_u8(q13u8), d1u8);
            q5u16 = vmlal_u8(q5u16, vget_low_u8(q14u8), d1u8);
            q6u16 = vmlal_u8(q6u16, vget_high_u8(q14u8), d1u8);
            q7u16 = vmlal_u8(q7u16, vget_low_u8(q15u8), d1u8);
            q8u16 = vmlal_u8(q8u16, vget_high_u8(q15u8), d1u8);

            d2u8 = vqrshrn_n_u16(q1u16, 7);
            d3u8 = vqrshrn_n_u16(q2u16, 7);
            d4u8 = vqrshrn_n_u16(q3u16, 7);
            d5u8 = vqrshrn_n_u16(q4u16, 7);
            d6u8 = vqrshrn_n_u16(q5u16, 7);
            d7u8 = vqrshrn_n_u16(q6u16, 7);
            d8u8 = vqrshrn_n_u16(q7u16, 7);
            d9u8 = vqrshrn_n_u16(q8u16, 7);

            q1u8 = vcombine_u8(d2u8, d3u8);
            q2u8 = vcombine_u8(d4u8, d5u8);
            q3u8 = vcombine_u8(d6u8, d7u8);
            q4u8 = vcombine_u8(d8u8, d9u8);

            q11u8 = q15u8;

            vst1q_u8((uint8_t *)tmpp, q1u8);
            tmpp += 16;
            vst1q_u8((uint8_t *)tmpp, q2u8);
            tmpp += 16;
            vst1q_u8((uint8_t *)tmpp, q3u8);
            tmpp += 16;
            vst1q_u8((uint8_t *)tmpp, q4u8);
            tmpp += 16;
        }
    } else if (yoffset == 0) {  // firstpass_bfilter16x16_only
        d0u8 = vdup_n_u8(bilinear_taps_coeff[xoffset][0]);
        d1u8 = vdup_n_u8(bilinear_taps_coeff[xoffset][1]);

        for (i = 4; i > 0 ; i--) {
            d2u8 = vld1_u8(src_ptr);
            d3u8 = vld1_u8(src_ptr + 8);
            d4u8 = vld1_u8(src_ptr + 16);
            src_ptr += src_pixels_per_line;
            d5u8 = vld1_u8(src_ptr);
            d6u8 = vld1_u8(src_ptr + 8);
            d7u8 = vld1_u8(src_ptr + 16);
            src_ptr += src_pixels_per_line;
            d8u8 = vld1_u8(src_ptr);
            d9u8 = vld1_u8(src_ptr + 8);
            d10u8 = vld1_u8(src_ptr + 16);
            src_ptr += src_pixels_per_line;
            d11u8 = vld1_u8(src_ptr);
            d12u8 = vld1_u8(src_ptr + 8);
            d13u8 = vld1_u8(src_ptr + 16);
            src_ptr += src_pixels_per_line;

            __builtin_prefetch(src_ptr);
            __builtin_prefetch(src_ptr + src_pixels_per_line);
            __builtin_prefetch(src_ptr + src_pixels_per_line * 2);

            q7u16  = vmull_u8(d2u8, d0u8);
            q8u16  = vmull_u8(d3u8, d0u8);
            q9u16  = vmull_u8(d5u8, d0u8);
            q10u16 = vmull_u8(d6u8, d0u8);
            q11u16 = vmull_u8(d8u8, d0u8);
            q12u16 = vmull_u8(d9u8, d0u8);
            q13u16 = vmull_u8(d11u8, d0u8);
            q14u16 = vmull_u8(d12u8, d0u8);

            d2u8  = vext_u8(d2u8, d3u8, 1);
            d5u8  = vext_u8(d5u8, d6u8, 1);
            d8u8  = vext_u8(d8u8, d9u8, 1);
            d11u8 = vext_u8(d11u8, d12u8, 1);

            q7u16  = vmlal_u8(q7u16, d2u8, d1u8);
            q9u16  = vmlal_u8(q9u16, d5u8, d1u8);
            q11u16 = vmlal_u8(q11u16, d8u8, d1u8);
            q13u16 = vmlal_u8(q13u16, d11u8, d1u8);

            d3u8  = vext_u8(d3u8, d4u8, 1);
            d6u8  = vext_u8(d6u8, d7u8, 1);
            d9u8  = vext_u8(d9u8, d10u8, 1);
            d12u8 = vext_u8(d12u8, d13u8, 1);

            q8u16  = vmlal_u8(q8u16,  d3u8, d1u8);
            q10u16 = vmlal_u8(q10u16, d6u8, d1u8);
            q12u16 = vmlal_u8(q12u16, d9u8, d1u8);
            q14u16 = vmlal_u8(q14u16, d12u8, d1u8);

            d14u8 = vqrshrn_n_u16(q7u16, 7);
            d15u8 = vqrshrn_n_u16(q8u16, 7);
            d16u8 = vqrshrn_n_u16(q9u16, 7);
            d17u8 = vqrshrn_n_u16(q10u16, 7);
            d18u8 = vqrshrn_n_u16(q11u16, 7);
            d19u8 = vqrshrn_n_u16(q12u16, 7);
            d20u8 = vqrshrn_n_u16(q13u16, 7);
            d21u8 = vqrshrn_n_u16(q14u16, 7);

            q7u8  = vcombine_u8(d14u8, d15u8);
            q8u8  = vcombine_u8(d16u8, d17u8);
            q9u8  = vcombine_u8(d18u8, d19u8);
            q10u8 = vcombine_u8(d20u8, d21u8);

            vst1q_u8((uint8_t *)tmpp, q7u8);
            tmpp += 16;
            vst1q_u8((uint8_t *)tmpp, q8u8);
            tmpp += 16;
            vst1q_u8((uint8_t *)tmpp, q9u8);
            tmpp += 16;
            vst1q_u8((uint8_t *)tmpp, q10u8);
            tmpp += 16;
        }
    } else {
        d0u8 = vdup_n_u8(bilinear_taps_coeff[xoffset][0]);
        d1u8 = vdup_n_u8(bilinear_taps_coeff[xoffset][1]);

        d2u8 = vld1_u8(src_ptr);
        d3u8 = vld1_u8(src_ptr + 8);
        d4u8 = vld1_u8(src_ptr + 16);
        src_ptr += src_pixels_per_line;
        d5u8 = vld1_u8(src_ptr);
        d6u8 = vld1_u8(src_ptr + 8);
        d7u8 = vld1_u8(src_ptr + 16);
        src_ptr += src_pixels_per_line;
        d8u8 = vld1_u8(src_ptr);
        d9u8 = vld1_u8(src_ptr + 8);
        d10u8 = vld1_u8(src_ptr + 16);
        src_ptr += src_pixels_per_line;
        d11u8 = vld1_u8(src_ptr);
        d12u8 = vld1_u8(src_ptr + 8);
        d13u8 = vld1_u8(src_ptr + 16);
        src_ptr += src_pixels_per_line;

        // First Pass: output_height lines x output_width columns (17x16)
        for (i = 3; i > 0; i--) {
            q7u16  = vmull_u8(d2u8, d0u8);
            q8u16  = vmull_u8(d3u8, d0u8);
            q9u16  = vmull_u8(d5u8, d0u8);
            q10u16 = vmull_u8(d6u8, d0u8);
            q11u16 = vmull_u8(d8u8, d0u8);
            q12u16 = vmull_u8(d9u8, d0u8);
            q13u16 = vmull_u8(d11u8, d0u8);
            q14u16 = vmull_u8(d12u8, d0u8);

            d2u8  = vext_u8(d2u8, d3u8, 1);
            d5u8  = vext_u8(d5u8, d6u8, 1);
            d8u8  = vext_u8(d8u8, d9u8, 1);
            d11u8 = vext_u8(d11u8, d12u8, 1);

            q7u16  = vmlal_u8(q7u16, d2u8, d1u8);
            q9u16  = vmlal_u8(q9u16, d5u8, d1u8);
            q11u16 = vmlal_u8(q11u16, d8u8, d1u8);
            q13u16 = vmlal_u8(q13u16, d11u8, d1u8);

            d3u8  = vext_u8(d3u8, d4u8, 1);
            d6u8  = vext_u8(d6u8, d7u8, 1);
            d9u8  = vext_u8(d9u8, d10u8, 1);
            d12u8 = vext_u8(d12u8, d13u8, 1);

            q8u16  = vmlal_u8(q8u16,  d3u8, d1u8);
            q10u16 = vmlal_u8(q10u16, d6u8, d1u8);
            q12u16 = vmlal_u8(q12u16, d9u8, d1u8);
            q14u16 = vmlal_u8(q14u16, d12u8, d1u8);

            d14u8 = vqrshrn_n_u16(q7u16, 7);
            d15u8 = vqrshrn_n_u16(q8u16, 7);
            d16u8 = vqrshrn_n_u16(q9u16, 7);
            d17u8 = vqrshrn_n_u16(q10u16, 7);
            d18u8 = vqrshrn_n_u16(q11u16, 7);
            d19u8 = vqrshrn_n_u16(q12u16, 7);
            d20u8 = vqrshrn_n_u16(q13u16, 7);
            d21u8 = vqrshrn_n_u16(q14u16, 7);

            d2u8 = vld1_u8(src_ptr);
            d3u8 = vld1_u8(src_ptr + 8);
            d4u8 = vld1_u8(src_ptr + 16);
            src_ptr += src_pixels_per_line;
            d5u8 = vld1_u8(src_ptr);
            d6u8 = vld1_u8(src_ptr + 8);
            d7u8 = vld1_u8(src_ptr + 16);
            src_ptr += src_pixels_per_line;
            d8u8 = vld1_u8(src_ptr);
            d9u8 = vld1_u8(src_ptr + 8);
            d10u8 = vld1_u8(src_ptr + 16);
            src_ptr += src_pixels_per_line;
            d11u8 = vld1_u8(src_ptr);
            d12u8 = vld1_u8(src_ptr + 8);
            d13u8 = vld1_u8(src_ptr + 16);
            src_ptr += src_pixels_per_line;

            q7u8 = vcombine_u8(d14u8, d15u8);
            q8u8 = vcombine_u8(d16u8, d17u8);
            q9u8 = vcombine_u8(d18u8, d19u8);
            q10u8 = vcombine_u8(d20u8, d21u8);

            vst1q_u8((uint8_t *)tmpp, q7u8);
            tmpp += 16;
            vst1q_u8((uint8_t *)tmpp, q8u8);
            tmpp += 16;
            vst1q_u8((uint8_t *)tmpp, q9u8);
            tmpp += 16;
            vst1q_u8((uint8_t *)tmpp, q10u8);
            tmpp += 16;
        }

        // First-pass filtering for rest 5 lines
        d14u8 = vld1_u8(src_ptr);
        d15u8 = vld1_u8(src_ptr + 8);
        d16u8 = vld1_u8(src_ptr + 16);
        src_ptr += src_pixels_per_line;

        q9u16  = vmull_u8(d2u8, d0u8);
        q10u16 = vmull_u8(d3u8, d0u8);
        q11u16 = vmull_u8(d5u8, d0u8);
        q12u16 = vmull_u8(d6u8, d0u8);
        q13u16 = vmull_u8(d8u8, d0u8);
        q14u16 = vmull_u8(d9u8, d0u8);

        d2u8  = vext_u8(d2u8, d3u8, 1);
        d5u8  = vext_u8(d5u8, d6u8, 1);
        d8u8  = vext_u8(d8u8, d9u8, 1);

        q9u16  = vmlal_u8(q9u16, d2u8, d1u8);
        q11u16 = vmlal_u8(q11u16, d5u8, d1u8);
        q13u16 = vmlal_u8(q13u16, d8u8, d1u8);

        d3u8  = vext_u8(d3u8, d4u8, 1);
        d6u8  = vext_u8(d6u8, d7u8, 1);
        d9u8  = vext_u8(d9u8, d10u8, 1);

        q10u16 = vmlal_u8(q10u16, d3u8, d1u8);
        q12u16 = vmlal_u8(q12u16, d6u8, d1u8);
        q14u16 = vmlal_u8(q14u16, d9u8, d1u8);

        q1u16 = vmull_u8(d11u8, d0u8);
        q2u16 = vmull_u8(d12u8, d0u8);
        q3u16 = vmull_u8(d14u8, d0u8);
        q4u16 = vmull_u8(d15u8, d0u8);

        d11u8 = vext_u8(d11u8, d12u8, 1);
        d14u8 = vext_u8(d14u8, d15u8, 1);

        q1u16 = vmlal_u8(q1u16, d11u8, d1u8);
        q3u16 = vmlal_u8(q3u16, d14u8, d1u8);

        d12u8 = vext_u8(d12u8, d13u8, 1);
        d15u8 = vext_u8(d15u8, d16u8, 1);

        q2u16 = vmlal_u8(q2u16, d12u8, d1u8);
        q4u16 = vmlal_u8(q4u16, d15u8, d1u8);

        d10u8 = vqrshrn_n_u16(q9u16, 7);
        d11u8 = vqrshrn_n_u16(q10u16, 7);
        d12u8 = vqrshrn_n_u16(q11u16, 7);
        d13u8 = vqrshrn_n_u16(q12u16, 7);
        d14u8 = vqrshrn_n_u16(q13u16, 7);
        d15u8 = vqrshrn_n_u16(q14u16, 7);
        d16u8 = vqrshrn_n_u16(q1u16, 7);
        d17u8 = vqrshrn_n_u16(q2u16, 7);
        d18u8 = vqrshrn_n_u16(q3u16, 7);
        d19u8 = vqrshrn_n_u16(q4u16, 7);

        q5u8 = vcombine_u8(d10u8, d11u8);
        q6u8 = vcombine_u8(d12u8, d13u8);
        q7u8 = vcombine_u8(d14u8, d15u8);
        q8u8 = vcombine_u8(d16u8, d17u8);
        q9u8 = vcombine_u8(d18u8, d19u8);

        vst1q_u8((uint8_t *)tmpp, q5u8);
        tmpp += 16;
        vst1q_u8((uint8_t *)tmpp, q6u8);
        tmpp += 16;
        vst1q_u8((uint8_t *)tmpp, q7u8);
        tmpp += 16;
        vst1q_u8((uint8_t *)tmpp, q8u8);
        tmpp += 16;
        vst1q_u8((uint8_t *)tmpp, q9u8);

        // secondpass_filter
        d0u8 = vdup_n_u8(bilinear_taps_coeff[yoffset][0]);
        d1u8 = vdup_n_u8(bilinear_taps_coeff[yoffset][1]);

        tmpp = tmp;
        q11u8 = vld1q_u8(tmpp);
        tmpp += 16;
        for (i = 4; i > 0; i--) {
            q12u8 = vld1q_u8(tmpp);
            tmpp += 16;
            q13u8 = vld1q_u8(tmpp);
            tmpp += 16;
            q14u8 = vld1q_u8(tmpp);
            tmpp += 16;
            q15u8 = vld1q_u8(tmpp);
            tmpp += 16;

            q1u16 = vmull_u8(vget_low_u8(q11u8), d0u8);
            q2u16 = vmull_u8(vget_high_u8(q11u8), d0u8);
            q3u16 = vmull_u8(vget_low_u8(q12u8), d0u8);
            q4u16 = vmull_u8(vget_high_u8(q12u8), d0u8);
            q5u16 = vmull_u8(vget_low_u8(q13u8), d0u8);
            q6u16 = vmull_u8(vget_high_u8(q13u8), d0u8);
            q7u16 = vmull_u8(vget_low_u8(q14u8), d0u8);
            q8u16 = vmull_u8(vget_high_u8(q14u8), d0u8);

            q1u16 = vmlal_u8(q1u16, vget_low_u8(q12u8), d1u8);
            q2u16 = vmlal_u8(q2u16, vget_high_u8(q12u8), d1u8);
            q3u16 = vmlal_u8(q3u16, vget_low_u8(q13u8), d1u8);
            q4u16 = vmlal_u8(q4u16, vget_high_u8(q13u8), d1u8);
            q5u16 = vmlal_u8(q5u16, vget_low_u8(q14u8), d1u8);
            q6u16 = vmlal_u8(q6u16, vget_high_u8(q14u8), d1u8);
            q7u16 = vmlal_u8(q7u16, vget_low_u8(q15u8), d1u8);
            q8u16 = vmlal_u8(q8u16, vget_high_u8(q15u8), d1u8);

            d2u8 = vqrshrn_n_u16(q1u16, 7);
            d3u8 = vqrshrn_n_u16(q2u16, 7);
            d4u8 = vqrshrn_n_u16(q3u16, 7);
            d5u8 = vqrshrn_n_u16(q4u16, 7);
            d6u8 = vqrshrn_n_u16(q5u16, 7);
            d7u8 = vqrshrn_n_u16(q6u16, 7);
            d8u8 = vqrshrn_n_u16(q7u16, 7);
            d9u8 = vqrshrn_n_u16(q8u16, 7);

            q1u8 = vcombine_u8(d2u8, d3u8);
            q2u8 = vcombine_u8(d4u8, d5u8);
            q3u8 = vcombine_u8(d6u8, d7u8);
            q4u8 = vcombine_u8(d8u8, d9u8);

            q11u8 = q15u8;

            vst1q_u8((uint8_t *)tmpp, q1u8);
            tmpp += 16;
            vst1q_u8((uint8_t *)tmpp, q2u8);
            tmpp += 16;
            vst1q_u8((uint8_t *)tmpp, q3u8);
            tmpp += 16;
            vst1q_u8((uint8_t *)tmpp, q4u8);
            tmpp += 16;
        }
    }

    // sub_pixel_variance16x16_neon
    q8s32 = vdupq_n_s32(0);
    q9s32 = vdupq_n_s32(0);
    q10s32 = vdupq_n_s32(0);

    tmpp = tmp;
    for (i = 0; i < 8; i++) {  // sub_pixel_variance16x16_neon_loop
        q0u8 = vld1q_u8(tmpp);
        tmpp += 16;
        q1u8 = vld1q_u8(tmpp);
        tmpp += 16;
        q2u8 = vld1q_u8(dst_ptr);
        dst_ptr += dst_pixels_per_line;
        q3u8 = vld1q_u8(dst_ptr);
        dst_ptr += dst_pixels_per_line;

        d0u8 = vget_low_u8(q0u8);
        d1u8 = vget_high_u8(q0u8);
        d2u8 = vget_low_u8(q1u8);
        d3u8 = vget_high_u8(q1u8);

        q11u16 = vsubl_u8(d0u8, vget_low_u8(q2u8));
        q12u16 = vsubl_u8(d1u8, vget_high_u8(q2u8));
        q13u16 = vsubl_u8(d2u8, vget_low_u8(q3u8));
        q14u16 = vsubl_u8(d3u8, vget_high_u8(q3u8));

        d22s16 = vreinterpret_s16_u16(vget_low_u16(q11u16));
        d23s16 = vreinterpret_s16_u16(vget_high_u16(q11u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q11u16));
        q9s32 = vmlal_s16(q9s32, d22s16, d22s16);
        q10s32 = vmlal_s16(q10s32, d23s16, d23s16);
        d24s16 = vreinterpret_s16_u16(vget_low_u16(q12u16));
        d25s16 = vreinterpret_s16_u16(vget_high_u16(q12u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q12u16));
        q9s32 = vmlal_s16(q9s32, d24s16, d24s16);
        q10s32 = vmlal_s16(q10s32, d25s16, d25s16);
        d26s16 = vreinterpret_s16_u16(vget_low_u16(q13u16));
        d27s16 = vreinterpret_s16_u16(vget_high_u16(q13u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q13u16));
        q9s32 = vmlal_s16(q9s32, d26s16, d26s16);
        q10s32 = vmlal_s16(q10s32, d27s16, d27s16);
        d28s16 = vreinterpret_s16_u16(vget_low_u16(q14u16));
        d29s16 = vreinterpret_s16_u16(vget_high_u16(q14u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q14u16));
        q9s32 = vmlal_s16(q9s32, d28s16, d28s16);
        q10s32 = vmlal_s16(q10s32, d29s16, d29s16);
    }

    q10s32 = vaddq_s32(q10s32, q9s32);
    q0s64 = vpaddlq_s32(q8s32);
    q1s64 = vpaddlq_s32(q10s32);

    d0s64 = vget_low_s64(q0s64);
    d1s64 = vget_high_s64(q0s64);
    d2s64 = vget_low_s64(q1s64);
    d3s64 = vget_high_s64(q1s64);
    d0s64 = vadd_s64(d0s64, d1s64);
    d1s64 = vadd_s64(d2s64, d3s64);

    q5s64 = vmull_s32(vreinterpret_s32_s64(d0s64),
                      vreinterpret_s32_s64(d0s64));
    vst1_lane_u32((uint32_t *)sse, vreinterpret_u32_s64(d1s64), 0);

    d10u32 = vshr_n_u32(vreinterpret_u32_s64(vget_low_s64(q5s64)), 8);
    d0u32 = vsub_u32(vreinterpret_u32_s64(d1s64), d10u32);

    return vget_lane_u32(d0u32, 0);
}

unsigned int vp8_variance_halfpixvar16x16_h_neon(
        const unsigned char *src_ptr,
        int  source_stride,
        const unsigned char *ref_ptr,
        int  recon_stride,
        unsigned int *sse) {
    int i;
    uint8x8_t d0u8, d1u8, d2u8, d3u8, d4u8, d5u8, d6u8, d7u8;
    int16x4_t d0s16, d1s16, d2s16, d3s16, d4s16, d5s16, d6s16, d7s16;
    int16x4_t d8s16, d9s16, d10s16, d11s16, d12s16, d13s16, d14s16, d15s16;
    uint32x2_t d0u32, d10u32;
    int64_t d0s64, d1s64, d2s64, d3s64;
    uint8x16_t q0u8, q1u8, q2u8, q3u8, q4u8, q5u8, q6u8;
    uint8x16_t q7u8, q11u8, q12u8, q13u8, q14u8;
    uint16x8_t q0u16, q1u16, q2u16, q3u16, q4u16, q5u16, q6u16, q7u16;
    int32x4_t q8s32, q9s32, q10s32;
    int64x2_t q0s64, q1s64, q5s64;

    q8s32 = vdupq_n_s32(0);
    q9s32 = vdupq_n_s32(0);
    q10s32 = vdupq_n_s32(0);

    for (i = 0; i < 4; i++) {  // vp8_filt_fpo16x16s_4_0_loop_neon
        q0u8 = vld1q_u8(src_ptr);
        q1u8 = vld1q_u8(src_ptr + 16);
        src_ptr += source_stride;
        q2u8 = vld1q_u8(src_ptr);
        q3u8 = vld1q_u8(src_ptr + 16);
        src_ptr += source_stride;
        q4u8 = vld1q_u8(src_ptr);
        q5u8 = vld1q_u8(src_ptr + 16);
        src_ptr += source_stride;
        q6u8 = vld1q_u8(src_ptr);
        q7u8 = vld1q_u8(src_ptr + 16);
        src_ptr += source_stride;

        q11u8 = vld1q_u8(ref_ptr);
        ref_ptr += recon_stride;
        q12u8 = vld1q_u8(ref_ptr);
        ref_ptr += recon_stride;
        q13u8 = vld1q_u8(ref_ptr);
        ref_ptr += recon_stride;
        q14u8 = vld1q_u8(ref_ptr);
        ref_ptr += recon_stride;

        q1u8 = vextq_u8(q0u8, q1u8, 1);
        q3u8 = vextq_u8(q2u8, q3u8, 1);
        q5u8 = vextq_u8(q4u8, q5u8, 1);
        q7u8 = vextq_u8(q6u8, q7u8, 1);

        q0u8 = vrhaddq_u8(q0u8, q1u8);
        q1u8 = vrhaddq_u8(q2u8, q3u8);
        q2u8 = vrhaddq_u8(q4u8, q5u8);
        q3u8 = vrhaddq_u8(q6u8, q7u8);

        d0u8 = vget_low_u8(q0u8);
        d1u8 = vget_high_u8(q0u8);
        d2u8 = vget_low_u8(q1u8);
        d3u8 = vget_high_u8(q1u8);
        d4u8 = vget_low_u8(q2u8);
        d5u8 = vget_high_u8(q2u8);
        d6u8 = vget_low_u8(q3u8);
        d7u8 = vget_high_u8(q3u8);

        q4u16 = vsubl_u8(d0u8, vget_low_u8(q11u8));
        q5u16 = vsubl_u8(d1u8, vget_high_u8(q11u8));
        q6u16 = vsubl_u8(d2u8, vget_low_u8(q12u8));
        q7u16 = vsubl_u8(d3u8, vget_high_u8(q12u8));
        q0u16 = vsubl_u8(d4u8, vget_low_u8(q13u8));
        q1u16 = vsubl_u8(d5u8, vget_high_u8(q13u8));
        q2u16 = vsubl_u8(d6u8, vget_low_u8(q14u8));
        q3u16 = vsubl_u8(d7u8, vget_high_u8(q14u8));

        d8s16 = vreinterpret_s16_u16(vget_low_u16(q4u16));
        d9s16 = vreinterpret_s16_u16(vget_high_u16(q4u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q4u16));
        q9s32 = vmlal_s16(q9s32, d8s16, d8s16);
        q10s32 = vmlal_s16(q10s32, d9s16, d9s16);
        d10s16 = vreinterpret_s16_u16(vget_low_u16(q5u16));
        d11s16 = vreinterpret_s16_u16(vget_high_u16(q5u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q5u16));
        q9s32 = vmlal_s16(q9s32, d10s16, d10s16);
        q10s32 = vmlal_s16(q10s32, d11s16, d11s16);
        d12s16 = vreinterpret_s16_u16(vget_low_u16(q6u16));
        d13s16 = vreinterpret_s16_u16(vget_high_u16(q6u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q6u16));
        q9s32 = vmlal_s16(q9s32, d12s16, d12s16);
        q10s32 = vmlal_s16(q10s32, d13s16, d13s16);
        d14s16 = vreinterpret_s16_u16(vget_low_u16(q7u16));
        d15s16 = vreinterpret_s16_u16(vget_high_u16(q7u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q7u16));
        q9s32 = vmlal_s16(q9s32, d14s16, d14s16);
        q10s32 = vmlal_s16(q10s32, d15s16, d15s16);
        d0s16 = vreinterpret_s16_u16(vget_low_u16(q0u16));
        d1s16 = vreinterpret_s16_u16(vget_high_u16(q0u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q0u16));
        q9s32 = vmlal_s16(q9s32, d0s16, d0s16);
        q10s32 = vmlal_s16(q10s32, d1s16, d1s16);
        d2s16 = vreinterpret_s16_u16(vget_low_u16(q1u16));
        d3s16 = vreinterpret_s16_u16(vget_high_u16(q1u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q1u16));
        q9s32 = vmlal_s16(q9s32, d2s16, d2s16);
        q10s32 = vmlal_s16(q10s32, d3s16, d3s16);
        d4s16 = vreinterpret_s16_u16(vget_low_u16(q2u16));
        d5s16 = vreinterpret_s16_u16(vget_high_u16(q2u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q2u16));
        q9s32 = vmlal_s16(q9s32, d4s16, d4s16);
        q10s32 = vmlal_s16(q10s32, d5s16, d5s16);
        d6s16 = vreinterpret_s16_u16(vget_low_u16(q3u16));
        d7s16 = vreinterpret_s16_u16(vget_high_u16(q3u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q3u16));
        q9s32 = vmlal_s16(q9s32, d6s16, d6s16);
        q10s32 = vmlal_s16(q10s32, d7s16, d7s16);
    }

    q10s32 = vaddq_s32(q10s32, q9s32);
    q0s64 = vpaddlq_s32(q8s32);
    q1s64 = vpaddlq_s32(q10s32);

    d0s64 = vget_low_s64(q0s64);
    d1s64 = vget_high_s64(q0s64);
    d2s64 = vget_low_s64(q1s64);
    d3s64 = vget_high_s64(q1s64);
    d0s64 = vadd_s64(d0s64, d1s64);
    d1s64 = vadd_s64(d2s64, d3s64);

    q5s64 = vmull_s32(vreinterpret_s32_s64(d0s64),
                      vreinterpret_s32_s64(d0s64));
    vst1_lane_u32((uint32_t *)sse, vreinterpret_u32_s64(d1s64), 0);

    d10u32 = vshr_n_u32(vreinterpret_u32_s64(vget_low_s64(q5s64)), 8);
    d0u32 = vsub_u32(vreinterpret_u32_s64(d1s64), d10u32);

    return vget_lane_u32(d0u32, 0);
}

unsigned int vp8_variance_halfpixvar16x16_v_neon(
        const unsigned char *src_ptr,
        int  source_stride,
        const unsigned char *ref_ptr,
        int  recon_stride,
        unsigned int *sse) {
    int i;
    uint8x8_t d0u8, d1u8, d4u8, d5u8, d8u8, d9u8, d12u8, d13u8;
    int16x4_t d22s16, d23s16, d24s16, d25s16, d26s16, d27s16, d28s16, d29s16;
    int16x4_t d0s16, d1s16, d2s16, d3s16, d4s16, d5s16, d6s16, d7s16;
    uint32x2_t d0u32, d10u32;
    int64_t d0s64, d1s64, d2s64, d3s64;
    uint8x16_t q0u8, q1u8, q2u8, q3u8, q4u8, q5u8, q6u8, q7u8, q15u8;
    uint16x8_t q0u16, q1u16, q2u16, q3u16, q11u16, q12u16, q13u16, q14u16;
    int32x4_t q8s32, q9s32, q10s32;
    int64x2_t q0s64, q1s64, q5s64;

    q8s32 = vdupq_n_s32(0);
    q9s32 = vdupq_n_s32(0);
    q10s32 = vdupq_n_s32(0);

    q0u8 = vld1q_u8(src_ptr);
    src_ptr += source_stride;
    for (i = 0; i < 4; i++) {  // vp8_filt_fpo16x16s_4_0_loop_neon
        q2u8 = vld1q_u8(src_ptr);
        src_ptr += source_stride;
        q4u8 = vld1q_u8(src_ptr);
        src_ptr += source_stride;
        q6u8 = vld1q_u8(src_ptr);
        src_ptr += source_stride;
        q15u8 = vld1q_u8(src_ptr);
        src_ptr += source_stride;

        q1u8 = vld1q_u8(ref_ptr);
        ref_ptr += recon_stride;
        q3u8 = vld1q_u8(ref_ptr);
        ref_ptr += recon_stride;
        q5u8 = vld1q_u8(ref_ptr);
        ref_ptr += recon_stride;
        q7u8 = vld1q_u8(ref_ptr);
        ref_ptr += recon_stride;

        q0u8 = vrhaddq_u8(q0u8, q2u8);
        q2u8 = vrhaddq_u8(q2u8, q4u8);
        q4u8 = vrhaddq_u8(q4u8, q6u8);
        q6u8 = vrhaddq_u8(q6u8, q15u8);

        d0u8  = vget_low_u8(q0u8);
        d1u8  = vget_high_u8(q0u8);
        d4u8  = vget_low_u8(q2u8);
        d5u8  = vget_high_u8(q2u8);
        d8u8  = vget_low_u8(q4u8);
        d9u8  = vget_high_u8(q4u8);
        d12u8 = vget_low_u8(q6u8);
        d13u8 = vget_high_u8(q6u8);

        q11u16 = vsubl_u8(d0u8, vget_low_u8(q1u8));
        q12u16 = vsubl_u8(d1u8, vget_high_u8(q1u8));
        q13u16 = vsubl_u8(d4u8, vget_low_u8(q3u8));
        q14u16 = vsubl_u8(d5u8, vget_high_u8(q3u8));
        q0u16  = vsubl_u8(d8u8, vget_low_u8(q5u8));
        q1u16  = vsubl_u8(d9u8, vget_high_u8(q5u8));
        q2u16  = vsubl_u8(d12u8, vget_low_u8(q7u8));
        q3u16  = vsubl_u8(d13u8, vget_high_u8(q7u8));

        d22s16 = vreinterpret_s16_u16(vget_low_u16(q11u16));
        d23s16 = vreinterpret_s16_u16(vget_high_u16(q11u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q11u16));
        q9s32 = vmlal_s16(q9s32, d22s16, d22s16);
        q10s32 = vmlal_s16(q10s32, d23s16, d23s16);
        d24s16 = vreinterpret_s16_u16(vget_low_u16(q12u16));
        d25s16 = vreinterpret_s16_u16(vget_high_u16(q12u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q12u16));
        q9s32 = vmlal_s16(q9s32, d24s16, d24s16);
        q10s32 = vmlal_s16(q10s32, d25s16, d25s16);
        d26s16 = vreinterpret_s16_u16(vget_low_u16(q13u16));
        d27s16 = vreinterpret_s16_u16(vget_high_u16(q13u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q13u16));
        q9s32 = vmlal_s16(q9s32, d26s16, d26s16);
        q10s32 = vmlal_s16(q10s32, d27s16, d27s16);
        d28s16 = vreinterpret_s16_u16(vget_low_u16(q14u16));
        d29s16 = vreinterpret_s16_u16(vget_high_u16(q14u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q14u16));
        q9s32 = vmlal_s16(q9s32, d28s16, d28s16);
        q10s32 = vmlal_s16(q10s32, d29s16, d29s16);
        d0s16 = vreinterpret_s16_u16(vget_low_u16(q0u16));
        d1s16 = vreinterpret_s16_u16(vget_high_u16(q0u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q0u16));
        q9s32 = vmlal_s16(q9s32, d0s16, d0s16);
        q10s32 = vmlal_s16(q10s32, d1s16, d1s16);
        d2s16 = vreinterpret_s16_u16(vget_low_u16(q1u16));
        d3s16 = vreinterpret_s16_u16(vget_high_u16(q1u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q1u16));
        q9s32 = vmlal_s16(q9s32, d2s16, d2s16);
        q10s32 = vmlal_s16(q10s32, d3s16, d3s16);
        d4s16 = vreinterpret_s16_u16(vget_low_u16(q2u16));
        d5s16 = vreinterpret_s16_u16(vget_high_u16(q2u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q2u16));
        q9s32 = vmlal_s16(q9s32, d4s16, d4s16);
        q10s32 = vmlal_s16(q10s32, d5s16, d5s16);
        d6s16 = vreinterpret_s16_u16(vget_low_u16(q3u16));
        d7s16 = vreinterpret_s16_u16(vget_high_u16(q3u16));
        q8s32 = vpadalq_s16(q8s32, vreinterpretq_s16_u16(q3u16));
        q9s32 = vmlal_s16(q9s32, d6s16, d6s16);
        q10s32 = vmlal_s16(q10s32, d7s16, d7s16);

        q0u8 = q15u8;
    }

    q10s32 = vaddq_s32(q10s32, q9s32);
    q0s64 = vpaddlq_s32(q8s32);
    q1s64 = vpaddlq_s32(q10s32);

    d0s64 = vget_low_s64(q0s64);
    d1s64 = vget_high_s64(q0s64);
    d2s64 = vget_low_s64(q1s64);
    d3s64 = vget_high_s64(q1s64);
    d0s64 = vadd_s64(d0s64, d1s64);
    d1s64 = vadd_s64(d2s64, d3s64);

    q5s64 = vmull_s32(vreinterpret_s32_s64(d0s64),
                      vreinterpret_s32_s64(d0s64));
    vst1_lane_u32((uint32_t *)sse, vreinterpret_u32_s64(d1s64), 0);

    d10u32 = vshr_n_u32(vreinterpret_u32_s64(vget_low_s64(q5s64)), 8);
    d0u32 = vsub_u32(vreinterpret_u32_s64(d1s64), d10u32);

    return vget_lane_u32(d0u32, 0);
}

unsigned int vp8_variance_halfpixvar16x16_hv_neon(
        const unsigned char *src_ptr,
        int  source_stride,
        const unsigned char *ref_ptr,
        int  recon_stride,
        unsigned int *sse) {
    int i;
    uint8x8_t d0u8, d1u8, d2u8, d3u8, d4u8, d5u8, d6u8, d7u8;
    int16x4_t d0s16, d1s16, d2s16, d3s16, d10s16, d11s16, d12s16, d13s16;
    int16x4_t d18s16, d19s16, d20s16, d21s16, d22s16, d23s16, d24s16, d25s16;
    uint32x2_t d0u32, d10u32;
    int64_t d0s64, d1s64, d2s64, d3s64;
    uint8x16_t q0u8, q1u8, q2u8, q3u8, q4u8, q5u8, q6u8, q7u8, q8u8, q9u8;
    uint16x8_t q0u16, q1u16, q5u16, q6u16, q9u16, q10u16, q11u16, q12u16;
    int32x4_t q13s32, q14s32, q15s32;
    int64x2_t q0s64, q1s64, q5s64;

    q13s32 = vdupq_n_s32(0);
    q14s32 = vdupq_n_s32(0);
    q15s32 = vdupq_n_s32(0);

    q0u8 = vld1q_u8(src_ptr);
    q1u8 = vld1q_u8(src_ptr + 16);
    src_ptr += source_stride;
    q1u8 = vextq_u8(q0u8, q1u8, 1);
    q0u8 = vrhaddq_u8(q0u8, q1u8);
    for (i = 0; i < 4; i++) {  // vp8_filt_fpo16x16s_4_0_loop_neon
        q2u8 = vld1q_u8(src_ptr);
        q3u8 = vld1q_u8(src_ptr + 16);
        src_ptr += source_stride;
        q4u8 = vld1q_u8(src_ptr);
        q5u8 = vld1q_u8(src_ptr + 16);
        src_ptr += source_stride;
        q6u8 = vld1q_u8(src_ptr);
        q7u8 = vld1q_u8(src_ptr + 16);
        src_ptr += source_stride;
        q8u8 = vld1q_u8(src_ptr);
        q9u8 = vld1q_u8(src_ptr + 16);
        src_ptr += source_stride;

        q3u8 = vextq_u8(q2u8, q3u8, 1);
        q5u8 = vextq_u8(q4u8, q5u8, 1);
        q7u8 = vextq_u8(q6u8, q7u8, 1);
        q9u8 = vextq_u8(q8u8, q9u8, 1);

        q1u8 = vrhaddq_u8(q2u8, q3u8);
        q2u8 = vrhaddq_u8(q4u8, q5u8);
        q3u8 = vrhaddq_u8(q6u8, q7u8);
        q4u8 = vrhaddq_u8(q8u8, q9u8);
        q0u8 = vrhaddq_u8(q0u8, q1u8);
        q1u8 = vrhaddq_u8(q1u8, q2u8);
        q2u8 = vrhaddq_u8(q2u8, q3u8);
        q3u8 = vrhaddq_u8(q3u8, q4u8);

        q5u8 = vld1q_u8(ref_ptr);
        ref_ptr += recon_stride;
        q6u8 = vld1q_u8(ref_ptr);
        ref_ptr += recon_stride;
        q5u8 = vld1q_u8(ref_ptr);
        ref_ptr += recon_stride;
        q7u8 = vld1q_u8(ref_ptr);
        ref_ptr += recon_stride;

        d0u8 = vget_low_u8(q0u8);
        d1u8 = vget_high_u8(q0u8);
        d2u8 = vget_low_u8(q1u8);
        d3u8 = vget_high_u8(q1u8);
        d4u8 = vget_low_u8(q2u8);
        d5u8 = vget_high_u8(q2u8);
        d6u8 = vget_low_u8(q3u8);
        d7u8 = vget_high_u8(q3u8);

        q9u16  = vsubl_u8(d0u8, vget_low_u8(q5u8));
        q10u16 = vsubl_u8(d1u8, vget_high_u8(q5u8));
        q11u16 = vsubl_u8(d2u8, vget_low_u8(q6u8));
        q12u16 = vsubl_u8(d3u8, vget_high_u8(q6u8));
        q0u16  = vsubl_u8(d4u8, vget_low_u8(q7u8));
        q1u16  = vsubl_u8(d5u8, vget_high_u8(q7u8));
        q5u16  = vsubl_u8(d6u8, vget_low_u8(q8u8));
        q6u16  = vsubl_u8(d7u8, vget_high_u8(q8u8));

        d18s16 = vreinterpret_s16_u16(vget_low_u16(q9u16));
        d19s16 = vreinterpret_s16_u16(vget_high_u16(q9u16));
        q13s32 = vpadalq_s16(q13s32, vreinterpretq_s16_u16(q9u16));
        q14s32 = vmlal_s16(q14s32, d18s16, d18s16);
        q15s32 = vmlal_s16(q15s32, d19s16, d19s16);
        d20s16 = vreinterpret_s16_u16(vget_low_u16(q10u16));
        d21s16 = vreinterpret_s16_u16(vget_high_u16(q10u16));
        q13s32 = vpadalq_s16(q13s32, vreinterpretq_s16_u16(q10u16));
        q14s32 = vmlal_s16(q14s32, d20s16, d20s16);
        q15s32 = vmlal_s16(q15s32, d21s16, d21s16);
        d22s16 = vreinterpret_s16_u16(vget_low_u16(q11u16));
        d23s16 = vreinterpret_s16_u16(vget_high_u16(q11u16));
        q13s32 = vpadalq_s16(q13s32, vreinterpretq_s16_u16(q11u16));
        q14s32 = vmlal_s16(q14s32, d22s16, d22s16);
        q15s32 = vmlal_s16(q15s32, d23s16, d23s16);
        d24s16 = vreinterpret_s16_u16(vget_low_u16(q12u16));
        d25s16 = vreinterpret_s16_u16(vget_high_u16(q12u16));
        q13s32 = vpadalq_s16(q13s32, vreinterpretq_s16_u16(q12u16));
        q14s32 = vmlal_s16(q14s32, d24s16, d24s16);
        q15s32 = vmlal_s16(q15s32, d25s16, d25s16);
        d0s16 = vreinterpret_s16_u16(vget_low_u16(q0u16));
        d1s16 = vreinterpret_s16_u16(vget_high_u16(q0u16));
        q13s32 = vpadalq_s16(q13s32, vreinterpretq_s16_u16(q0u16));
        q14s32 = vmlal_s16(q14s32, d0s16, d0s16);
        q15s32 = vmlal_s16(q15s32, d1s16, d1s16);
        d2s16 = vreinterpret_s16_u16(vget_low_u16(q1u16));
        d3s16 = vreinterpret_s16_u16(vget_high_u16(q1u16));
        q13s32 = vpadalq_s16(q13s32, vreinterpretq_s16_u16(q1u16));
        q14s32 = vmlal_s16(q14s32, d2s16, d2s16);
        q15s32 = vmlal_s16(q15s32, d3s16, d3s16);
        d10s16 = vreinterpret_s16_u16(vget_low_u16(q5u16));
        d11s16 = vreinterpret_s16_u16(vget_high_u16(q5u16));
        q13s32 = vpadalq_s16(q13s32, vreinterpretq_s16_u16(q5u16));
        q14s32 = vmlal_s16(q14s32, d10s16, d10s16);
        q15s32 = vmlal_s16(q15s32, d11s16, d11s16);
        d12s16 = vreinterpret_s16_u16(vget_low_u16(q6u16));
        d13s16 = vreinterpret_s16_u16(vget_high_u16(q6u16));
        q13s32 = vpadalq_s16(q13s32, vreinterpretq_s16_u16(q6u16));
        q14s32 = vmlal_s16(q14s32, d12s16, d12s16);
        q15s32 = vmlal_s16(q15s32, d13s16, d13s16);

        q0u8 = q4u8;
    }

    q15s32 = vaddq_s32(q14s32, q15s32);
    q0s64 = vpaddlq_s32(q13s32);
    q1s64 = vpaddlq_s32(q15s32);

    d0s64 = vget_low_s64(q0s64);
    d1s64 = vget_high_s64(q0s64);
    d2s64 = vget_low_s64(q1s64);
    d3s64 = vget_high_s64(q1s64);
    d0s64 = vadd_s64(d0s64, d1s64);
    d1s64 = vadd_s64(d2s64, d3s64);

    q5s64 = vmull_s32(vreinterpret_s32_s64(d0s64),
                      vreinterpret_s32_s64(d0s64));
    vst1_lane_u32((uint32_t *)sse, vreinterpret_u32_s64(d1s64), 0);

    d10u32 = vshr_n_u32(vreinterpret_u32_s64(vget_low_s64(q5s64)), 8);
    d0u32 = vsub_u32(vreinterpret_u32_s64(d1s64), d10u32);

    return vget_lane_u32(d0u32, 0);
}

// Can't test functions
unsigned int vp8_sub_pixel_variance8x8_neon(
        const unsigned char *src_ptr,
        int src_pixels_per_line,
        int xoffset,
        int yoffset,
        const unsigned char *dst_ptr,
        int dst_pixels_per_line,
        unsigned int *sse) {
  //   printf("[%s:%d]\n", __func__, __LINE__);
    return 0;
}

unsigned int vp8_sub_pixel_variance16x16s_neon(
        const unsigned char *src_ptr,
        int src_pixels_per_line,
        int xoffset,
        int yoffset,
        const unsigned char *dst_ptr,
        int dst_pixels_per_line,
        unsigned int *sse) {
    // printf("[%s:%d]\n", __func__, __LINE__);
    return 0;
}
