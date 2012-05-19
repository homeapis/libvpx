/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VP8_ENCODER_DENOISING_H_
#define VP8_ENCODER_DENOISING_H_

#include "block.h"
static const unsigned int NOISE_MOTION_THRESHOLD = 20 * 20;
static const unsigned int NOISE_DIFF2_THRESHOLD = 75;
// SSE_DIFF_THRESHOLD is selected as ~95% confidence assuming var(noise) ~= 100.
static const unsigned int SSE_DIFF_THRESHOLD = 16 * 16 * 20;
static const unsigned int SSE_THRESHOLD = 16 * 16 * 40;

typedef struct vp8_denoiser
{
  YV12_BUFFER_CONFIG yv12_running_avg;
  YV12_BUFFER_CONFIG yv12_mc_running_avg;
} VP8_DENOISER;

int vp8_denoiser_allocate(VP8_DENOISER *denoiser, int width, int height);

void vp8_denoiser_free(VP8_DENOISER *denoiser);

void vp8_denoiser_denoise_mb(VP8_DENOISER *denoiser,
                             MACROBLOCK *x,
                             unsigned int best_sse,
                             unsigned int zero_mv_sse,
                             int recon_yoffset,
                             int recon_uvoffset);

union coeff_pair
{
    uint32_t as_int;
    uint16_t as_short[2];
};

union coeff_pair *vp8_get_filter_coeff_LUT(unsigned int motion_magnitude);

#endif  // VP8_ENCODER_DENOISING_H_
