/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


extern "C" {
//#include "vpx_config.h"
//#include "vpx_rtcd.h"
#include "vp8/common/blockd.h"
#include "vp8/encoder/block.h"
#include "vpx_mem/vpx_mem.h"

extern void vp8_subtract_b_c(BLOCK *be, BLOCKD *bd, int pitch);
extern void vp8_subtract_b_mmx(BLOCK *be, BLOCKD *bd, int pitch);
extern void vp8_subtract_b_sse2(BLOCK *be, BLOCKD *bd, int pitch);
}
#include "third_party/googletest/src/include/gtest/gtest.h"


typedef void (*subtract_b_fn_t)(BLOCK *be, BLOCKD *bd, int pitch);

namespace {

class SubtractBlockTest : public ::testing::TestWithParam<subtract_b_fn_t> {};

TEST_P(SubtractBlockTest, SimpleSubtract) {
  BLOCK be;
  BLOCKD bd;
  int pitch = 4;

  //Allocate... align to 16 for mmx/sse tests
  unsigned char * source = reinterpret_cast<unsigned char*>(vpx_memalign(16, 4*4*sizeof(unsigned char)));
  be.src_diff = reinterpret_cast<short*>(vpx_memalign(16, 4*4*sizeof(short)));
  bd.predictor = reinterpret_cast<unsigned char*>(vpx_memalign(16, 4*4*sizeof(unsigned char)));

  //start at block0
  be.src = 0;
  be.base_src = &source;

  //set difference
  short * src_diff = be.src_diff;
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
	  src_diff[c] = 0xa5a5;
    }
    src_diff += pitch;
  }

  //set destination
  unsigned char * base_src = *be.base_src;
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      base_src[c] = 0x1;
    }
    base_src += pitch;
  }

  //set predictor
  unsigned char * predictor = bd.predictor;
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      predictor[c] = 255;
    }
    predictor += pitch;
  }

  GetParam()(&be, &bd, pitch);

  src_diff = be.src_diff;
  for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) {
          EXPECT_EQ(-254, src_diff[c]) << "r = " << r << ", c = " << c;
      }
      src_diff += pitch;
  }

  // Free allocated memory
  vpx_free(be.src_diff);
  vpx_free(source);
  vpx_free(bd.predictor);

}

INSTANTIATE_TEST_CASE_P(C, SubtractBlockTest,
                        ::testing::Values(vp8_subtract_b_c));

#if HAVE_MMX
INSTANTIATE_TEST_CASE_P(MMX, SubtractBlockTest,
                        ::testing::Values(vp8_subtract_b_mmx));
#endif

#if HAVE_SSE2
INSTANTIATE_TEST_CASE_P(SSE2, SubtractBlockTest,
                        ::testing::Values(vp8_subtract_b_sse2));
#endif

} // namespace
