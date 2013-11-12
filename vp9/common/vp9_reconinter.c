/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <assert.h>

#include "./vpx_scale_rtcd.h"
#include "./vpx_config.h"

#include "vpx/vpx_integer.h"

#include "vp9/common/vp9_blockd.h"
#include "vp9/common/vp9_filter.h"
#include "vp9/common/vp9_reconinter.h"
#include "vp9/common/vp9_reconintra.h"

extern int frame_width;
extern int frame_height;


static void reset_boarder(uint8_t *const src, int src_stride,
                         int width, int height,
                         int extend_top, int extend_left,
                         int extend_bottom, int extend_right) {
  int i;
  const int linesize = extend_left + extend_right + width;

  /* copy the left and right most columns out */
  uint8_t *src_ptr1 = src - extend_left;
  uint8_t *src_ptr2 = src + width - 1;
  uint8_t *dst_ptr1 = src - extend_left;
  uint8_t *dst_ptr2 = src + width;

  for (i = 0; i < extend_top; ++i) {
    if (i !=  0)
      vpx_memset(src_ptr1, 0, linesize);
    src_ptr1 -= src_stride;
  }
  return;

  *src_ptr1 = src + src_stride * (height - 1);

      for (i = 0; i < extend_top; ++i) {
        if (i != 0) vpx_memset(src_ptr1, 0, linesize);
        src_ptr1 += src_stride;
      }

     return;

      /* copy the left and right most columns out */
      src_ptr1 = src - extend_left;
      src_ptr2 = src + width;


      for (i = 0; i < height; ++i) {
        vpx_memset(src_ptr1, 0, extend_left);
        vpx_memset(src_ptr2, 0, extend_right);
        src_ptr1 += src_stride;
        src_ptr2 += src_stride;
      }
}

void vp9_setup_interp_filters(MACROBLOCKD *xd,
                              INTERPOLATION_TYPE mcomp_filter_type,
                              VP9_COMMON *cm) {
  if (xd->mi_8x8 && xd->mi_8x8[0]) {
    MB_MODE_INFO *const mbmi = &xd->mi_8x8[0]->mbmi;

    set_scale_factors(xd, mbmi->ref_frame[0] - LAST_FRAME,
                          mbmi->ref_frame[1] - LAST_FRAME,
                          cm->active_ref_scale);
  } else {
    set_scale_factors(xd, -1, -1, cm->active_ref_scale);
  }

  xd->subpix.filter_x = xd->subpix.filter_y =
      vp9_get_filter_kernel(mcomp_filter_type == SWITCHABLE ?
                               EIGHTTAP : mcomp_filter_type);

  assert(((intptr_t)xd->subpix.filter_x & 0xff) == 0);
}

static void inter_predictor(const uint8_t *src, int src_stride,
                            uint8_t *dst, int dst_stride,
                            const MV32 *mv,
                            const struct scale_factors *scale,
                            int w, int h, int ref,
                            const struct subpix_fn_table *subpix,
                            int xs, int ys) {
  const int subpel_x = mv->col & SUBPEL_MASK;
  const int subpel_y = mv->row & SUBPEL_MASK;
  int count = 0, i, j;
  uint8_t *src_ = src;
  src += (mv->row >> SUBPEL_BITS) * src_stride + (mv->col >> SUBPEL_BITS);
  int test = mv->row >> SUBPEL_BITS;


  for (i = 0; i < h; i++) {
    for (j = 0; j < w; j++)
      if (*(src + src_stride*i + j) == 0)
        count = 0;
  }


  scale->sfc->predict[subpel_x != 0][subpel_y != 0][ref](
      src, src_stride, dst, dst_stride,
      subpix->filter_x[subpel_x], xs,
      subpix->filter_y[subpel_y], ys,
      w, h);
}

/***************************************************
 * ondemand_boarder_extend implements on demand
 * boarder extension. It extends the boarder when
 * the reference block is out of the reference frame
 * boundary. Below is a sketch of the coordinates in
 * the function.
   |<------------ frame_width ----------->|
   |_ ____________________________________|____
   |(0,0)                                 |  ^
   |                                      |  |
   |     (x0,y0)___(x1,y0)                |  |
   |           |   |                      |  |
   |     (x0,y1|___|(x1,y1)               |  |
   |                                      | frame_height
   |                                      |  |
   |                                      |  |
   |                                      |  |
   |                                      |  |
   |_ ____________________________________|__|
***************************************************/
static void ondemand_boarder_extend(struct buf_2d *const pre_buf,
                                     const MACROBLOCKD *xd,
                                     const MV32 *scaled_mv,
                                     int bw, int bh, int ss_x,
                                     int ss_y) {
  const int frame_width = -xd->mb_to_left_edge / (8 << ss_x) + bw
                          + xd->mb_to_right_edge / (8 << ss_y);
  const int frame_height = -xd->mb_to_top_edge / (8 << ss_x) + bh
                           + xd->mb_to_bottom_edge / (8 << ss_y);
  const uint8_t *ref_frame = NULL, *src_ptr = NULL, *dst_ptr = NULL;
  int x0, y0, x1, y1, i;
  int factor = 20;

  // get the current block position
  x0 = -xd->mb_to_left_edge / (8 << ss_x);
  y0 = -xd->mb_to_top_edge / (8 << ss_y);

  // get reference frame pointer
  ref_frame = pre_buf->buf - (y0 * pre_buf->stride + x0);

  // get reference block position in reference frame based on scaled_mv
  // get reference block top left pixel coordinate
  x0 = (-xd->mb_to_left_edge + scaled_mv->col / 2) / (8 << ss_x);
  y0 = (-xd->mb_to_top_edge + scaled_mv->row / 2) / (8 << ss_y);

  // get reference block bottom right pixel coordinate
  x1 = x0 + bw;
  y1 = y0 + bh;

  // extend top boarder if necessary
  if (y0 <= 5) {
    uint8_t extend_top = y0 <= 0 ? factor : -y0 + factor;
    src_ptr = ref_frame + (x0 < 0 ? 0 : x0);
    dst_ptr = src_ptr - pre_buf->stride * (extend_top);
    for (i = 0; i < extend_top; i++) {
      vpx_memcpy(dst_ptr, src_ptr, bw);
      dst_ptr += pre_buf->stride;
    }
  }

  // extend bottom boarder if necessary
  if (y1 >= (frame_height - 5)) {
    uint8_t extend_bottom =
             y1 <= frame_height ? factor : - frame_height + factor;
    src_ptr = ref_frame +
             pre_buf->stride * (frame_height - 1) + (x0 < 0 ? 0 : x0);
    dst_ptr = src_ptr + pre_buf->stride;
    for (i = 0; i < extend_bottom; i++) {
      vpx_memcpy(dst_ptr, src_ptr, bw);
      dst_ptr += pre_buf->stride;
    }
  }

  // extend left boarder if necessary
  if (x0 <= 5) {
    uint8_t extend_left = x0 <= 0 ? factor : -x0 + factor;
    dst_ptr = ref_frame + pre_buf->stride * (y0-1) - extend_left;
    src_ptr = ref_frame + pre_buf->stride * (y0-1);
    for (i = 0; i < bh; i++) {
      vpx_memset(dst_ptr, *src_ptr, extend_left);
      src_ptr += pre_buf->stride;
      dst_ptr += pre_buf->stride;
    }
  }

  // extend right boarder if necessary
  if (x1 >= (frame_width-5)) {
    uint8_t extend_right =
               x1 <= frame_width ? factor : x1 - frame_width + factor;
    src_ptr = ref_frame + pre_buf->stride * (y0 - 1) + frame_width - 1;
    dst_ptr = src_ptr;
    for (i = 0; i < bh; i++) {
      vpx_memset(dst_ptr, *src_ptr, extend_right);
      src_ptr += pre_buf->stride;
      dst_ptr += pre_buf->stride;
    }
  }
}
void vp9_build_inter_predictor(const uint8_t *src, int src_stride,
                               uint8_t *dst, int dst_stride,
                               const MV *src_mv,
                               const struct scale_factors *scale,
                               int w, int h, int ref,
                               const struct subpix_fn_table *subpix,
                               enum mv_precision precision) {
  const int is_q4 = precision == MV_PRECISION_Q4;
  const MV mv_q4 = { is_q4 ? src_mv->row : src_mv->row * 2,
                     is_q4 ? src_mv->col : src_mv->col * 2 };
  const struct scale_factors_common *sfc = scale->sfc;
  const MV32 mv = sfc->scale_mv(&mv_q4, scale);

  inter_predictor(src, src_stride, dst, dst_stride, &mv, scale,
                  w, h, ref, subpix, sfc->x_step_q4, sfc->y_step_q4);
}

static INLINE int round_mv_comp_q4(int value) {
  return (value < 0 ? value - 2 : value + 2) / 4;
}

static MV mi_mv_pred_q4(const MODE_INFO *mi, int idx) {
  MV res = { round_mv_comp_q4(mi->bmi[0].as_mv[idx].as_mv.row +
                              mi->bmi[1].as_mv[idx].as_mv.row +
                              mi->bmi[2].as_mv[idx].as_mv.row +
                              mi->bmi[3].as_mv[idx].as_mv.row),
             round_mv_comp_q4(mi->bmi[0].as_mv[idx].as_mv.col +
                              mi->bmi[1].as_mv[idx].as_mv.col +
                              mi->bmi[2].as_mv[idx].as_mv.col +
                              mi->bmi[3].as_mv[idx].as_mv.col) };
  return res;
}

// TODO(jkoleszar): yet another mv clamping function :-(
MV clamp_mv_to_umv_border_sb(const MACROBLOCKD *xd, const MV *src_mv,
                             int bw, int bh, int ss_x, int ss_y) {
  // If the MV points so far into the UMV border that no visible pixels
  // are used for reconstruction, the subpel part of the MV can be
  // discarded and the MV limited to 16 pixels with equivalent results.
  const int spel_left = (VP9_INTERP_EXTEND + bw) << SUBPEL_BITS;
  const int spel_right = spel_left - SUBPEL_SHIFTS;
  const int spel_top = (VP9_INTERP_EXTEND + bh) << SUBPEL_BITS;
  const int spel_bottom = spel_top - SUBPEL_SHIFTS;
  MV clamped_mv = {
    src_mv->row * (1 << (1 - ss_y)),
    src_mv->col * (1 << (1 - ss_x))
  };
  assert(ss_x <= 1);
  assert(ss_y <= 1);

  clamp_mv(&clamped_mv,
           xd->mb_to_left_edge * (1 << (1 - ss_x)) - spel_left,
           xd->mb_to_right_edge * (1 << (1 - ss_x)) + spel_right,
           xd->mb_to_top_edge * (1 << (1 - ss_y)) - spel_top,
           xd->mb_to_bottom_edge * (1 << (1 - ss_y)) + spel_bottom);

  return clamped_mv;
}

struct build_inter_predictors_args {
  MACROBLOCKD *xd;
  int x, y;
};


static void build_inter_predictors(int plane, int block, BLOCK_SIZE bsize,
                                   int pred_w, int pred_h,
                                   void *argv) {
  const struct build_inter_predictors_args* const arg = argv;
  MACROBLOCKD *const xd = arg->xd;
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const int bwl = b_width_log2(bsize) - pd->subsampling_x;
  const int bw = 4 << bwl;
  const int bh = plane_block_height(bsize, pd);
  const int x = 4 * (block & ((1 << bwl) - 1));
  const int y = 4 * (block >> bwl);
  const MODE_INFO *mi = xd->mi_8x8[0];
  const int is_compound = has_second_ref(&mi->mbmi);
  int ref;
  int frame_width2;
  int frame_height2;
  int frame_width3;
  int frame_height3;
  uint8_t *ref_frame = NULL, *src_ptr = NULL, *dst_ptr = NULL, *dst_ptr1 = NULL;
  int x0, y0, x1, y1, i;
  int x0_, y0_, x1_, y1_;
  int factor = 20;
  int ss_x, ss_y;
  int num = 0;
  int test;

  assert(x < bw);
  assert(y < bh);
  assert(mi->mbmi.sb_type < BLOCK_8X8 || 4 << pred_w == bw);
  assert(mi->mbmi.sb_type < BLOCK_8X8 || 4 << pred_h == bh);

  for (ref = 0; ref < 1 + is_compound; ++ref) {
    struct scale_factors *const scale = &xd->scale_factor[ref];
    struct buf_2d *const pre_buf = &pd->pre[ref];
    struct buf_2d *const dst_buf = &pd->dst;
    uint8_t *const dst = dst_buf->buf + dst_buf->stride * y + x;

    // TODO(jkoleszar): All chroma MVs in SPLITMV mode are taken as the
    // same MV (the average of the 4 luma MVs) but we could do something
    // smarter for non-4:2:0. Just punt for now, pending the changes to get
    // rid of SPLITMV mode entirely.
    const MV mv = mi->mbmi.sb_type < BLOCK_8X8
               ? (plane == 0 ? mi->bmi[block].as_mv[ref].as_mv
                             : mi_mv_pred_q4(mi, ref))
               : mi->mbmi.mv[ref].as_mv;

    // TODO(jkoleszar): This clamping is done in the incorrect place for the
    // scaling case. It needs to be done on the scaled MV, not the pre-scaling
    // MV. Note however that it performs the subsampling aware scaling so
    // that the result is always q4.
    // mv_precision precision is MV_PRECISION_Q4.
    const MV mv_q4 = clamp_mv_to_umv_border_sb(xd, &mv, bw, bh,
                                               pd->subsampling_x,
                                               pd->subsampling_y);

    uint8_t *pre;
    MV32 scaled_mv;
    int xs, ys;

    if (vp9_is_scaled(scale->sfc)) {
      pre = pre_buf->buf + scaled_buffer_offset(x, y, pre_buf->stride, scale);
      scale->sfc->set_scaled_offsets(scale, arg->y + y, arg->x + x);
      scaled_mv = scale->sfc->scale_mv(&mv_q4, scale);
      xs = scale->sfc->x_step_q4;
      ys = scale->sfc->y_step_q4;
    } else {
      pre = pre_buf->buf + (y * pre_buf->stride + x);
      scaled_mv.row = mv_q4.row;
      scaled_mv.col = mv_q4.col;
      xs = ys = 16;
    }
    ss_x = pd->subsampling_x;
    ss_y = pd->subsampling_y;

    frame_width3 = -xd->mb_to_left_edge / (8 << ss_x) + bw
                           + xd->mb_to_right_edge / (8 << ss_y);
    frame_height3 = -xd->mb_to_top_edge / (8 << ss_x) + bh
                             + xd->mb_to_bottom_edge / (8 << ss_y);

    if (plane == 0) {
      frame_width2 = xd->ref_buf[ref]->y_crop_width;
      frame_height2 = xd->ref_buf[ref]->y_crop_height;
      ref_frame = xd->ref_buf[ref]->y_buffer;
    } else {
      frame_width2 = xd->ref_buf[ref]->uv_crop_width;
      frame_height2 = xd->ref_buf[ref]->uv_crop_height;
      ref_frame =  plane == 1 ? xd->ref_buf[ref]->u_buffer : xd->ref_buf[ref]->v_buffer;
    }
    //printf("----------%d ----------------%d\n", frame_width, frame_height);
    if (frame_width !=352) {
      test = 0;
    }

    // get the current block position
    x0 = -xd->mb_to_left_edge / (8 << ss_x);
    y0 = -xd->mb_to_top_edge / (8 << ss_y);

    // get reference frame pointer

    //reset_boarder(ref_frame, pre_buf->stride, frame_width2,
           //                   frame_height2, 4, 4, 4, 4);

    // get reference block position in reference frame based on scaled_mv
    // get reference block top left pixel coordinate
    x0 = (-xd->mb_to_left_edge + (mv_q4.col >> (1 - ss_x))) / (8 << ss_x);
    y0 = (-xd->mb_to_top_edge + (mv_q4.row >> (1 - ss_y))) / (8 << ss_y);


    // get reference block bottom right pixel coordinate
    x1 = x0 + bw;
    y1 = y0 + bh;

    x0_ = x0;
    y0_ = y0;
    x1_ = x1;
    y1_ = y1;

    x0 -= 4;
    y0 -= 4;
    x1 += 4;
    y1 += 4;

    /*if (vp9_is_scaled(scale->sfc)) {
      ref_frame = xd->ref_buf[ref];
    }*/

    // extend top boarder if necessary
    if (y0 <= 0) {
      num |= 1 << 1;
      int j;
      uint8_t extend_top = -y0;
      src_ptr = ref_frame +
           (x0 < 0 ? 0 : (x0 >= frame_width2 ? frame_width2 - 1: x0));
      dst_ptr = src_ptr - pre_buf->stride;
      for (i = 0; i < extend_top; i++) {
        for (j = 0; j < x1- x0; j++) {
          if (*(src_ptr+j) != *(dst_ptr + j))
            test = 0;
        }
        vpx_memcpy(dst_ptr, src_ptr, x1 - x0);
        dst_ptr -= pre_buf->stride;
      }
    }


    // extend bottom boarder if necessary
    if (y1 >= frame_height2) {
      if (frame_width !=352) {
        test = 0;
      }
      num |= 1 << 2;
      int j;
      uint8_t extend_bottom = y1 - frame_height2;
      src_ptr = ref_frame +  pre_buf->stride * (frame_height2 - 1)
                   +(x0 < 0 ? 0 : (x0 >= frame_width2 ? frame_width2 - 1: x0));
      dst_ptr = src_ptr + pre_buf->stride;
      for (i = 0; i < extend_bottom; i++) {
        for (j = 0; j < x1- x0; j++) {
          if (*(src_ptr+j) != *(dst_ptr + j))
            test = 0;
        }
        vpx_memcpy(dst_ptr, src_ptr, x1 - x0);
        dst_ptr += pre_buf->stride;
      }
    }

    // extend left boarder if necessary
    if (x0 <= 0) {
      num |= 1 << 3;
      uint8_t extend_left = -x0;
      dst_ptr = ref_frame + pre_buf->stride * y0 + x0;
      src_ptr = ref_frame + pre_buf->stride * y0;
      for (i = 0; i < y1 - y0; i++) {
        vpx_memset(dst_ptr, *src_ptr, extend_left);
        src_ptr += pre_buf->stride;
        dst_ptr += pre_buf->stride;
      }
    }

    // extend right boarder if necessary
    if (x1 >= frame_width2) {
      num |= 1 << 4;
      uint8_t extend_right = x1 - frame_width2 +1;
      src_ptr = ref_frame + pre_buf->stride * (y0 < 0 ? y0 : y0-1)
                             + frame_width2 - 1;
      dst_ptr = src_ptr;

      for (i = 0; i <= y1 - y0; i++) {
        vpx_memset(dst_ptr, *src_ptr, extend_right);
        src_ptr += pre_buf->stride;
        dst_ptr += pre_buf->stride;
      }
    }
    inter_predictor(pre, pre_buf->stride, dst, dst_buf->stride,
                     &scaled_mv, scale,
                     4 << pred_w, 4 << pred_h, ref,
                     &xd->subpix, xs, ys);
  }
}

// TODO(jkoleszar): In principle, pred_w, pred_h are unnecessary, as we could
// calculate the subsampled BLOCK_SIZE, but that type isn't defined for
// sizes smaller than 16x16 yet.
typedef void (*foreach_predicted_block_visitor)(int plane, int block,
                                                BLOCK_SIZE bsize,
                                                int pred_w, int pred_h,
                                                void *arg);
static INLINE void foreach_predicted_block_in_plane(
    const MACROBLOCKD* const xd, BLOCK_SIZE bsize, int plane,
    foreach_predicted_block_visitor visit, void *arg) {
  const int bwl = b_width_log2(bsize) - xd->plane[plane].subsampling_x;
  const int bhl = b_height_log2(bsize) - xd->plane[plane].subsampling_y;

  if (xd->mi_8x8[0]->mbmi.sb_type < BLOCK_8X8) {
    int i = 0, x, y;
    assert(bsize == BLOCK_8X8);
    for (y = 0; y < 1 << bhl; ++y)
      for (x = 0; x < 1 << bwl; ++x)
        visit(plane, i++, bsize, 0, 0, arg);
  } else {
    visit(plane, 0, bsize, bwl, bhl, arg);
  }
}

static void build_inter_predictors_for_planes(MACROBLOCKD *xd, BLOCK_SIZE bsize,
                                              int mi_row, int mi_col,
                                              int plane_from, int plane_to) {
  int plane;
  for (plane = plane_from; plane <= plane_to; ++plane) {
    struct build_inter_predictors_args args = {
      xd, mi_col * MI_SIZE, mi_row * MI_SIZE,
    };
    foreach_predicted_block_in_plane(xd, bsize, plane, build_inter_predictors,
                                     &args);
  }
}

void vp9_build_inter_predictors_sby(MACROBLOCKD *xd, int mi_row, int mi_col,
                                    BLOCK_SIZE bsize) {
  build_inter_predictors_for_planes(xd, bsize, mi_row, mi_col, 0, 0);
}
void vp9_build_inter_predictors_sbuv(MACROBLOCKD *xd, int mi_row, int mi_col,
                                     BLOCK_SIZE bsize) {
  build_inter_predictors_for_planes(xd, bsize, mi_row, mi_col, 1,
                                    MAX_MB_PLANE - 1);
}
void vp9_build_inter_predictors_sb(MACROBLOCKD *xd, int mi_row, int mi_col,
                                   BLOCK_SIZE bsize) {
  build_inter_predictors_for_planes(xd, bsize, mi_row, mi_col, 0,
                                    MAX_MB_PLANE - 1);
}

// TODO(dkovalev: find better place for this function)
void vp9_setup_scale_factors(VP9_COMMON *cm, int i) {
  const int ref = cm->active_ref_idx[i];
  struct scale_factors *const sf = &cm->active_ref_scale[i];
  struct scale_factors_common *const sfc = &cm->active_ref_scale_comm[i];
  if (ref >= NUM_YV12_BUFFERS) {
    vp9_zero(*sf);
    vp9_zero(*sfc);
  } else {
    YV12_BUFFER_CONFIG *const fb = &cm->yv12_fb[ref];
    vp9_setup_scale_factors_for_frame(sf, sfc,
                                      fb->y_crop_width, fb->y_crop_height,
                                      cm->width, cm->height);

    if (vp9_is_scaled(sfc))
      vp9_extend_frame_borders(fb, cm->subsampling_x, cm->subsampling_y);
  }
}

