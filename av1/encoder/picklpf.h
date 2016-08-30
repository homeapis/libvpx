/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AV1_ENCODER_PICKLPF_H_
#define AV1_ENCODER_PICKLPF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "av1/encoder/encoder.h"

struct yv12_buffer_config;
struct AV1_COMP;
int av1_get_max_filter_level(const AV1_COMP *cpi);
int av1_search_filter_level(const YV12_BUFFER_CONFIG *sd, AV1_COMP *cpi,
                            int partial_frame, double *err);
void av1_pick_filter_level(const struct yv12_buffer_config *sd,
                           struct AV1_COMP *cpi, LPF_PICK_METHOD method);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AV1_ENCODER_PICKLPF_H_
