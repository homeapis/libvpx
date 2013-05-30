/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef VP9_DECODER_VP9_DECODFRAME_H_
#define VP9_DECODER_VP9_DECODFRAME_H_

struct VP9Common;
struct VP9Decompressor;

void vp9_init_dequantizer(struct VP9Common *pc);
int vp9_decode_frame(struct VP9Decompressor *cpi, const uint8_t **p_data_end);

#define COUNT_COEFF_BOOL_DECODES

#ifdef COUNT_COEFF_BOOL_DECODES
extern int64_t num_bool_decodes;
#endif

#endif  // VP9_DECODER_VP9_DECODFRAME_H_
