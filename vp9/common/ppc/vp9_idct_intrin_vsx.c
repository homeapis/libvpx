/*
 *  Copyright (c) 2018 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <assert.h>

#include "vpx_dsp/vpx_dsp_common.h"
#include "vpx_dsp/ppc/inv_txfm_vsx.h"
#include "vpx_dsp/ppc/bitdepth_conversion_vsx.h"

#include "../vp9_enums.h"

void vp9_iht4x4_16_add_vsx(const tran_low_t *input, uint8_t *dest, int stride,
                           int tx_type) {
  int16x8_t in[2], out[2];

  in[0] = load_tran_low(0, input);
  in[1] = load_tran_low(8 * sizeof(*input), input);

  switch (tx_type) {
    case DCT_DCT:
      idct4_vsx(in, out);
      idct4_vsx(out, in);
      break;
    case ADST_DCT:
      idct4_vsx(in, out);
      iadst4_vsx(out, in);
      break;
    case DCT_ADST:
      iadst4_vsx(in, out);
      idct4_vsx(out, in);
      break;
    default:
      assert(tx_type == ADST_ADST);
      iadst4_vsx(in, out);
      iadst4_vsx(out, in);
      break;
  }

  round_store4x4_vsx(in, out, dest, stride);
}

void vp9_iht8x8_64_add_vsx(const tran_low_t *input, uint8_t *dest, int stride,
                            int tx_type) {
  int16x8_t in[8], out[8];

  // load input data
  in[0] = load_tran_low(0, input);
  in[1] = load_tran_low(8 * sizeof(*input), input);
  in[2] = load_tran_low(2 * 8 * sizeof(*input), input);
  in[3] = load_tran_low(3 * 8 * sizeof(*input), input);
  in[4] = load_tran_low(4 * 8 * sizeof(*input), input);
  in[5] = load_tran_low(5 * 8 * sizeof(*input), input);
  in[6] = load_tran_low(6 * 8 * sizeof(*input), input);
  in[7] = load_tran_low(7 * 8 * sizeof(*input), input);

  switch (tx_type) {
    case DCT_DCT:
      idct8_vsx(in, out);
      idct8_vsx(out, in);
      break;
    case ADST_DCT:
      idct8_vsx(in, out);
      iadst8_vsx(out, in);
      break;
    case DCT_ADST:
      iadst8_vsx(in, out);
      idct8_vsx(out, in);
      break;
    default:
      assert(tx_type == ADST_ADST);
      iadst8_vsx(in, out);
      iadst8_vsx(out, in);
      break;
  }

  round_store8x8_vsx(in, dest, stride);
}
