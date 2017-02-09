/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <errno.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <y4minput.h>
#include "vpx/vpx_codec.h"
#include "vpx/vpx_integer.h"

void vp8_ssim_parms_8x8_c(unsigned char *s, int sp, unsigned char *r, int rp,
                          uint32_t *sum_s, uint32_t *sum_r, uint32_t *sum_sq_s,
                          uint32_t *sum_sq_r, uint32_t *sum_sxr) {
  int i, j;
  for (i = 0; i < 8; i++, s += sp, r += rp) {
    for (j = 0; j < 8; j++) {
      *sum_s += s[j];
      *sum_r += r[j];
      *sum_sq_s += s[j] * s[j];
      *sum_sq_r += r[j] * r[j];
      *sum_sxr += s[j] * r[j];
    }
  }
}

static const int64_t cc1 = 26634;   // (64^2*(.01*255)^2
static const int64_t cc2 = 239708;  // (64^2*(.03*255)^2

static double similarity(uint32_t sum_s, uint32_t sum_r, uint32_t sum_sq_s,
                         uint32_t sum_sq_r, uint32_t sum_sxr, int count) {
  int64_t ssim_n, ssim_d;
  int64_t c1, c2;

  // scale the constants by number of pixels
  c1 = (cc1 * count * count) >> 12;
  c2 = (cc2 * count * count) >> 12;

  ssim_n = (2 * sum_s * sum_r + c1) *
           ((int64_t)2 * count * sum_sxr - (int64_t)2 * sum_s * sum_r + c2);

  ssim_d = (sum_s * sum_s + sum_r * sum_r + c1) *
           ((int64_t)count * sum_sq_s - (int64_t)sum_s * sum_s +
            (int64_t)count * sum_sq_r - (int64_t)sum_r * sum_r + c2);

  return ssim_n * 1.0 / ssim_d;
}

static double ssim_8x8(unsigned char *s, int sp, unsigned char *r, int rp) {
  uint32_t sum_s = 0, sum_r = 0, sum_sq_s = 0, sum_sq_r = 0, sum_sxr = 0;
  vp8_ssim_parms_8x8_c(s, sp, r, rp, &sum_s, &sum_r, &sum_sq_s, &sum_sq_r,
                       &sum_sxr);
  return similarity(sum_s, sum_r, sum_sq_s, sum_sq_r, sum_sxr, 64);
}

// We are using a 8x8 moving window with starting location of each 8x8 window
// on the 4x4 pixel grid. Such arrangement allows the windows to overlap
// block boundaries to penalize blocking artifacts.
double vp8_ssim2(unsigned char *img1, unsigned char *img2, int stride_img1,
                 int stride_img2, int width, int height) {
  int i, j;
  int samples = 0;
  double ssim_total = 0;

  // sample point start with each 4x4 location
  for (i = 0; i <= height - 8;
       i += 4, img1 += stride_img1 * 4, img2 += stride_img2 * 4) {
    for (j = 0; j <= width - 8; j += 4) {
      double v = ssim_8x8(img1 + j, stride_img1, img2 + j, stride_img2);
      ssim_total += v;
      samples++;
    }
  }
  ssim_total /= samples;
  return ssim_total;
}

static uint64_t calc_plane_error(uint8_t *orig, int orig_stride, uint8_t *recon,
                                 int recon_stride, unsigned int cols,
                                 unsigned int rows) {
  unsigned int row, col;
  uint64_t total_sse = 0;
  int diff;

  for (row = 0; row < rows; row++) {
    for (col = 0; col < cols; col++) {
      diff = orig[col] - recon[col];
      total_sse += diff * diff;
    }

    orig += orig_stride;
    recon += recon_stride;
  }
  return total_sse;
}

#define MAX_PSNR 100

double vp9_mse2psnr(double samples, double peak, double mse) {
  double psnr;

  if (mse > 0.0)
    psnr = 10.0 * log10(peak * peak * samples / mse);
  else
    psnr = MAX_PSNR;  // Limit to prevent / 0

  if (psnr > MAX_PSNR) psnr = MAX_PSNR;

  return psnr;
}

// Open a file and determine if its y4m or raw.  If y4m get the header.
FILE *open_y4m_or_raw(char *file_name, int *is_y4m, y4m_input *y4m) {
  char y4m_buf[4];
  FILE *f;
  size_t r1;
  *is_y4m = 0;
  f = strcmp(file_name, "-") ? fopen(file_name, "rb") : stdin;
  r1 = fread(y4m_buf, 1, 4, f);
  if (r1 == 4) {
    if (memcmp(y4m_buf, "YUV4", 4) == 0) *is_y4m = 1;
    if (*is_y4m)
      y4m_input_open(y4m, f, y4m_buf, 4, 0);
    else
      fseek(f, -4, SEEK_CUR);
  }
  return f;
}

int main(int argc, char *argv[]) {
  FILE *f[2];
  uint8_t *buf[2] = { NULL, NULL };
  int w, h, n_frames, tl_skip = 0, tl_skips_remaining = 0;
  double ssim = 0, psnravg = 0, psnrglb = 0;
  double ssimy, ssimu, ssimv;
  uint64_t psnry, psnru, psnrv;
  y4m_input y4m[2];
  int is_y4m[2] = { 0, 0 };
  vpx_image_t img[2];

  if (argc < 2) {
    fprintf(stderr,
            "Usage: %s file1.{yuv|y4m} file2.{yuv|y4m"
            "[WxH tl_skip={0,1,3}]\n",
            argv[0]);
    return 1;
  }
  f[0] = open_y4m_or_raw(argv[1], &is_y4m[0], &y4m[0]);
  w = y4m[0].pic_w;
  h = y4m[0].pic_h;
  f[1] = open_y4m_or_raw(argv[2], &is_y4m[1], &y4m[1]);

  if (argc > 3) {
    sscanf(argv[3], "%dx%d", &w, &h);
  }

  // Number of frames to skip from file1.yuv for every frame used. Normal values
  // 0, 1 and 3 correspond to TL2, TL1 and TL0 respectively for a 3TL encoding
  // in mode 10. 7 would be reasonable for comparing TL0 of a 4-layer encoding.
  if (argc > 4) {
    sscanf(argv[4], "%d", &tl_skip);
  }

  if (!f[0] || !f[1]) {
    fprintf(stderr, "Could not open input files: %s\n", strerror(errno));
    return 1;
  }
  if (w <= 0 || h <= 0 || w & 1 || h & 1) {
    fprintf(stderr, "Invalid size %dx%d\n", w, h);
    return 1;
  }
  if (is_y4m[0] == 0) buf[0] = malloc(w * h * 3 / 2);
  if (is_y4m[1] == 0) buf[1] = malloc(w * h * 3 / 2);
  n_frames = 0;
  while (1) {
    size_t r1, r2;
    unsigned char *y[2], *u[2], *v[2];
    if (is_y4m[0] == 1) {
      r1 = y4m_input_fetch_frame(&y4m[0], f[0], &img[0]);
      y[0] = img[0].planes[0];
      u[0] = img[0].planes[1];
      v[0] = img[0].planes[2];
    } else {
      r1 = fread(buf[0], w * h * 3 / 2, 1, f[0]);
      y[0] = buf[0];
      u[0] = buf[0] + w * h;
      v[0] = buf[0] + 5 * w * h / 4;
    }

    if (r1) {
      // Reading parts of file1.yuv that were not used in temporal layer.
      if (tl_skips_remaining > 0) {
        --tl_skips_remaining;
        continue;
      }
      // Use frame, but skip |tl_skip| after it.
      tl_skips_remaining = tl_skip;
    }

    if (is_y4m[1] == 1) {
      r2 = y4m_input_fetch_frame(&y4m[1], f[1], &img[1]);
      y[1] = img[1].planes[0];
      u[1] = img[1].planes[1];
      v[1] = img[1].planes[2];
    } else {
      r2 = fread(buf[1], w * h * 3 / 2, 1, f[1]);
      y[1] = buf[1];
      u[1] = buf[1] + w * h;
      v[1] = buf[1] + 5 * w * h / 4;
    }

    if (r1 && r2 && r1 != r2) {
      fprintf(stderr, "Failed to read data: %s [%d/%d]\n", strerror(errno),
              (int)r1, (int)r2);
      return 1;
    } else if (r1 == 0 || r2 == 0) {
      break;
    }
#define psnr_and_ssim(ssim, psnr, buf0, buf1, w, h) \
  ssim = vp8_ssim2(buf0, buf1, w, w, w, h);         \
  psnr = calc_plane_error(buf0, w, buf1, w, w, h);

    psnr_and_ssim(ssimy, psnry, y[0], y[1], w, h);
    psnr_and_ssim(ssimu, psnru, u[0], u[1], w / 2, h / 2);
    psnr_and_ssim(ssimv, psnrv, v[0], v[1], w / 2, h / 2);
    ssim += 0.8 * ssimy + 0.1 * (ssimu + ssimv);
    psnravg +=
        vp9_mse2psnr(w * h * 6 / 4, 255.0, (double)psnry + psnru + psnrv);
    psnrglb += psnry + psnru + psnrv;
    n_frames++;
  }
  if (is_y4m[0] == 0) free(buf[0]);
  if (is_y4m[1] == 0) free(buf[1]);
  if (is_y4m[0] == 1) vpx_img_free(&img[0]);
  if (is_y4m[1] == 1) vpx_img_free(&img[1]);

  ssim /= n_frames;
  psnravg /= n_frames;
  psnrglb = vp9_mse2psnr((double)n_frames * w * h * 6 / 4, 255.0, psnrglb);

  printf("AvgPSNR: %lf\n", psnravg);
  printf("GlbPSNR: %lf\n", psnrglb);
  printf("SSIM: %lf\n", 100 * pow(ssim, 8.0));
  printf("Nframes: %d\n", n_frames);

  if (strcmp(argv[1], "-")) fclose(f[0]);
  if (strcmp(argv[2], "-")) fclose(f[1]);

  return 0;
}
