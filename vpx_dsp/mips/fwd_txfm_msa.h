/*
 *  Copyright (c) 2015 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "vpx_dsp/mips/txfm_macros_msa.h"
#include "vp9/common/vp9_idct.h"

#define VP9_FDCT4(in0, in1, in2, in3, out0, out1, out2, out3) {     \
  v8i16 cnst0_m, cnst1_m, cnst2_m, cnst3_m;                         \
  v8i16 vec0_m, vec1_m, vec2_m, vec3_m;                             \
  v4i32 vec4_m, vec5_m, vec6_m, vec7_m;                             \
  v8i16 coeff_m = { cospi_16_64, -cospi_16_64, cospi_8_64,          \
                    cospi_24_64, -cospi_8_64, 0, 0, 0 };            \
                                                                    \
  BUTTERFLY_4(in0, in1, in2, in3, vec0_m, vec1_m, vec2_m, vec3_m);  \
  ILVR_H2_SH(vec1_m, vec0_m, vec3_m, vec2_m, vec0_m, vec2_m);       \
  SPLATI_H2_SH(coeff_m, 0, 1, cnst0_m, cnst1_m);                    \
  cnst1_m = __msa_ilvev_h(cnst1_m, cnst0_m);                        \
  vec5_m = __msa_dotp_s_w(vec0_m, cnst1_m);                         \
                                                                    \
  SPLATI_H2_SH(coeff_m, 4, 3, cnst2_m, cnst3_m);                    \
  cnst2_m = __msa_ilvev_h(cnst3_m, cnst2_m);                        \
  vec7_m = __msa_dotp_s_w(vec2_m, cnst2_m);                         \
                                                                    \
  vec4_m = __msa_dotp_s_w(vec0_m, cnst0_m);                         \
  cnst2_m = __msa_splati_h(coeff_m, 2);                             \
  cnst2_m = __msa_ilvev_h(cnst2_m, cnst3_m);                        \
  vec6_m = __msa_dotp_s_w(vec2_m, cnst2_m);                         \
                                                                    \
  SRARI_W4_SW(vec4_m, vec5_m, vec6_m, vec7_m, DCT_CONST_BITS);      \
  PCKEV_H4_SH(vec4_m, vec4_m, vec5_m, vec5_m, vec6_m, vec6_m,       \
              vec7_m, vec7_m, out0, out2, out1, out3);              \
}

#define VP9_FDCT8(in0, in1, in2, in3, in4, in5, in6, in7,            \
                  out0, out1, out2, out3, out4, out5, out6, out7) {  \
  v8i16 s0_m, s1_m, s2_m, s3_m, s4_m, s5_m, s6_m;                    \
  v8i16 s7_m, x0_m, x1_m, x2_m, x3_m;                                \
  v8i16 coeff_m = { cospi_16_64, -cospi_16_64, cospi_8_64,           \
                    cospi_24_64, cospi_4_64, cospi_28_64,            \
                    cospi_12_64, cospi_20_64 };                      \
                                                                     \
  /* FDCT stage1 */                                                  \
  BUTTERFLY_8(in0, in1, in2, in3, in4, in5, in6, in7,                \
              s0_m, s1_m, s2_m, s3_m, s4_m, s5_m, s6_m, s7_m);       \
  BUTTERFLY_4(s0_m, s1_m, s2_m, s3_m, x0_m, x1_m, x2_m, x3_m);       \
  ILVL_H2_SH(x1_m, x0_m, x3_m, x2_m, s0_m, s2_m);                    \
  ILVR_H2_SH(x1_m, x0_m, x3_m, x2_m, s1_m, s3_m);                    \
  SPLATI_H2_SH(coeff_m, 0, 1, x0_m, x1_m);                           \
  x1_m = __msa_ilvev_h(x1_m, x0_m);                                  \
  out4 = DOT_SHIFT_RIGHT_PCK_H(s0_m, s1_m, x1_m);                \
                                                                     \
  SPLATI_H2_SH(coeff_m, 2, 3, x2_m, x3_m);                           \
  x2_m = -x2_m;                                                      \
  x2_m = __msa_ilvev_h(x3_m, x2_m);                                  \
  out6 = DOT_SHIFT_RIGHT_PCK_H(s2_m, s3_m, x2_m);                \
                                                                     \
  out0 = DOT_SHIFT_RIGHT_PCK_H(s0_m, s1_m, x0_m);                \
  x2_m = __msa_splati_h(coeff_m, 2);                                 \
  x2_m = __msa_ilvev_h(x2_m, x3_m);                                  \
  out2 = DOT_SHIFT_RIGHT_PCK_H(s2_m, s3_m, x2_m);                \
                                                                     \
  /* stage2 */                                                       \
  ILVRL_H2_SH(s5_m, s6_m, s1_m, s0_m);                               \
                                                                     \
  s6_m = DOT_SHIFT_RIGHT_PCK_H(s0_m, s1_m, x0_m);                \
  s5_m = DOT_SHIFT_RIGHT_PCK_H(s0_m, s1_m, x1_m);                \
                                                                     \
  /* stage3 */                                                       \
  BUTTERFLY_4(s4_m, s7_m, s6_m, s5_m, x0_m, x3_m, x2_m, x1_m);       \
                                                                     \
  /* stage4 */                                                       \
  ILVL_H2_SH(x3_m, x0_m, x2_m, x1_m, s4_m, s6_m);                    \
  ILVR_H2_SH(x3_m, x0_m, x2_m, x1_m, s5_m, s7_m);                    \
                                                                     \
  SPLATI_H2_SH(coeff_m, 4, 5, x0_m, x1_m);                           \
  x1_m = __msa_ilvev_h(x0_m, x1_m);                                  \
  out1 = DOT_SHIFT_RIGHT_PCK_H(s4_m, s5_m, x1_m);                \
                                                                     \
  SPLATI_H2_SH(coeff_m, 6, 7, x2_m, x3_m);                           \
  x2_m = __msa_ilvev_h(x3_m, x2_m);                                  \
  out5 = DOT_SHIFT_RIGHT_PCK_H(s6_m, s7_m, x2_m);                \
                                                                     \
  x1_m = __msa_splati_h(coeff_m, 5);                                 \
  x0_m = -x0_m;                                                      \
  x0_m = __msa_ilvev_h(x1_m, x0_m);                                  \
  out7 = DOT_SHIFT_RIGHT_PCK_H(s4_m, s5_m, x0_m);                \
                                                                     \
  x2_m = __msa_splati_h(coeff_m, 6);                                 \
  x3_m = -x3_m;                                                      \
  x2_m = __msa_ilvev_h(x2_m, x3_m);                                  \
  out3 = DOT_SHIFT_RIGHT_PCK_H(s6_m, s7_m, x2_m);                \
}

#define FDCT8x16_EVEN(in0, in1, in2, in3, in4, in5, in6, in7,            \
                      out0, out1, out2, out3, out4, out5, out6, out7) {  \
  v8i16 s0_m, s1_m, s2_m, s3_m, s4_m, s5_m, s6_m, s7_m;                      \
  v8i16 x0_m, x1_m, x2_m, x3_m;                                              \
  v8i16 coeff_m = { cospi_16_64, -cospi_16_64, cospi_8_64, cospi_24_64,      \
                    cospi_4_64, cospi_28_64, cospi_12_64, cospi_20_64 };     \
                                                                             \
  /* FDCT stage1 */                                                          \
  BUTTERFLY_8(in0, in1, in2, in3, in4, in5, in6, in7,                        \
              s0_m, s1_m, s2_m, s3_m, s4_m, s5_m, s6_m, s7_m);               \
  BUTTERFLY_4(s0_m, s1_m, s2_m, s3_m, x0_m, x1_m, x2_m, x3_m);               \
  ILVL_H2_SH(x1_m, x0_m, x3_m, x2_m, s0_m, s2_m);                            \
  ILVR_H2_SH(x1_m, x0_m, x3_m, x2_m, s1_m, s3_m);                            \
  SPLATI_H2_SH(coeff_m, 0, 1, x0_m, x1_m);                                   \
  x1_m = __msa_ilvev_h(x1_m, x0_m);                                          \
  out4 = DOT_SHIFT_RIGHT_PCK_H(s0_m, s1_m, x1_m);                        \
                                                                             \
  SPLATI_H2_SH(coeff_m, 2, 3, x2_m, x3_m);                                   \
  x2_m = -x2_m;                                                              \
  x2_m = __msa_ilvev_h(x3_m, x2_m);                                          \
  out6 = DOT_SHIFT_RIGHT_PCK_H(s2_m, s3_m, x2_m);                        \
                                                                             \
  out0 = DOT_SHIFT_RIGHT_PCK_H(s0_m, s1_m, x0_m);                        \
  x2_m = __msa_splati_h(coeff_m, 2);                                         \
  x2_m = __msa_ilvev_h(x2_m, x3_m);                                          \
  out2 = DOT_SHIFT_RIGHT_PCK_H(s2_m, s3_m, x2_m);                        \
                                                                             \
  /* stage2 */                                                               \
  ILVRL_H2_SH(s5_m, s6_m, s1_m, s0_m);                                       \
                                                                             \
  s6_m = DOT_SHIFT_RIGHT_PCK_H(s0_m, s1_m, x0_m);                        \
  s5_m = DOT_SHIFT_RIGHT_PCK_H(s0_m, s1_m, x1_m);                        \
                                                                             \
  /* stage3 */                                                               \
  BUTTERFLY_4(s4_m, s7_m, s6_m, s5_m, x0_m, x3_m, x2_m, x1_m);               \
                                                                             \
  /* stage4 */                                                               \
  ILVL_H2_SH(x3_m, x0_m, x2_m, x1_m, s4_m, s6_m);                            \
  ILVR_H2_SH(x3_m, x0_m, x2_m, x1_m, s5_m, s7_m);                            \
                                                                             \
  SPLATI_H2_SH(coeff_m, 4, 5, x0_m, x1_m);                                   \
  x1_m = __msa_ilvev_h(x0_m, x1_m);                                          \
  out1 = DOT_SHIFT_RIGHT_PCK_H(s4_m, s5_m, x1_m);                        \
                                                                             \
  SPLATI_H2_SH(coeff_m, 6, 7, x2_m, x3_m);                                   \
  x2_m = __msa_ilvev_h(x3_m, x2_m);                                          \
  out5 = DOT_SHIFT_RIGHT_PCK_H(s6_m, s7_m, x2_m);                        \
                                                                             \
  x1_m = __msa_splati_h(coeff_m, 5);                                         \
  x0_m = -x0_m;                                                              \
  x0_m = __msa_ilvev_h(x1_m, x0_m);                                          \
  out7 = DOT_SHIFT_RIGHT_PCK_H(s4_m, s5_m, x0_m);                        \
                                                                             \
  x2_m = __msa_splati_h(coeff_m, 6);                                         \
  x3_m = -x3_m;                                                              \
  x2_m = __msa_ilvev_h(x2_m, x3_m);                                          \
  out3 = DOT_SHIFT_RIGHT_PCK_H(s6_m, s7_m, x2_m);                        \
}

#define FDCT8x16_ODD(input0, input1, input2, input3,           \
                     input4, input5, input6, input7,           \
                     out1, out3, out5, out7,                   \
                     out9, out11, out13, out15) {              \
  v8i16 stp21_m, stp22_m, stp23_m, stp24_m, stp25_m, stp26_m;      \
  v8i16 stp30_m, stp31_m, stp32_m, stp33_m, stp34_m, stp35_m;      \
  v8i16 stp36_m, stp37_m, vec0_m, vec1_m;                          \
  v8i16 vec2_m, vec3_m, vec4_m, vec5_m, vec6_m;                    \
  v8i16 cnst0_m, cnst1_m, cnst4_m, cnst5_m;                        \
  v8i16 coeff_m = { cospi_16_64, -cospi_16_64, cospi_8_64,         \
                    cospi_24_64, -cospi_8_64, -cospi_24_64,        \
                    cospi_12_64, cospi_20_64 };                    \
  v8i16 coeff1_m = { cospi_2_64, cospi_30_64, cospi_14_64,         \
                     cospi_18_64, cospi_10_64, cospi_22_64,        \
                     cospi_6_64, cospi_26_64 };                    \
  v8i16 coeff2_m = { -cospi_2_64, -cospi_10_64, -cospi_18_64,      \
                     -cospi_26_64, 0, 0, 0, 0 };                   \
                                                                   \
  /* stp 1 */                                                      \
  ILVL_H2_SH(input2, input5, input3, input4, vec2_m, vec4_m);      \
  ILVR_H2_SH(input2, input5, input3, input4, vec3_m, vec5_m);      \
                                                                   \
  cnst4_m = __msa_splati_h(coeff_m, 0);                            \
  stp25_m = DOT_SHIFT_RIGHT_PCK_H(vec2_m, vec3_m, cnst4_m);    \
                                                                   \
  cnst5_m = __msa_splati_h(coeff_m, 1);                            \
  cnst5_m = __msa_ilvev_h(cnst5_m, cnst4_m);                       \
  stp22_m = DOT_SHIFT_RIGHT_PCK_H(vec2_m, vec3_m, cnst5_m);    \
  stp24_m = DOT_SHIFT_RIGHT_PCK_H(vec4_m, vec5_m, cnst4_m);    \
  stp23_m = DOT_SHIFT_RIGHT_PCK_H(vec4_m, vec5_m, cnst5_m);    \
                                                                   \
  /* stp2 */                                                       \
  BUTTERFLY_4(input0, input1, stp22_m, stp23_m,                    \
              stp30_m, stp31_m, stp32_m, stp33_m);                 \
  BUTTERFLY_4(input7, input6, stp25_m, stp24_m,                    \
              stp37_m, stp36_m, stp35_m, stp34_m);                 \
                                                                   \
  ILVL_H2_SH(stp36_m, stp31_m, stp35_m, stp32_m, vec2_m, vec4_m);  \
  ILVR_H2_SH(stp36_m, stp31_m, stp35_m, stp32_m, vec3_m, vec5_m);  \
                                                                   \
  SPLATI_H2_SH(coeff_m, 2, 3, cnst0_m, cnst1_m);                   \
  cnst0_m = __msa_ilvev_h(cnst0_m, cnst1_m);                       \
  stp26_m = DOT_SHIFT_RIGHT_PCK_H(vec2_m, vec3_m, cnst0_m);    \
                                                                   \
  cnst0_m = __msa_splati_h(coeff_m, 4);                            \
  cnst1_m = __msa_ilvev_h(cnst1_m, cnst0_m);                       \
  stp21_m = DOT_SHIFT_RIGHT_PCK_H(vec2_m, vec3_m, cnst1_m);    \
                                                                   \
  SPLATI_H2_SH(coeff_m, 5, 2, cnst0_m, cnst1_m);                   \
  cnst1_m = __msa_ilvev_h(cnst0_m, cnst1_m);                       \
  stp25_m = DOT_SHIFT_RIGHT_PCK_H(vec4_m, vec5_m, cnst1_m);    \
                                                                   \
  cnst0_m = __msa_splati_h(coeff_m, 3);                            \
  cnst1_m = __msa_ilvev_h(cnst1_m, cnst0_m);                       \
  stp22_m = DOT_SHIFT_RIGHT_PCK_H(vec4_m, vec5_m, cnst1_m);    \
                                                                   \
  /* stp4 */                                                       \
  BUTTERFLY_4(stp30_m, stp37_m, stp26_m, stp21_m,                  \
              vec6_m, vec2_m, vec4_m, vec5_m);                     \
  BUTTERFLY_4(stp33_m, stp34_m, stp25_m, stp22_m,                  \
              stp21_m, stp23_m, stp24_m, stp31_m);                 \
                                                                   \
  ILVRL_H2_SH(vec2_m, vec6_m, vec1_m, vec0_m);                     \
  SPLATI_H2_SH(coeff1_m, 0, 1, cnst0_m, cnst1_m);                  \
  cnst0_m = __msa_ilvev_h(cnst0_m, cnst1_m);                       \
                                                                   \
  out1 = DOT_SHIFT_RIGHT_PCK_H(vec0_m, vec1_m, cnst0_m);       \
                                                                   \
  cnst0_m = __msa_splati_h(coeff2_m, 0);                           \
  cnst0_m = __msa_ilvev_h(cnst1_m, cnst0_m);                       \
  out15 = DOT_SHIFT_RIGHT_PCK_H(vec0_m, vec1_m, cnst0_m);      \
                                                                   \
  ILVRL_H2_SH(vec4_m, vec5_m, vec1_m, vec0_m);                     \
  SPLATI_H2_SH(coeff1_m, 2, 3, cnst0_m, cnst1_m);                  \
  cnst1_m = __msa_ilvev_h(cnst1_m, cnst0_m);                       \
                                                                   \
  out9 = DOT_SHIFT_RIGHT_PCK_H(vec0_m, vec1_m, cnst1_m);       \
                                                                   \
  cnst1_m = __msa_splati_h(coeff2_m, 2);                           \
  cnst0_m = __msa_ilvev_h(cnst0_m, cnst1_m);                       \
  out7 = DOT_SHIFT_RIGHT_PCK_H(vec0_m, vec1_m, cnst0_m);       \
                                                                   \
  ILVRL_H2_SH(stp23_m, stp21_m, vec1_m, vec0_m);                   \
  SPLATI_H2_SH(coeff1_m, 4, 5, cnst0_m, cnst1_m);                  \
  cnst0_m = __msa_ilvev_h(cnst0_m, cnst1_m);                       \
  out5 = DOT_SHIFT_RIGHT_PCK_H(vec0_m, vec1_m, cnst0_m);       \
                                                                   \
  cnst0_m = __msa_splati_h(coeff2_m, 1);                           \
  cnst0_m = __msa_ilvev_h(cnst1_m, cnst0_m);                       \
  out11 = DOT_SHIFT_RIGHT_PCK_H(vec0_m, vec1_m, cnst0_m);      \
                                                                   \
  ILVRL_H2_SH(stp24_m, stp31_m, vec1_m, vec0_m);                   \
  SPLATI_H2_SH(coeff1_m, 6, 7, cnst0_m, cnst1_m);                  \
  cnst1_m = __msa_ilvev_h(cnst1_m, cnst0_m);                       \
                                                                   \
  out13 = DOT_SHIFT_RIGHT_PCK_H(vec0_m, vec1_m, cnst1_m);      \
                                                                   \
  cnst1_m = __msa_splati_h(coeff2_m, 3);                           \
  cnst0_m = __msa_ilvev_h(cnst0_m, cnst1_m);                       \
  out3 = DOT_SHIFT_RIGHT_PCK_H(vec0_m, vec1_m, cnst0_m);       \
}

static void fdct8x16_1d_column(const int16_t *input, int16_t *tmp_ptr,
                               int32_t src_stride) {
  v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
  v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
  v8i16 in8, in9, in10, in11, in12, in13, in14, in15;
  v8i16 stp21, stp22, stp23, stp24, stp25, stp26, stp30;
  v8i16 stp31, stp32, stp33, stp34, stp35, stp36, stp37;
  v8i16 vec0, vec1, vec2, vec3, vec4, vec5, cnst0, cnst1, cnst4, cnst5;
  v8i16 coeff = { cospi_16_64, -cospi_16_64, cospi_8_64, cospi_24_64,
                 -cospi_8_64, -cospi_24_64, cospi_12_64, cospi_20_64 };
  v8i16 coeff1 = { cospi_2_64, cospi_30_64, cospi_14_64, cospi_18_64,
                   cospi_10_64, cospi_22_64, cospi_6_64, cospi_26_64 };
  v8i16 coeff2 = { -cospi_2_64, -cospi_10_64, -cospi_18_64, -cospi_26_64,
                   0, 0, 0, 0 };

  LD_SH16(input, src_stride,
          in0, in1, in2, in3, in4, in5, in6, in7,
          in8, in9, in10, in11, in12, in13, in14, in15);
  SLLI_4V(in0, in1, in2, in3, 2);
  SLLI_4V(in4, in5, in6, in7, 2);
  SLLI_4V(in8, in9, in10, in11, 2);
  SLLI_4V(in12, in13, in14, in15, 2);
  ADD4(in0, in15, in1, in14, in2, in13, in3, in12, tmp0, tmp1, tmp2, tmp3);
  ADD4(in4, in11, in5, in10, in6, in9, in7, in8, tmp4, tmp5, tmp6, tmp7);
  FDCT8x16_EVEN(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7,
                tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7);
  ST_SH8(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp_ptr, 32);
  SUB4(in0, in15, in1, in14, in2, in13, in3, in12, in15, in14, in13, in12);
  SUB4(in4, in11, in5, in10, in6, in9, in7, in8, in11, in10, in9, in8);

  tmp_ptr += 16;

  /* stp 1 */
  ILVL_H2_SH(in10, in13, in11, in12, vec2, vec4);
  ILVR_H2_SH(in10, in13, in11, in12, vec3, vec5);

  cnst4 = __msa_splati_h(coeff, 0);
  stp25 = DOT_SHIFT_RIGHT_PCK_H(vec2, vec3, cnst4);

  cnst5 = __msa_splati_h(coeff, 1);
  cnst5 = __msa_ilvev_h(cnst5, cnst4);
  stp22 = DOT_SHIFT_RIGHT_PCK_H(vec2, vec3, cnst5);
  stp24 = DOT_SHIFT_RIGHT_PCK_H(vec4, vec5, cnst4);
  stp23 = DOT_SHIFT_RIGHT_PCK_H(vec4, vec5, cnst5);

  /* stp2 */
  BUTTERFLY_4(in8, in9, stp22, stp23, stp30, stp31, stp32, stp33);
  BUTTERFLY_4(in15, in14, stp25, stp24, stp37, stp36, stp35, stp34);
  ILVL_H2_SH(stp36, stp31, stp35, stp32, vec2, vec4);
  ILVR_H2_SH(stp36, stp31, stp35, stp32, vec3, vec5);
  SPLATI_H2_SH(coeff, 2, 3, cnst0, cnst1);
  cnst0 = __msa_ilvev_h(cnst0, cnst1);
  stp26 = DOT_SHIFT_RIGHT_PCK_H(vec2, vec3, cnst0);

  cnst0 = __msa_splati_h(coeff, 4);
  cnst1 = __msa_ilvev_h(cnst1, cnst0);
  stp21 = DOT_SHIFT_RIGHT_PCK_H(vec2, vec3, cnst1);

  BUTTERFLY_4(stp30, stp37, stp26, stp21, in8, in15, in14, in9);
  ILVRL_H2_SH(in15, in8, vec1, vec0);
  SPLATI_H2_SH(coeff1, 0, 1, cnst0, cnst1);
  cnst0 = __msa_ilvev_h(cnst0, cnst1);

  in8 = DOT_SHIFT_RIGHT_PCK_H(vec0, vec1, cnst0);
  ST_SH(in8, tmp_ptr);

  cnst0 = __msa_splati_h(coeff2, 0);
  cnst0 = __msa_ilvev_h(cnst1, cnst0);
  in8 = DOT_SHIFT_RIGHT_PCK_H(vec0, vec1, cnst0);
  ST_SH(in8, tmp_ptr + 224);

  ILVRL_H2_SH(in14, in9, vec1, vec0);
  SPLATI_H2_SH(coeff1, 2, 3, cnst0, cnst1);
  cnst1 = __msa_ilvev_h(cnst1, cnst0);

  in8 = DOT_SHIFT_RIGHT_PCK_H(vec0, vec1, cnst1);
  ST_SH(in8, tmp_ptr + 128);

  cnst1 = __msa_splati_h(coeff2, 2);
  cnst0 = __msa_ilvev_h(cnst0, cnst1);
  in8 = DOT_SHIFT_RIGHT_PCK_H(vec0, vec1, cnst0);
  ST_SH(in8, tmp_ptr + 96);

  SPLATI_H2_SH(coeff, 2, 5, cnst0, cnst1);
  cnst1 = __msa_ilvev_h(cnst1, cnst0);

  stp25 = DOT_SHIFT_RIGHT_PCK_H(vec4, vec5, cnst1);

  cnst1 = __msa_splati_h(coeff, 3);
  cnst1 = __msa_ilvev_h(cnst0, cnst1);
  stp22 = DOT_SHIFT_RIGHT_PCK_H(vec4, vec5, cnst1);

  /* stp4 */
  ADD2(stp34, stp25, stp33, stp22, in13, in10);

  ILVRL_H2_SH(in13, in10, vec1, vec0);
  SPLATI_H2_SH(coeff1, 4, 5, cnst0, cnst1);
  cnst0 = __msa_ilvev_h(cnst0, cnst1);
  in8 = DOT_SHIFT_RIGHT_PCK_H(vec0, vec1, cnst0);
  ST_SH(in8, tmp_ptr + 64);

  cnst0 = __msa_splati_h(coeff2, 1);
  cnst0 = __msa_ilvev_h(cnst1, cnst0);
  in8 = DOT_SHIFT_RIGHT_PCK_H(vec0, vec1, cnst0);
  ST_SH(in8, tmp_ptr + 160);

  SUB2(stp34, stp25, stp33, stp22, in12, in11);
  ILVRL_H2_SH(in12, in11, vec1, vec0);
  SPLATI_H2_SH(coeff1, 6, 7, cnst0, cnst1);
  cnst1 = __msa_ilvev_h(cnst1, cnst0);

  in8 = DOT_SHIFT_RIGHT_PCK_H(vec0, vec1, cnst1);
  ST_SH(in8, tmp_ptr + 192);

  cnst1 = __msa_splati_h(coeff2, 3);
  cnst0 = __msa_ilvev_h(cnst0, cnst1);
  in8 = DOT_SHIFT_RIGHT_PCK_H(vec0, vec1, cnst0);
  ST_SH(in8, tmp_ptr + 32);
}

static void fdct16x8_1d_row(int16_t *input, int16_t *output) {
  v8i16 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
  v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
  v8i16 in8, in9, in10, in11, in12, in13, in14, in15;

  LD_SH8(input, 16, in0, in1, in2, in3, in4, in5, in6, in7);
  LD_SH8((input + 8), 16, in8, in9, in10, in11, in12, in13, in14, in15);
  TRANSPOSE8x8_SH_SH(in0, in1, in2, in3, in4, in5, in6, in7,
                     in0, in1, in2, in3, in4, in5, in6, in7);
  TRANSPOSE8x8_SH_SH(in8, in9, in10, in11, in12, in13, in14, in15,
                     in8, in9, in10, in11, in12, in13, in14, in15);
  ADD4(in0, 1, in1, 1, in2, 1, in3, 1, in0, in1, in2, in3);
  ADD4(in4, 1, in5, 1, in6, 1, in7, 1, in4, in5, in6, in7);
  ADD4(in8, 1, in9, 1, in10, 1, in11, 1, in8, in9, in10, in11);
  ADD4(in12, 1, in13, 1, in14, 1, in15, 1, in12, in13, in14, in15);
  SRA_4V(in0, in1, in2, in3, 2);
  SRA_4V(in4, in5, in6, in7, 2);
  SRA_4V(in8, in9, in10, in11, 2);
  SRA_4V(in12, in13, in14, in15, 2);
  BUTTERFLY_16(in0, in1, in2, in3, in4, in5, in6, in7, in8, in9, in10, in11,
               in12, in13, in14, in15, tmp0, tmp1, tmp2, tmp3, tmp4, tmp5,
               tmp6, tmp7, in8, in9, in10, in11, in12, in13, in14, in15);
  ST_SH8(in8, in9, in10, in11, in12, in13, in14, in15, input, 16);
  FDCT8x16_EVEN(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7,
                tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7);
  LD_SH8(input, 16, in8, in9, in10, in11, in12, in13, in14, in15);
  FDCT8x16_ODD(in8, in9, in10, in11, in12, in13, in14, in15,
                   in0, in1, in2, in3, in4, in5, in6, in7);
  TRANSPOSE8x8_SH_SH(tmp0, in0, tmp1, in1, tmp2, in2, tmp3, in3,
                     tmp0, in0, tmp1, in1, tmp2, in2, tmp3, in3);
  ST_SH8(tmp0, in0, tmp1, in1, tmp2, in2, tmp3, in3, output, 16);
  TRANSPOSE8x8_SH_SH(tmp4, in4, tmp5, in5, tmp6, in6, tmp7, in7,
                     tmp4, in4, tmp5, in5, tmp6, in6, tmp7, in7);
  ST_SH8(tmp4, in4, tmp5, in5, tmp6, in6, tmp7, in7, output + 8, 16);
}
