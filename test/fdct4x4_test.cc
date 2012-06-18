/*
*  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
*
*  Use of this source code is governed by a BSD-style license
*  that can be found in the LICENSE file in the root of the source
*  tree. An additional intellectual property rights grant can be found
*  in the file PATENTS.  All contributing project authors may
*  be found in the AUTHORS file in the root of the source tree.
*/


extern "C" {
#include "vpx_rtcd.h"
}
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "third_party/googletest/src/include/gtest/gtest.h"
#include "vpx/vpx_integer.h"
#include "vpx_mem/vpx_mem.h"
#include "vp8/encoder/onyx_int.h"


namespace {
  const int cospi8sqrt2minus1 = 20091;
  const int sinpi8sqrt2 = 35468;

  void reference_idct4x4(const short *input, short *output) {
    const short *ip = input;
    short *op = output;
    int temp1, temp2;
    int i;

    for (i = 0; i < 4; ++i) {
      const int a1 = ip[0] + ip[8];
      const int b1 = ip[0] - ip[8];
      temp1 = (ip[4] * sinpi8sqrt2) >> 16;
      temp2 = ip[12] + ((ip[12] * cospi8sqrt2minus1) >> 16);
      const int c1 = temp1 - temp2;
      temp1 = ip[4] + ((ip[4] * cospi8sqrt2minus1) >> 16);
      temp2 = (ip[12] * sinpi8sqrt2) >> 16;
      const int d1 = temp1 + temp2;
      op[0] = a1 + d1;
      op[12] = a1 - d1;
      op[4] = b1 + c1;
      op[8] = b1 - c1;
      ++ip;
      ++op;
    }
    ip = output;
    op = output;
    for (i = 0; i < 4; ++i ) {
      const int a1 = ip[0] + ip[2];
      const int b1 = ip[0] - ip[2];
      temp1 = (ip[1] * sinpi8sqrt2) >> 16;
      temp2 = ip[3] + ((ip[3] * cospi8sqrt2minus1) >> 16);
      const int c1 = temp1 - temp2;
      temp1 = ip[1] + ((ip[1] * cospi8sqrt2minus1) >> 16);
      temp2 = (ip[3] * sinpi8sqrt2) >> 16;
      const int d1 = temp1 + temp2;
      op[0] = (a1 + d1 + 4) >> 3;
      op[3] = (a1 - d1 + 4) >> 3;
      op[1] = (b1 + c1 + 4) >> 3;
      op[2] = (b1 - c1 + 4) >> 3;
      ip += 4;
      op += 4;
    }
  }

  // This is a class that generate random numbers for test input.
  class ACMRandom {
  public:
    explicit ACMRandom(int seed) { Reset(seed); }

    void Reset(int seed) { srand(seed); }

    uint8_t Rand8(void) { return (rand() >> 8) & 0xff; }

    int PseudoUniform(int range) { return (rand() >> 8) % range; }

    int operator()(int n) { return PseudoUniform(n); }

    static int DeterministicSeed(void) { return 0xbaba; }
  };


  TEST(Vp8FdctTest, SignBiasCheck) {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    short test_input_block[16];
    short test_output_block[16];
    const int pitch = 8;
    int count_sign_block[16][2];
    int i, j;
    const int count_test_block = 1000000;

    memset(count_sign_block, 0, sizeof(count_sign_block));

    for(i = 0; i < count_test_block; ++i) {
      // Initialize a test block with input range [-255, 255].
      for(j = 0; j < 16; ++j)
        test_input_block[j] = rnd.Rand8() - rnd.Rand8();

      vp8_short_fdct4x4_c(test_input_block, test_output_block, pitch);

      for(j = 0; j < 16; ++j) {
        if (test_output_block[j] < 0)
          ++count_sign_block[j][0];
        else if (test_output_block[j] > 0)
          ++count_sign_block[j][1];
      }
    }

    bool bias_acceptable=true;
    for(j = 0; j < 16; ++j)
      bias_acceptable = bias_acceptable &&
        (abs(count_sign_block[j][0] - count_sign_block[j][1]) < 10000);

    EXPECT_EQ(true, bias_acceptable)
      << "Error: 4x4 FDCT has a sign bias > 1% for input range [-255, 255]";

    memset(count_sign_block, 0, sizeof(count_sign_block));

    for(i = 0; i < count_test_block; ++i) {
      // Initialize a test block with input range [-15, 15].
      for(j = 0; j < 16; ++j)
        test_input_block[j] = (rnd.Rand8() >> 4) - (rnd.Rand8() >> 4);

      vp8_short_fdct4x4_c(test_input_block, test_output_block, pitch);

      for(j = 0; j < 16; ++j) {
        if (test_output_block[j] < 0)
          ++ count_sign_block[j][0];
        else if (test_output_block[j] > 0)
          ++count_sign_block[j][1];
      }
    }

    bias_acceptable = true;

    for(j = 0; j < 16; ++j)
      bias_acceptable = bias_acceptable &&
        (abs(count_sign_block[j][0] - count_sign_block[j][1]) < 100000);

    EXPECT_EQ(true, bias_acceptable)
      << "Error: 4x4 FDCT has a sign bias > 10% for input range [-15, 15]";
  };

  TEST(Vp8FdctTest, RoundTripErrorCheck) {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    int max_error=0;
    double total_error=0;

    int i;
    const int count_test_block = 1000000;
    for(i = 0; i < count_test_block; ++i) {
      short test_input_block[16];
      short test_temp_block[16];
      short test_output_block[16];
      int j;

      // Initialize a test block with input range [-255, 255].
      for(j = 0; j < 16; ++j)
        test_input_block[j] = rnd.Rand8() - rnd.Rand8();

      const int pitch = 8;
      vp8_short_fdct4x4_c(test_input_block, test_temp_block, pitch);
      reference_idct4x4(test_temp_block, test_output_block);

      for(j = 0; j < 16; ++j) {
        const int diff = test_input_block[j] - test_output_block[j];
        const int error = diff*diff;
        if (max_error < error)
          max_error = error;
        total_error += error;
      }
    }

    EXPECT_GE(1, max_error )
      << "Error: FDCT/IDCT has an individual roundtrip error > 1";

    EXPECT_GE(count_test_block, total_error)
      << "Error: FDCT/IDCT has average roundtrip error > 1 per block";
  };

}  // namespace