/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


/****************************************************************************
 * Notes:
 *
 * This implementation makes use of 16 bit fixed point verio of two multiply
 * constants:
 *         1.   sqrt(2) * cos (pi/8)
 *         2.   sqrt(2) * sin (pi/8)
 * Becuase the first constant is bigger than 1, to maintain the same 16 bit
 * fixed point precision as the second one, we use a trick of
 *         x * a = x + x*(a-1)
 * so
 *         x * sqrt(2) * cos (pi/8) = x + x * (sqrt(2) *cos(pi/8)-1).
 **************************************************************************/
#include "vpx_ports/config.h"
#include "vp8/common/idct.h"

#if CONFIG_HYBRIDTRANSFORM
#include "vp8/common/blockd.h"
#endif

#include <math.h>

static const int cospi8sqrt2minus1 = 20091;
static const int sinpi8sqrt2      = 35468;
static const int rounding = 0;

#if CONFIG_HYBRIDTRANSFORM
float idct_4[16] = {
  0.500000000000000,   0.653281482438188,   0.500000000000000,   0.270598050073099,
  0.500000000000000,   0.270598050073099,  -0.500000000000000,  -0.653281482438188,
  0.500000000000000,  -0.270598050073099,  -0.500000000000000,   0.653281482438188,
  0.500000000000000,  -0.653281482438188,   0.500000000000000,  -0.270598050073099
};

float iadst_4[16] = {
  0.228013428883779,   0.577350269189626,   0.656538502008139,   0.428525073124360,
  0.428525073124360,   0.577350269189626,  -0.228013428883779,  -0.656538502008139,
  0.577350269189626,                   0,  -0.577350269189626,   0.577350269189626,
  0.656538502008139,  -0.577350269189626,   0.428525073124359,  -0.228013428883779
};
#endif

#if CONFIG_HYBRIDTRANSFORM
void vp8_iht4x4llm_c(short *input, short *output, int pitch, TX_TYPE tx_type) {
  int i, j, k;
  float bufa[16], bufb[16]; // buffers are for floating-point test purpose
                            // the implementation could be simplified in conjunction with integer transform
  short *ip = input;
  short *op = output;
  int shortpitch = pitch >> 1;

  float *pfa = &bufa[0];
  float *pfb = &bufb[0];

  // pointers to vertical and horizontal transforms
  float *ptv, *pth;

  // load and convert residual array into floating-point
  for(j = 0; j < 4; j++) {
    for(i = 0; i < 4; i++) {
      pfa[i] = (float)ip[i];
    }
    pfa += 4;
    ip  += 4;
  }

  // vertical transformation
  pfa = &bufa[0];
  pfb = &bufb[0];

  switch(tx_type) {
    case ADST_ADST :
    case ADST_DCT  :
      ptv = &iadst_4[0];
      break;

    default :
      ptv = &idct_4[0];
      break;
  }

  for(j = 0; j < 4; j++) {
    for(i = 0; i < 4; i++) {
      pfb[i] = 0 ;
      for(k = 0; k < 4; k++) {
        pfb[i] += ptv[k] * pfa[(k<<2)];
      }
      pfa += 1;
    }

    pfb += 4;
    ptv += 4;
    pfa = &bufa[0];
  }

  // horizontal transformation
  pfa = &bufa[0];
  pfb = &bufb[0];

  switch(tx_type) {
    case ADST_ADST :
    case  DCT_ADST :
      pth = &iadst_4[0];
      break;

    default :
      pth = &idct_4[0];
      break;
  }

  for(j = 0; j < 4; j++) {
    for(i = 0; i < 4; i++) {
      pfa[i] = 0;
      for(k = 0; k < 4; k++) {
        pfa[i] += pfb[k] * pth[k];
      }
      pth += 4;
     }

    pfa += 4;
    pfb += 4;

    switch(tx_type) {
      case ADST_ADST :
      case  DCT_ADST :
        pth = &iadst_4[0];
        break;

      default :
        pth = &idct_4[0];
        break;
    }
  }

  // convert to short integer format and load BLOCKD buffer
  op  = output;
  pfa = &bufa[0];

  for(j = 0; j < 4; j++) {
    for(i = 0; i < 4; i++) {
      op[i] = (pfa[i] > 0 ) ? (short)( pfa[i] / 8 + 0.49) :
                             -(short)( - pfa[i] / 8 + 0.49);
    }
    op  += shortpitch;
    pfa += 4;
  }
}
#endif


void vp8_short_idct4x4llm_c(short *input, short *output, int pitch) {
  int i;
  int a1, b1, c1, d1;

  short *ip = input;
  short *op = output;
  int temp1, temp2;
  int shortpitch = pitch >> 1;

  for (i = 0; i < 4; i++) {
    a1 = ip[0] + ip[8];
    b1 = ip[0] - ip[8];

    temp1 = (ip[4] * sinpi8sqrt2 + rounding) >> 16;
    temp2 = ip[12] + ((ip[12] * cospi8sqrt2minus1 + rounding) >> 16);
    c1 = temp1 - temp2;

    temp1 = ip[4] + ((ip[4] * cospi8sqrt2minus1 + rounding) >> 16);
    temp2 = (ip[12] * sinpi8sqrt2 + rounding) >> 16;
    d1 = temp1 + temp2;

    op[shortpitch * 0] = a1 + d1;
    op[shortpitch * 3] = a1 - d1;

    op[shortpitch * 1] = b1 + c1;
    op[shortpitch * 2] = b1 - c1;

    ip++;
    op++;
  }

  ip = output;
  op = output;

  for (i = 0; i < 4; i++) {
    a1 = ip[0] + ip[2];
    b1 = ip[0] - ip[2];

    temp1 = (ip[1] * sinpi8sqrt2 + rounding) >> 16;
    temp2 = ip[3] + ((ip[3] * cospi8sqrt2minus1 + rounding) >> 16);
    c1 = temp1 - temp2;

    temp1 = ip[1] + ((ip[1] * cospi8sqrt2minus1 + rounding) >> 16);
    temp2 = (ip[3] * sinpi8sqrt2 + rounding) >> 16;
    d1 = temp1 + temp2;

    op[0] = (a1 + d1 + 16) >> 5;
    op[3] = (a1 - d1 + 16) >> 5;

    op[1] = (b1 + c1 + 16) >> 5;
    op[2] = (b1 - c1 + 16) >> 5;

    ip += shortpitch;
    op += shortpitch;
  }
}

void vp8_short_idct4x4llm_1_c(short *input, short *output, int pitch) {
  int i;
  int a1;
  short *op = output;
  int shortpitch = pitch >> 1;
  a1 = ((input[0] + 16) >> 5);
  for (i = 0; i < 4; i++) {
    op[0] = a1;
    op[1] = a1;
    op[2] = a1;
    op[3] = a1;
    op += shortpitch;
  }
}

void vp8_dc_only_idct_add_c(short input_dc, unsigned char *pred_ptr, unsigned char *dst_ptr, int pitch, int stride) {
  int a1 = ((input_dc + 16) >> 5);
  int r, c;

  for (r = 0; r < 4; r++) {
    for (c = 0; c < 4; c++) {
      int a = a1 + pred_ptr[c];

      if (a < 0)
        a = 0;

      if (a > 255)
        a = 255;

      dst_ptr[c] = (unsigned char) a;
    }

    dst_ptr += stride;
    pred_ptr += pitch;
  }

}

void vp8_short_inv_walsh4x4_c(short *input, short *output) {
  int i;
  int a1, b1, c1, d1;
  short *ip = input;
  short *op = output;

  for (i = 0; i < 4; i++) {
    a1 = ((ip[0] + ip[3]));
    b1 = ((ip[1] + ip[2]));
    c1 = ((ip[1] - ip[2]));
    d1 = ((ip[0] - ip[3]));

    op[0] = (a1 + b1 + 1) >> 1;
    op[1] = (c1 + d1) >> 1;
    op[2] = (a1 - b1) >> 1;
    op[3] = (d1 - c1) >> 1;

    ip += 4;
    op += 4;
  }

  ip = output;
  op = output;
  for (i = 0; i < 4; i++) {
    a1 = ip[0] + ip[12];
    b1 = ip[4] + ip[8];
    c1 = ip[4] - ip[8];
    d1 = ip[0] - ip[12];
    op[0] = (a1 + b1 + 1) >> 1;
    op[4] = (c1 + d1) >> 1;
    op[8] = (a1 - b1) >> 1;
    op[12] = (d1 - c1) >> 1;
    ip++;
    op++;
  }
}

void vp8_short_inv_walsh4x4_1_c(short *in, short *out) {
  int i;
  short tmp[4];
  short *ip = in;
  short *op = tmp;

  op[0] = (ip[0] + 1) >> 1;
  op[1] = op[2] = op[3] = (ip[0] >> 1);

  ip = tmp;
  op = out;
  for (i = 0; i < 4; i++) {
    op[0] = (ip[0] + 1) >> 1;
    op[4] = op[8] = op[12] = (ip[0] >> 1);
    ip++;
    op++;
  }
}

#if CONFIG_LOSSLESS
void vp8_short_inv_walsh4x4_lossless_c(short *input, short *output) {
  int i;
  int a1, b1, c1, d1;
  short *ip = input;
  short *op = output;

  for (i = 0; i < 4; i++) {
    a1 = ((ip[0] + ip[3])) >> Y2_WHT_UPSCALE_FACTOR;
    b1 = ((ip[1] + ip[2])) >> Y2_WHT_UPSCALE_FACTOR;
    c1 = ((ip[1] - ip[2])) >> Y2_WHT_UPSCALE_FACTOR;
    d1 = ((ip[0] - ip[3])) >> Y2_WHT_UPSCALE_FACTOR;

    op[0] = (a1 + b1 + 1) >> 1;
    op[1] = (c1 + d1) >> 1;
    op[2] = (a1 - b1) >> 1;
    op[3] = (d1 - c1) >> 1;

    ip += 4;
    op += 4;
  }

  ip = output;
  op = output;
  for (i = 0; i < 4; i++) {
    a1 = ip[0] + ip[12];
    b1 = ip[4] + ip[8];
    c1 = ip[4] - ip[8];
    d1 = ip[0] - ip[12];


    op[0] = ((a1 + b1 + 1) >> 1) << Y2_WHT_UPSCALE_FACTOR;
    op[4] = ((c1 + d1) >> 1) << Y2_WHT_UPSCALE_FACTOR;
    op[8] = ((a1 - b1) >> 1) << Y2_WHT_UPSCALE_FACTOR;
    op[12] = ((d1 - c1) >> 1) << Y2_WHT_UPSCALE_FACTOR;

    ip++;
    op++;
  }
}

void vp8_short_inv_walsh4x4_1_lossless_c(short *in, short *out) {
  int i;
  short tmp[4];
  short *ip = in;
  short *op = tmp;

  op[0] = ((ip[0] >> Y2_WHT_UPSCALE_FACTOR) + 1) >> 1;
  op[1] = op[2] = op[3] = ((ip[0] >> Y2_WHT_UPSCALE_FACTOR) >> 1);

  ip = tmp;
  op = out;
  for (i = 0; i < 4; i++) {
    op[0] = ((ip[0] + 1) >> 1) << Y2_WHT_UPSCALE_FACTOR;
    op[4] = op[8] = op[12] = ((ip[0] >> 1)) << Y2_WHT_UPSCALE_FACTOR;
    ip++;
    op++;
  }
}

void vp8_short_inv_walsh4x4_x8_c(short *input, short *output, int pitch) {
  int i;
  int a1, b1, c1, d1;
  short *ip = input;
  short *op = output;
  int shortpitch = pitch >> 1;

  for (i = 0; i < 4; i++) {
    a1 = ((ip[0] + ip[3])) >> WHT_UPSCALE_FACTOR;
    b1 = ((ip[1] + ip[2])) >> WHT_UPSCALE_FACTOR;
    c1 = ((ip[1] - ip[2])) >> WHT_UPSCALE_FACTOR;
    d1 = ((ip[0] - ip[3])) >> WHT_UPSCALE_FACTOR;

    op[0] = (a1 + b1 + 1) >> 1;
    op[1] = (c1 + d1) >> 1;
    op[2] = (a1 - b1) >> 1;
    op[3] = (d1 - c1) >> 1;

    ip += 4;
    op += shortpitch;
  }

  ip = output;
  op = output;
  for (i = 0; i < 4; i++) {
    a1 = ip[shortpitch * 0] + ip[shortpitch * 3];
    b1 = ip[shortpitch * 1] + ip[shortpitch * 2];
    c1 = ip[shortpitch * 1] - ip[shortpitch * 2];
    d1 = ip[shortpitch * 0] - ip[shortpitch * 3];


    op[shortpitch * 0] = (a1 + b1 + 1) >> 1;
    op[shortpitch * 1] = (c1 + d1) >> 1;
    op[shortpitch * 2] = (a1 - b1) >> 1;
    op[shortpitch * 3] = (d1 - c1) >> 1;

    ip++;
    op++;
  }
}

void vp8_short_inv_walsh4x4_1_x8_c(short *in, short *out, int pitch) {
  int i;
  short tmp[4];
  short *ip = in;
  short *op = tmp;
  int shortpitch = pitch >> 1;

  op[0] = ((ip[0] >> WHT_UPSCALE_FACTOR) + 1) >> 1;
  op[1] = op[2] = op[3] = ((ip[0] >> WHT_UPSCALE_FACTOR) >> 1);


  ip = tmp;
  op = out;
  for (i = 0; i < 4; i++) {
    op[shortpitch * 0] = (ip[0] + 1) >> 1;
    op[shortpitch * 1] = op[shortpitch * 2] = op[shortpitch * 3] = ip[0] >> 1;
    ip++;
    op++;
  }
}

void vp8_dc_only_inv_walsh_add_c(short input_dc, unsigned char *pred_ptr, unsigned char *dst_ptr, int pitch, int stride) {
  int r, c;
  short tmp[16];
  vp8_short_inv_walsh4x4_1_x8_c(&input_dc, tmp, 4 << 1);

  for (r = 0; r < 4; r++) {
    for (c = 0; c < 4; c++) {
      int a = tmp[r * 4 + c] + pred_ptr[c];
      if (a < 0)
        a = 0;

      if (a > 255)
        a = 255;

      dst_ptr[c] = (unsigned char) a;
    }

    dst_ptr += stride;
    pred_ptr += pitch;
  }
}
#endif

void vp8_dc_only_idct_add_8x8_c(short input_dc,
                                unsigned char *pred_ptr,
                                unsigned char *dst_ptr,
                                int pitch, int stride) {
  int a1 = ((input_dc + 16) >> 5);
  int r, c, b;
  unsigned char *orig_pred = pred_ptr;
  unsigned char *orig_dst = dst_ptr;
  for (b = 0; b < 4; b++) {
    for (r = 0; r < 4; r++) {
      for (c = 0; c < 4; c++) {
        int a = a1 + pred_ptr[c];

        if (a < 0)
          a = 0;

        if (a > 255)
          a = 255;

        dst_ptr[c] = (unsigned char) a;
      }

      dst_ptr += stride;
      pred_ptr += pitch;
    }
    dst_ptr = orig_dst + (b + 1) % 2 * 4 + (b + 1) / 2 * 4 * stride;
    pred_ptr = orig_pred + (b + 1) % 2 * 4 + (b + 1) / 2 * 4 * pitch;
  }
}

#define W1 2841                 /* 2048*sqrt(2)*cos(1*pi/16) */
#define W2 2676                 /* 2048*sqrt(2)*cos(2*pi/16) */
#define W3 2408                 /* 2048*sqrt(2)*cos(3*pi/16) */
#define W5 1609                 /* 2048*sqrt(2)*cos(5*pi/16) */
#define W6 1108                 /* 2048*sqrt(2)*cos(6*pi/16) */
#define W7 565                  /* 2048*sqrt(2)*cos(7*pi/16) */

/* row (horizontal) IDCT
 *
 * 7                       pi         1 dst[k] = sum c[l] * src[l] * cos( -- *
 * ( k + - ) * l ) l=0                      8          2
 *
 * where: c[0]    = 128 c[1..7] = 128*sqrt(2) */

static void idctrow(int *blk) {
  int x0, x1, x2, x3, x4, x5, x6, x7, x8;
  /* shortcut */
  if (!((x1 = blk[4] << 11) | (x2 = blk[6]) | (x3 = blk[2]) |
        (x4 = blk[1]) | (x5 = blk[7]) | (x6 = blk[5]) | (x7 = blk[3]))) {
    blk[0] = blk[1] = blk[2] = blk[3] = blk[4]
                                        = blk[5] = blk[6] = blk[7] = blk[0] << 3;
    return;
  }

  x0 = (blk[0] << 11) + 128;    /* for proper rounding in the fourth stage */
  /* first stage */
  x8 = W7 * (x4 + x5);
  x4 = x8 + (W1 - W7) * x4;
  x5 = x8 - (W1 + W7) * x5;
  x8 = W3 * (x6 + x7);
  x6 = x8 - (W3 - W5) * x6;
  x7 = x8 - (W3 + W5) * x7;

  /* second stage */
  x8 = x0 + x1;
  x0 -= x1;
  x1 = W6 * (x3 + x2);
  x2 = x1 - (W2 + W6) * x2;
  x3 = x1 + (W2 - W6) * x3;
  x1 = x4 + x6;
  x4 -= x6;
  x6 = x5 + x7;
  x5 -= x7;

  /* third stage */
  x7 = x8 + x3;
  x8 -= x3;
  x3 = x0 + x2;
  x0 -= x2;
  x2 = (181 * (x4 + x5) + 128) >> 8;
  x4 = (181 * (x4 - x5) + 128) >> 8;

  /* fourth stage */
  blk[0] = (x7 + x1) >> 8;
  blk[1] = (x3 + x2) >> 8;
  blk[2] = (x0 + x4) >> 8;
  blk[3] = (x8 + x6) >> 8;
  blk[4] = (x8 - x6) >> 8;
  blk[5] = (x0 - x4) >> 8;
  blk[6] = (x3 - x2) >> 8;
  blk[7] = (x7 - x1) >> 8;
}

/* column (vertical) IDCT
 *
 * 7                         pi         1 dst[8*k] = sum c[l] * src[8*l] *
 * cos( -- * ( k + - ) * l ) l=0                        8          2
 *
 * where: c[0]    = 1/1024 c[1..7] = (1/1024)*sqrt(2) */
static void idctcol(int *blk) {
  int x0, x1, x2, x3, x4, x5, x6, x7, x8;

  /* shortcut */
  if (!((x1 = (blk[8 * 4] << 8)) | (x2 = blk[8 * 6]) | (x3 = blk[8 * 2]) |
        (x4 = blk[8 * 1]) | (x5 = blk[8 * 7]) | (x6 = blk[8 * 5]) |
        (x7 = blk[8 * 3]))) {
    blk[8 * 0] = blk[8 * 1] = blk[8 * 2] = blk[8 * 3]
                                           = blk[8 * 4] = blk[8 * 5] = blk[8 * 6]
                                                                       = blk[8 * 7] = ((blk[8 * 0] + 32) >> 6);
    return;
  }

  x0 = (blk[8 * 0] << 8) + 16384;

  /* first stage */
  x8 = W7 * (x4 + x5) + 4;
  x4 = (x8 + (W1 - W7) * x4) >> 3;
  x5 = (x8 - (W1 + W7) * x5) >> 3;
  x8 = W3 * (x6 + x7) + 4;
  x6 = (x8 - (W3 - W5) * x6) >> 3;
  x7 = (x8 - (W3 + W5) * x7) >> 3;

  /* second stage */
  x8 = x0 + x1;
  x0 -= x1;
  x1 = W6 * (x3 + x2) + 4;
  x2 = (x1 - (W2 + W6) * x2) >> 3;
  x3 = (x1 + (W2 - W6) * x3) >> 3;
  x1 = x4 + x6;
  x4 -= x6;
  x6 = x5 + x7;
  x5 -= x7;

  /* third stage */
  x7 = x8 + x3;
  x8 -= x3;
  x3 = x0 + x2;
  x0 -= x2;
  x2 = (181 * (x4 + x5) + 128) >> 8;
  x4 = (181 * (x4 - x5) + 128) >> 8;

  /* fourth stage */
  blk[8 * 0] = (x7 + x1) >> 14;
  blk[8 * 1] = (x3 + x2) >> 14;
  blk[8 * 2] = (x0 + x4) >> 14;
  blk[8 * 3] = (x8 + x6) >> 14;
  blk[8 * 4] = (x8 - x6) >> 14;
  blk[8 * 5] = (x0 - x4) >> 14;
  blk[8 * 6] = (x3 - x2) >> 14;
  blk[8 * 7] = (x7 - x1) >> 14;
}

#define TX_DIM 8
void vp8_short_idct8x8_c(short *coefs, short *block, int pitch) {
  int X[TX_DIM * TX_DIM];
  int i, j;
  int shortpitch = pitch >> 1;

  for (i = 0; i < TX_DIM; i++) {
    for (j = 0; j < TX_DIM; j++) {
      X[i * TX_DIM + j] = (int)(coefs[i * TX_DIM + j] + 1
                                + (coefs[i * TX_DIM + j] < 0)) >> 2;
    }
  }
  for (i = 0; i < 8; i++)
    idctrow(X + 8 * i);

  for (i = 0; i < 8; i++)
    idctcol(X + i);

  for (i = 0; i < TX_DIM; i++) {
    for (j = 0; j < TX_DIM; j++) {
      block[i * shortpitch + j]  = X[i * TX_DIM + j] >> 1;
    }
  }
}


void vp8_short_ihaar2x2_c(short *input, short *output, int pitch) {
  int i;
  short *ip = input; // 0,1, 4, 8
  short *op = output;
  for (i = 0; i < 16; i++) {
    op[i] = 0;
  }

  op[0] = (ip[0] + ip[1] + ip[4] + ip[8] + 1) >> 1;
  op[1] = (ip[0] - ip[1] + ip[4] - ip[8]) >> 1;
  op[4] = (ip[0] + ip[1] - ip[4] - ip[8]) >> 1;
  op[8] = (ip[0] - ip[1] - ip[4] + ip[8]) >> 1;
}


#if CONFIG_TX16X16
// Keep a really bad float version for now.
/*void vp8_short_idct16x16_c(short *input, short *output, int pitch) {
  double x;
  const int short_pitch = pitch >> 1;
  int i, j, k, l;
  for (l = 0; l < 16; ++l) {
    for (k = 0; k < 16; ++k) {
      double s = 0;
      for (i = 0; i < 16; ++i) {
        for (j = 0; j < 16; ++j) {
          x=cos(PI*j*(l+0.5)/16.0)*cos(PI*i*(k+0.5)/16.0)*input[i*16+j]/32;
          if (i != 0)
            x *= sqrt(2.0);
          if (j != 0)
            x *= sqrt(2.0);
          s += x;
        }
      }
      output[k*short_pitch+l] = (short)round(s);
    }
  }
}*/

static void butterfly_16x16_idct_1d(double input[16], double output[16]) {
  double step[16];
  double intermediate[16];
  double temp1, temp2;

  const double PI = M_PI;
  const double C1 = cos(1*PI/(double)32);
  const double C2 = cos(2*PI/(double)32);
  const double C3 = cos(3*PI/(double)32);
  const double C4 = cos(4*PI/(double)32);
  const double C5 = cos(5*PI/(double)32);
  const double C6 = cos(6*PI/(double)32);
  const double C7 = cos(7*PI/(double)32);
  const double C8 = cos(8*PI/(double)32);
  const double C9 = cos(9*PI/(double)32);
  const double C10 = cos(10*PI/(double)32);
  const double C11 = cos(11*PI/(double)32);
  const double C12 = cos(12*PI/(double)32);
  const double C13 = cos(13*PI/(double)32);
  const double C14 = cos(14*PI/(double)32);
  const double C15 = cos(15*PI/(double)32);

  // step 1 and 2
  step[ 0] = input[0] + input[8];
  step[ 1] = input[0] - input[8];

  temp1 = input[4]*C12;
  temp2 = input[12]*C4;

  temp1 -= temp2;
  temp1 *= C8;

  step[ 2] = 2*(temp1);

  temp1 = input[4]*C4;
  temp2 = input[12]*C12;
  temp1 += temp2;
  temp1 = (temp1);
  temp1 *= C8;
  step[ 3] = 2*(temp1);

  temp1 = input[2]*C8;
  temp1 = 2*(temp1);
  temp2 = input[6] + input[10];

  step[ 4] = temp1 + temp2;
  step[ 5] = temp1 - temp2;

  temp1 = input[14]*C8;
  temp1 = 2*(temp1);
  temp2 = input[6] - input[10];

  step[ 6] = temp2 - temp1;
  step[ 7] = temp2 + temp1;

  // for odd input
  temp1 = input[3]*C12;
  temp2 = input[13]*C4;
  temp1 += temp2;
  temp1 = (temp1);
  temp1 *= C8;
  intermediate[ 8] = 2*(temp1);

  temp1 = input[3]*C4;
  temp2 = input[13]*C12;
  temp2 -= temp1;
  temp2 = (temp2);
  temp2 *= C8;
  intermediate[ 9] = 2*(temp2);

  intermediate[10] = 2*(input[9]*C8);
  intermediate[11] = input[15] - input[1];
  intermediate[12] = input[15] + input[1];
  intermediate[13] = 2*((input[7]*C8));

  temp1 = input[11]*C12;
  temp2 = input[5]*C4;
  temp2 -= temp1;
  temp2 = (temp2);
  temp2 *= C8;
  intermediate[14] = 2*(temp2);

  temp1 = input[11]*C4;
  temp2 = input[5]*C12;
  temp1 += temp2;
  temp1 = (temp1);
  temp1 *= C8;
  intermediate[15] = 2*(temp1);

  step[ 8] = intermediate[ 8] + intermediate[14];
  step[ 9] = intermediate[ 9] + intermediate[15];
  step[10] = intermediate[10] + intermediate[11];
  step[11] = intermediate[10] - intermediate[11];
  step[12] = intermediate[12] + intermediate[13];
  step[13] = intermediate[12] - intermediate[13];
  step[14] = intermediate[ 8] - intermediate[14];
  step[15] = intermediate[ 9] - intermediate[15];

  // step 3
  output[0] = step[ 0] + step[ 3];
  output[1] = step[ 1] + step[ 2];
  output[2] = step[ 1] - step[ 2];
  output[3] = step[ 0] - step[ 3];

  temp1 = step[ 4]*C14;
  temp2 = step[ 7]*C2;
  temp1 -= temp2;
  output[4] =  (temp1);

  temp1 = step[ 4]*C2;
  temp2 = step[ 7]*C14;
  temp1 += temp2;
  output[7] =  (temp1);

  temp1 = step[ 5]*C10;
  temp2 = step[ 6]*C6;
  temp1 -= temp2;
  output[5] =  (temp1);

  temp1 = step[ 5]*C6;
  temp2 = step[ 6]*C10;
  temp1 += temp2;
  output[6] =  (temp1);

  output[8] = step[ 8] + step[11];
  output[9] = step[ 9] + step[10];
  output[10] = step[ 9] - step[10];
  output[11] = step[ 8] - step[11];
  output[12] = step[12] + step[15];
  output[13] = step[13] + step[14];
  output[14] = step[13] - step[14];
  output[15] = step[12] - step[15];

  // output 4
  step[ 0] = output[0] + output[7];
  step[ 1] = output[1] + output[6];
  step[ 2] = output[2] + output[5];
  step[ 3] = output[3] + output[4];
  step[ 4] = output[3] - output[4];
  step[ 5] = output[2] - output[5];
  step[ 6] = output[1] - output[6];
  step[ 7] = output[0] - output[7];

  temp1 = output[8]*C7;
  temp2 = output[15]*C9;
  temp1 -= temp2;
  step[ 8] = (temp1);

  temp1 = output[9]*C11;
  temp2 = output[14]*C5;
  temp1 += temp2;
  step[ 9] = (temp1);

  temp1 = output[10]*C3;
  temp2 = output[13]*C13;
  temp1 -= temp2;
  step[10] = (temp1);

  temp1 = output[11]*C15;
  temp2 = output[12]*C1;
  temp1 += temp2;
  step[11] = (temp1);

  temp1 = output[11]*C1;
  temp2 = output[12]*C15;
  temp2 -= temp1;
  step[12] = (temp2);

  temp1 = output[10]*C13;
  temp2 = output[13]*C3;
  temp1 += temp2;
  step[13] = (temp1);

  temp1 = output[9]*C5;
  temp2 = output[14]*C11;
  temp2 -= temp1;
  step[14] = (temp2);

  temp1 = output[8]*C9;
  temp2 = output[15]*C7;
  temp1 += temp2;
  step[15] = (temp1);

  // step 5
  output[0] = (step[0] + step[15]);
  output[1] = (step[1] + step[14]);
  output[2] = (step[2] + step[13]);
  output[3] = (step[3] + step[12]);
  output[4] = (step[4] + step[11]);
  output[5] = (step[5] + step[10]);
  output[6] = (step[6] + step[ 9]);
  output[7] = (step[7] + step[ 8]);

  output[15] = (step[0] - step[15]);
  output[14] = (step[1] - step[14]);
  output[13] = (step[2] - step[13]);
  output[12] = (step[3] - step[12]);
  output[11] = (step[4] - step[11]);
  output[10] = (step[5] - step[10]);
  output[9] = (step[6] - step[ 9]);
  output[8] = (step[7] - step[ 8]);
}

/*void reference_16x16_idct_1d(double input[16], double output[16]) {
  const double kPi = 3.141592653589793238462643383279502884;
  const double kSqrt2 = 1.414213562373095048801688724209698;
  for (int k = 0; k < 16; k++) {
    output[k] = 0.0;
    for (int n = 0; n < 16; n++) {
      output[k] += input[n]*cos(kPi*(2*k+1)*n/32.0);
      if (n == 0)
        output[k] = output[k]/kSqrt2;
    }
  }
}*/

void vp8_short_idct16x16_c(short *input, short *output, int pitch) {
  double out[16*16], out2[16*16];
  const int short_pitch = pitch >> 1;
  int i, j;
    // First transform rows
  for (i = 0; i < 16; ++i) {
    double temp_in[16], temp_out[16];
    for (j = 0; j < 16; ++j)
      temp_in[j] = input[j + i*short_pitch];
    butterfly_16x16_idct_1d(temp_in, temp_out);
    for (j = 0; j < 16; ++j)
      out[j + i*16] = temp_out[j];
  }
  // Then transform columns
  for (i = 0; i < 16; ++i) {
    double temp_in[16], temp_out[16];
    for (j = 0; j < 16; ++j)
      temp_in[j] = out[j*16 + i];
    butterfly_16x16_idct_1d(temp_in, temp_out);
    for (j = 0; j < 16; ++j)
      out2[j*16 + i] = temp_out[j];
  }
  for (i = 0; i < 16*16; ++i)
    output[i] = round(out2[i]/128);
}
#endif
