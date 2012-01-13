/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef __INC_ENCODEMB_H
#define __INC_ENCODEMB_H

#include "onyx_int.h"
struct VP8_ENCODER_RTCD;
void vp8_encode_inter16x16(const struct VP8_ENCODER_RTCD *rtcd, MACROBLOCK *x);

void vp8_build_dcblock(MACROBLOCK *b);
void vp8_transform_mb(MACROBLOCK *mb);
void vp8_transform_mbuv(MACROBLOCK *x);
void vp8_transform_intra_mby(MACROBLOCK *x);

void vp8_optimize_mby(MACROBLOCK *x, const struct VP8_ENCODER_RTCD *rtcd);
void vp8_optimize_mbuv(MACROBLOCK *x, const struct VP8_ENCODER_RTCD *rtcd);
void vp8_encode_inter16x16y(const struct VP8_ENCODER_RTCD *rtcd, MACROBLOCK *x);
#endif
