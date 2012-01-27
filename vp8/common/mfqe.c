/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


/* MFQE: Multiframe Quality Enhancement
 * In rate limited situations keyframes may cause significant visual artifacts
 * commonly referred to as "popping." This file implements a postproccesing
 * algorithm which blends data from the preceeding frame when there is no
 * motion.
 */

/* TODO: Prune includes */
#include "vpx_config.h"
#include "vpx_rtcd.h"
#include "vpx_scale/yv12config.h"
#include "postproc.h"
#include "common.h"
#include "vpx_scale/yv12extend.h"
#include "vpx_scale/vpxscale.h"
#include "systemdependent.h"
#include "../encoder/variance.h"

#include <stdlib.h>
#include <limits.h>

static inline void filter_by_weight(unsigned char *src, int src_stride,
                                    unsigned char *dst, int dst_stride,
                                    int block_size, int src_weight)
{
    int dst_weight = (1 << MFQE_PRECISION) - src_weight;
    int rounding_bit = 1 << (MFQE_PRECISION - 1);
    int r, c;

    for (r = 0; r < block_size; r++)
    {
        for (c = 0; c < block_size; c++)
        {
            dst[c] = (src[c] * src_weight +
                      dst[c] * dst_weight +
                      rounding_bit) >> MFQE_PRECISION;
        }
        src += src_stride;
        dst += dst_stride;
    }
}

void vp8_filter_by_weight16x16_c(unsigned char *src, int src_stride,
                                 unsigned char *dst, int dst_stride,
                                 int src_weight)
{
    filter_by_weight(src, src_stride, dst, dst_stride, 16, src_weight);
}

void vp8_filter_by_weight8x8_c(unsigned char *src, int src_stride,
                               unsigned char *dst, int dst_stride,
                               int src_weight)
{
    filter_by_weight(src, src_stride, dst, dst_stride, 8, src_weight);
}

void vp8_filter_by_weight4x4_c(unsigned char *src, int src_stride,
                               unsigned char *dst, int dst_stride,
                               int src_weight)
{
    filter_by_weight(src, src_stride, dst, dst_stride, 4, src_weight);
}

void vp8_filter_by_weight2x2_c(unsigned char *src, int src_stride,
                               unsigned char *dst, int dst_stride,
                               int src_weight)
{
    filter_by_weight(src, src_stride, dst, dst_stride, 2, src_weight);
}

static inline void apply_ifactor(unsigned char *y_src,
                                 int y_src_stride,
                                 unsigned char *y_dst,
                                 int y_dst_stride,
                                 unsigned char *u_src,
                                 unsigned char *v_src,
                                 int uv_src_stride,
                                 unsigned char *u_dst,
                                 unsigned char *v_dst,
                                 int uv_dst_stride,
                                 int block_size,
                                 int src_weight)
{
    if (block_size == 16)
    {
        vp8_filter_by_weight16x16(y_src, y_src_stride, y_dst, y_dst_stride, src_weight);
        vp8_filter_by_weight8x8(u_src, uv_src_stride, u_dst, uv_dst_stride, src_weight);
        vp8_filter_by_weight8x8(v_src, uv_src_stride, v_dst, uv_dst_stride, src_weight);
    }
    else if (block_size == 8)
    {
        vp8_filter_by_weight8x8(y_src, y_src_stride, y_dst, y_dst_stride, src_weight);
        vp8_filter_by_weight4x4(u_src, uv_src_stride, u_dst, uv_dst_stride, src_weight);
        vp8_filter_by_weight4x4(v_src, uv_src_stride, v_dst, uv_dst_stride, src_weight);
    }
    else if (block_size == 4)
    {
        vp8_filter_by_weight4x4(y_src, y_src_stride, y_dst, y_dst_stride, src_weight);
        vp8_filter_by_weight2x2(u_src, uv_src_stride, u_dst, uv_dst_stride, src_weight);
        vp8_filter_by_weight2x2(v_src, uv_src_stride, v_dst, uv_dst_stride, src_weight);
    }
}

static void multiframe_quality_enhance_block
(
    int blksize, /* Currently only values supported are 16, 8, 4 */
    int qcurr,
    int qprev,
    unsigned char *y,
    unsigned char *u,
    unsigned char *v,
    int y_stride,
    int uv_stride,
    unsigned char *yd,
    unsigned char *ud,
    unsigned char *vd,
    int yd_stride,
    int uvd_stride
)
{
    static const unsigned char VP8_ZEROS[16]=
    {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };

    int uvblksize = blksize >> 1;
    /* is this safe? */
    int qdiff = qcurr - qprev;

    int i;
    unsigned char *yp;
    unsigned char *ydp;
    unsigned char *up;
    unsigned char *udp;
    unsigned char *vp;
    unsigned char *vdp;

    unsigned int act, sad, thr, sse;

    if (blksize == 16)
    {
        act = (vp8_variance16x16(yd, yd_stride, VP8_ZEROS, 0, &sse)+128)>>8;
        sad = (vp8_sad16x16(y, y_stride, yd, yd_stride, INT_MAX)+128)>>8;
    }
    else if (blksize == 8)
    {
        act = (vp8_variance8x8(yd, yd_stride, VP8_ZEROS, 0, &sse)+32)>>6;
        sad = (vp8_sad8x8(y, y_stride, yd, yd_stride, INT_MAX)+32)>>6;
    }
    else
    {
        act = (vp8_variance4x4(yd, yd_stride, VP8_ZEROS, 0, &sse)+8)>>4;
        sad = (vp8_sad4x4(y, y_stride, yd, yd_stride, INT_MAX)+8)>>4;
    }

    /* thr = qdiff/8 + log2(act) + log4(qprev) */
    thr = (qdiff>>3);
    while (act>>=1) thr++;
    while (qprev>>=2) thr++;

    if (sad < thr)
    {
        int ifactor = (sad << MFQE_PRECISION) / thr;
        ifactor >>= (qdiff >> 5);

        if (ifactor)
        {
            apply_ifactor(y, y_stride, yd, yd_stride,
                          u, v, uv_stride,
                          ud, vd, uvd_stride,
                          blksize, ifactor);
        }
    }
    else
    {
        if (blksize == 16)
        {
            vp8_copy_mem16x16(y, y_stride, yd, yd_stride);
            vp8_copy_mem8x8(u, uv_stride, ud, uvd_stride);
            vp8_copy_mem8x8(v, uv_stride, vd, uvd_stride);
        }
        else if (blksize == 8)
        {
            vp8_copy_mem8x8(y, y_stride, yd, yd_stride);

            /* consider adding 4x4 recon copy */
            for (up = u, udp = ud, i = 0; i < uvblksize; ++i, up += uv_stride, udp += uvd_stride)
                vpx_memcpy(udp, up, uvblksize);
            for (vp = v, vdp = vd, i = 0; i < uvblksize; ++i, vp += uv_stride, vdp += uvd_stride)
                vpx_memcpy(vdp, vp, uvblksize);
        }
        else
        {
            /* 4x4 recon copy plus 2x2? */
            for (yp = y, ydp = yd, i = 0; i < blksize; ++i, yp += y_stride, ydp += yd_stride)
                vpx_memcpy(ydp, yp, blksize);
            for (up = u, udp = ud, i = 0; i < uvblksize; ++i, up += uv_stride, udp += uvd_stride)
                vpx_memcpy(udp, up, uvblksize);
            for (vp = v, vdp = vd, i = 0; i < uvblksize; ++i, vp += uv_stride, vdp += uvd_stride)
                vpx_memcpy(vdp, vp, uvblksize);
        }
    }
}

void vp8_multiframe_quality_enhance
(
    VP8_COMMON *cm
)
{
    YV12_BUFFER_CONFIG *show = cm->frame_to_show;
    YV12_BUFFER_CONFIG *dest = &cm->post_proc_buffer;

    FRAME_TYPE frame_type = cm->frame_type;
    /* Point at base of Mb MODE_INFO list has motion vectors etc */
    const MODE_INFO *mode_info_context = cm->mi;
    int mb_row;
    int mb_col;
    int qcurr = cm->base_qindex;
    int qprev = cm->postproc_state.last_base_qindex;

    unsigned char *y_ptr, *u_ptr, *v_ptr;
    unsigned char *yd_ptr, *ud_ptr, *vd_ptr;

    /* Set up the buffer pointers */
    y_ptr = show->y_buffer;
    u_ptr = show->u_buffer;
    v_ptr = show->v_buffer;
    yd_ptr = dest->y_buffer;
    ud_ptr = dest->u_buffer;
    vd_ptr = dest->v_buffer;

    /* postprocess each macro block */
    for (mb_row = 0; mb_row < cm->mb_rows; mb_row++)
    {
        for (mb_col = 0; mb_col < cm->mb_cols; mb_col++)
        {
            /* if motion is high there will likely be no benefit */
            if (((frame_type == INTER_FRAME &&
                  abs(mode_info_context->mbmi.mv.as_mv.row) <= 10 &&
                  abs(mode_info_context->mbmi.mv.as_mv.col) <= 10) ||
                 (frame_type == KEY_FRAME)))
            {
                if (mode_info_context->mbmi.mode == B_PRED || mode_info_context->mbmi.mode == SPLITMV)
                {
                    int i, j;
                    for (i=0; i<2; ++i)
                        for (j=0; j<2; ++j)
                            multiframe_quality_enhance_block(8, qcurr, qprev,
                                                             y_ptr + 8*(i*show->y_stride+j),
                                                             u_ptr + 4*(i*show->uv_stride+j),
                                                             v_ptr + 4*(i*show->uv_stride+j),
                                                             show->y_stride,
                                                             show->uv_stride,
                                                             yd_ptr + 8*(i*dest->y_stride+j),
                                                             ud_ptr + 4*(i*dest->uv_stride+j),
                                                             vd_ptr + 4*(i*dest->uv_stride+j),
                                                             dest->y_stride,
                                                             dest->uv_stride);
                }
                else
                {
                    multiframe_quality_enhance_block(16, qcurr, qprev, y_ptr,
                                                     u_ptr, v_ptr,
                                                     show->y_stride,
                                                     show->uv_stride,
                                                     yd_ptr, ud_ptr, vd_ptr,
                                                     dest->y_stride,
                                                     dest->uv_stride);
                }
            }
            else
            {
                vp8_copy_mem16x16(y_ptr, show->y_stride, yd_ptr, dest->y_stride);
                vp8_copy_mem8x8(u_ptr, show->uv_stride, ud_ptr, dest->uv_stride);
                vp8_copy_mem8x8(v_ptr, show->uv_stride, vd_ptr, dest->uv_stride);
            }
            y_ptr += 16;
            u_ptr += 8;
            v_ptr += 8;
            yd_ptr += 16;
            ud_ptr += 8;
            vd_ptr += 8;
            mode_info_context++;     /* step to next MB */
        }

        y_ptr += show->y_stride  * 16 - 16 * cm->mb_cols;
        u_ptr += show->uv_stride *  8 - 8 * cm->mb_cols;
        v_ptr += show->uv_stride *  8 - 8 * cm->mb_cols;
        yd_ptr += dest->y_stride  * 16 - 16 * cm->mb_cols;
        ud_ptr += dest->uv_stride *  8 - 8 * cm->mb_cols;
        vd_ptr += dest->uv_stride *  8 - 8 * cm->mb_cols;

        mode_info_context++;         /* Skip border mb */
    }
}

/* Calculate the SAD between src1 and src2 as well as the variance across src2
 * and return both variables via pointers
 *
 * Variance is calculated for 16x16, 8x8 and 4x4.
 * Combinations such as 16x8 are not supported.
 *
 * Although we have existing functions for SAD and for variance it was deemed
 * reasonable to reimplement them here. The SAD is identical but the variance is
 * only across a single block. It can be calculated by setting the second block
 * to 0. However, these functions are currently only required by the encoder.
 * Using them here would require moving them to "common" and including them in
 * the decoder even when they are not used.
 *
 * By reimplementing them we can do both calculations at once and keep the SAD
 * and variance functions out of the decoder at least if postproc is disabled.
 */
/* FOR REFERENCE: staying with built-in sad/variance for now
  vp8_variance_and_sad_16x16(y, y_stride, yd, yd_stride, &act, &sad);
static inline void variance_and_sad(unsigned char *src1, int stride1,
                                    unsigned char *src2, int stride2,
                                    int block_size, unsigned int *sum_src2,
                                    unsigned int *sum_squared_src2,
                                    unsigned int *sad)
{
  int r, c;
  unsigned int sum_src2_tmp = 0,
               sum_squared_src2_tmp = 0,
               sad_tmp = 0;

  for (r = 0; r < block_size; r++)
  {
    for (c = 0; c < block_size; c++)
    {
      sad_tmp += abs(src1[c] - src2[c]);

      sum_src2_tmp += src2[c];
      sum_squared_src2_tmp += src2[c] * src2[c];
    }
    src1 += stride1;
    src2 += stride2;
  }

  *sad = sad_tmp;
  *sum_src2 = sum_src2_tmp;
  *sum_squared_src2 = sum_squared_src2_tmp;
}

void vp8_variance_and_sad_16x16_c(unsigned char *src1, int stride1,
                                  unsigned char *src2, int stride2,
                                  unsigned int *variance, unsigned int *sad)
{
    unsigned int sum_src2 = 0,
                 sum_squared_src2 = 0;

    variance_and_sad(src1, stride1, src2, stride2, 16,
                     &sum_src2, &sum_squared_src2, sad);

    *variance = sum_squared_src2 - ((sum_src2 * sum_src2) >> 8);
    *variance = (*variance + 128) >> 8;

    *sad = (*sad + 128) >> 8;
}

static void variance_and_sad_8x8_c(unsigned char *src1, int stride1,
                                   unsigned char *src2, int stride2,
                                   unsigned int *variance, unsigned int *sad)
{
    unsigned int sum_src2 = 0,
                 sum_squared_src2 = 0;

    variance_and_sad(src1, stride1, src2, stride2, 8,
                     &sum_src2, &sum_squared_src2, sad);

    *variance = sum_squared_src2 - ((sum_src2 * sum_src2) >> 6);
    *variance = (*variance + 32) >> 6;

    *sad = (*sad + 32) >> 6;
}

static void variance_and_sad_4x4_c(unsigned char *src1, int stride1,
                                   unsigned char *src2, int stride2,
                                   unsigned int *variance, unsigned int *sad)
{
    unsigned int sum_src2 = 0,
                 sum_squared_src2 = 0;

    variance_and_sad(src1, stride1, src2, stride2, 4,
                     &sum_src2, &sum_squared_src2, sad);

    *variance = sum_squared_src2 - ((sum_src2 * sum_src2) >> 4);
    *variance = (*variance + 8) >> 4;

    *sad = (*sad + 8) >> 4;
}
*/
