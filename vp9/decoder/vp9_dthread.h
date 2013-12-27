/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VP9_DECODER_VP9_DTHREAD_H_
#define VP9_DECODER_VP9_DTHREAD_H_

#include "vp9/common/vp9_loopfilter.h"
#include "vp9/decoder/vp9_reader.h"

struct VP9Decompressor;
struct VP9Common;
struct macroblockd;
struct VP9LfSyncData;

typedef struct TileWorkerData {
  struct VP9Common *cm;
  vp9_reader bit_reader;
  DECLARE_ALIGNED(16, struct macroblockd, xd);
  DECLARE_ALIGNED(16, int16_t, dqcoeff[MAX_MB_PLANE][64 * 64]);

  // Row-based parallel loopfilter data
  LFWorkerData lfdata;
} TileWorkerData;

// Allocate memory for loopfilter row synchronization.
void vp9_loop_filter_alloc(struct VP9Common *cm, struct VP9LfSyncData *lf_sync,
                           int rows, int width);

// Deallocate loopfilter synchronization related mutex and data.
void vp9_loop_filter_dealloc(struct VP9LfSyncData *lf_sync, int rows);

// Row-based multi-threaded loopfilter hook
int vp9_loop_filter_row_worker(void *arg1, void *arg2);

// VP9 decoder: Implement multi-threaded loopfilter that uses the tile
// threads.
void vp9_loop_filter_frame_mt(struct VP9Decompressor *pbi,
                              struct VP9Common *cm,
                              struct macroblockd *xd,
                              int frame_filter_level,
                              int y_only, int partial);

#endif  // VP9_DECODER_VP9_DTHREAD_H_
