/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef VP9_ENCODER_VP9_RATECTRL_H_
#define VP9_ENCODER_VP9_RATECTRL_H_

#include "vp9/encoder/vp9_onyx_int.h"

#define FRAME_OVERHEAD_BITS 200

void vp9_save_coding_context(VP9_COMP *cpi);
void vp9_restore_coding_context(VP9_COMP *cpi);

void vp9_setup_key_frame(VP9_COMP *cpi);
void vp9_setup_inter_frame(VP9_COMP *cpi);

double vp9_convert_qindex_to_q(int qindex);

// Updates rate correction factors
void vp9_rc_update_rate_correction_factors(VP9_COMP *cpi, int damp_var);

// initialize luts for minq
void vp9_rc_init_minq_luts(void);

// return of 0 means drop frame
// Changes rc.this_frame_target and rc.sb64_rate_target
int vp9_rc_pick_frame_size_and_bounds(VP9_COMP *cpi,
                                      int *frame_under_shoot_limit,
                                      int *frame_over_shoot_limit);
// Picks q and q bounds given the target for bits
int vp9_rc_pick_q_and_adjust_q_bounds(VP9_COMP *cpi,
                                      int * bottom_index,
                                      int * top_index);

// Estimates q to achieve a target bits per frame
int vp9_rc_regulate_q(const VP9_COMP *cpi, int target_bits_per_frame);

// Post encode update of the rate control parameters based
// on bytes used and q used for the frame
void vp9_rc_postencode_update(VP9_COMP *cpi,
                              uint64_t bytes_used,
                              int q_used);

// estimates bits per mb for a given qindex and correction factor
int vp9_rc_bits_per_mb(FRAME_TYPE frame_type, int qindex,
                       double correction_factor);

#endif  // VP9_ENCODER_VP9_RATECTRL_H_
