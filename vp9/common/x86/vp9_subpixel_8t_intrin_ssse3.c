/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// Due to a header conflict between math.h and intrinsics includes with ceil()
// in certain configurations under vs9 this include needs to precede
// tmmintrin.h.
#include "./vp9_rtcd.h"

#include <tmmintrin.h>

#include "vp9/common/x86/convolve.h"
#include "vpx_ports/mem.h"
#include "vpx_ports/emmintrin_compat.h"

// filters only for the 4_h8 convolution
DECLARE_ALIGNED(16, static const uint8_t, filt1_4_h8[16]) = {
  0, 1, 1, 2, 2, 3, 3, 4, 2, 3, 3, 4, 4, 5, 5, 6
};

DECLARE_ALIGNED(16, static const uint8_t, filt2_4_h8[16]) = {
  4, 5, 5, 6, 6, 7, 7, 8, 6, 7, 7, 8, 8, 9, 9, 10
};

// filters for 8_h8 and 16_h8
DECLARE_ALIGNED(16, static const uint8_t, filt1_global[16]) = {
  0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8
};

DECLARE_ALIGNED(16, static const uint8_t, filt2_global[16]) = {
  2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10
};

DECLARE_ALIGNED(16, static const uint8_t, filt3_global[16]) = {
  4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12
};

DECLARE_ALIGNED(16, static const uint8_t, filt4_global[16]) = {
  6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14
};

// These are reused by the avx2 intrinsics.
filter8_1dfunction vp9_filter_block1d8_v8_intrin_ssse3;
filter8_1dfunction vp9_filter_block1d8_h8_intrin_ssse3;
filter8_1dfunction vp9_filter_block1d4_h8_intrin_ssse3;

void vp9_filter_block1d4_h8_intrin_ssse3(const uint8_t *src_ptr,
                                         ptrdiff_t src_pixels_per_line,
                                         uint8_t *output_ptr,
                                         ptrdiff_t output_pitch,
                                         uint32_t output_height,
                                         const int16_t *filter) {
  __m128i firstFilters, secondFilters, shuffle1, shuffle2;
  __m128i srcRegFilt1, srcRegFilt2, srcRegFilt3, srcRegFilt4;
  __m128i addFilterReg64, filtersReg, srcReg, minReg;
  unsigned int i;

  // create a register with 0,64,0,64,0,64,0,64,0,64,0,64,0,64,0,64
  addFilterReg64 =_mm_set1_epi32((int)0x0400040u);
  filtersReg = _mm_loadu_si128((const __m128i *)filter);
  // converting the 16 bit (short) to  8 bit (byte) and have the same data
  // in both lanes of 128 bit register.
  filtersReg =_mm_packs_epi16(filtersReg, filtersReg);

  // duplicate only the first 16 bits in the filter into the first lane
  firstFilters = _mm_shufflelo_epi16(filtersReg, 0);
  // duplicate only the third 16 bit in the filter into the first lane
  secondFilters = _mm_shufflelo_epi16(filtersReg, 0xAAu);
  // duplicate only the seconds 16 bits in the filter into the second lane
  // firstFilters: k0 k1 k0 k1 k0 k1 k0 k1 k2 k3 k2 k3 k2 k3 k2 k3
  firstFilters = _mm_shufflehi_epi16(firstFilters, 0x55u);
  // duplicate only the forth 16 bits in the filter into the second lane
  // secondFilters: k4 k5 k4 k5 k4 k5 k4 k5 k6 k7 k6 k7 k6 k7 k6 k7
  secondFilters = _mm_shufflehi_epi16(secondFilters, 0xFFu);

  // loading the local filters
  shuffle1 =_mm_load_si128((__m128i const *)filt1_4_h8);
  shuffle2 = _mm_load_si128((__m128i const *)filt2_4_h8);

  for (i = 0; i < output_height; i++) {
    srcReg = _mm_loadu_si128((const __m128i *)(src_ptr - 3));

    // filter the source buffer
    srcRegFilt1= _mm_shuffle_epi8(srcReg, shuffle1);
    srcRegFilt2= _mm_shuffle_epi8(srcReg, shuffle2);

    // multiply 2 adjacent elements with the filter and add the result
    srcRegFilt1 = _mm_maddubs_epi16(srcRegFilt1, firstFilters);
    srcRegFilt2 = _mm_maddubs_epi16(srcRegFilt2, secondFilters);

    // extract the higher half of the lane
    srcRegFilt3 =  _mm_srli_si128(srcRegFilt1, 8);
    srcRegFilt4 =  _mm_srli_si128(srcRegFilt2, 8);

    minReg = _mm_min_epi16(srcRegFilt3, srcRegFilt2);

    // add and saturate all the results together
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, srcRegFilt4);
    srcRegFilt3 = _mm_max_epi16(srcRegFilt3, srcRegFilt2);
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, minReg);
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, srcRegFilt3);
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, addFilterReg64);

    // shift by 7 bit each 16 bits
    srcRegFilt1 = _mm_srai_epi16(srcRegFilt1, 7);

    // shrink to 8 bit each 16 bits
    srcRegFilt1 = _mm_packus_epi16(srcRegFilt1, srcRegFilt1);
    src_ptr+=src_pixels_per_line;

    // save only 4 bytes
    *((int*)&output_ptr[0])= _mm_cvtsi128_si32(srcRegFilt1);

    output_ptr+=output_pitch;
  }
}

void vp9_filter_block1d8_h8_intrin_ssse3(const uint8_t *src_ptr,
                                         ptrdiff_t src_pixels_per_line,
                                         uint8_t *output_ptr,
                                         ptrdiff_t output_pitch,
                                         uint32_t output_height,
                                         const int16_t *filter) {
  __m128i firstFilters, secondFilters, thirdFilters, forthFilters, srcReg;
  __m128i filt1Reg, filt2Reg, filt3Reg, filt4Reg;
  __m128i srcRegFilt1, srcRegFilt2, srcRegFilt3, srcRegFilt4;
  __m128i addFilterReg64, filtersReg, minReg;
  unsigned int i;

  // create a register with 0,64,0,64,0,64,0,64,0,64,0,64,0,64,0,64
  addFilterReg64 = _mm_set1_epi32((int)0x0400040u);
  filtersReg = _mm_loadu_si128((const __m128i *)filter);
  // converting the 16 bit (short) to  8 bit (byte) and have the same data
  // in both lanes of 128 bit register.
  filtersReg =_mm_packs_epi16(filtersReg, filtersReg);

  // duplicate only the first 16 bits (first and second byte)
  // across 128 bit register
  firstFilters = _mm_shuffle_epi8(filtersReg, _mm_set1_epi16(0x100u));
  // duplicate only the second 16 bits (third and forth byte)
  // across 128 bit register
  secondFilters = _mm_shuffle_epi8(filtersReg, _mm_set1_epi16(0x302u));
  // duplicate only the third 16 bits (fifth and sixth byte)
  // across 128 bit register
  thirdFilters = _mm_shuffle_epi8(filtersReg, _mm_set1_epi16(0x504u));
  // duplicate only the forth 16 bits (seventh and eighth byte)
  // across 128 bit register
  forthFilters = _mm_shuffle_epi8(filtersReg, _mm_set1_epi16(0x706u));

  filt1Reg = _mm_load_si128((__m128i const *)filt1_global);
  filt2Reg = _mm_load_si128((__m128i const *)filt2_global);
  filt3Reg = _mm_load_si128((__m128i const *)filt3_global);
  filt4Reg = _mm_load_si128((__m128i const *)filt4_global);

  for (i = 0; i < output_height; i++) {
    srcReg = _mm_loadu_si128((const __m128i *)(src_ptr - 3));

    // filter the source buffer
    srcRegFilt1= _mm_shuffle_epi8(srcReg, filt1Reg);
    srcRegFilt2= _mm_shuffle_epi8(srcReg, filt2Reg);

    // multiply 2 adjacent elements with the filter and add the result
    srcRegFilt1 = _mm_maddubs_epi16(srcRegFilt1, firstFilters);
    srcRegFilt2 = _mm_maddubs_epi16(srcRegFilt2, secondFilters);

    // filter the source buffer
    srcRegFilt3= _mm_shuffle_epi8(srcReg, filt3Reg);
    srcRegFilt4= _mm_shuffle_epi8(srcReg, filt4Reg);

    // multiply 2 adjacent elements with the filter and add the result
    srcRegFilt3 = _mm_maddubs_epi16(srcRegFilt3, thirdFilters);
    srcRegFilt4 = _mm_maddubs_epi16(srcRegFilt4, forthFilters);

    // add and saturate all the results together
    minReg = _mm_min_epi16(srcRegFilt2, srcRegFilt3);
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, srcRegFilt4);

    srcRegFilt2= _mm_max_epi16(srcRegFilt2, srcRegFilt3);
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, minReg);
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, srcRegFilt2);
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, addFilterReg64);

    // shift by 7 bit each 16 bits
    srcRegFilt1 = _mm_srai_epi16(srcRegFilt1, 7);

    // shrink to 8 bit each 16 bits
    srcRegFilt1 = _mm_packus_epi16(srcRegFilt1, srcRegFilt1);

    src_ptr+=src_pixels_per_line;

    // save only 8 bytes
    _mm_storel_epi64((__m128i*)&output_ptr[0], srcRegFilt1);

    output_ptr+=output_pitch;
  }
}

static void vp9_filter_block1d16_h8_intrin_ssse3(const uint8_t *src_ptr,
                                                 ptrdiff_t src_pixels_per_line,
                                                 uint8_t *output_ptr,
                                                 ptrdiff_t output_pitch,
                                                 uint32_t output_height,
                                                 const int16_t *filter) {
  __m128i addFilterReg64, filtersReg, srcReg1, srcReg2;
  __m128i filt1Reg, filt2Reg, filt3Reg, filt4Reg;
  __m128i firstFilters, secondFilters, thirdFilters, forthFilters;
  __m128i srcRegFilt1_1, srcRegFilt2_1, srcRegFilt2, srcRegFilt3;
  unsigned int i;

  // create a register with 0,64,0,64,0,64,0,64,0,64,0,64,0,64,0,64
  addFilterReg64 = _mm_set1_epi32((int)0x0400040u);
  filtersReg = _mm_loadu_si128((const __m128i *)filter);
  // converting the 16 bit (short) to  8 bit (byte) and have the same data
  // in both lanes of 128 bit register.
  filtersReg =_mm_packs_epi16(filtersReg, filtersReg);

  // duplicate only the first 16 bits (first and second byte)
  // across 128 bit register
  firstFilters = _mm_shuffle_epi8(filtersReg, _mm_set1_epi16(0x100u));
  // duplicate only the second 16 bits (third and forth byte)
  // across 128 bit register
  secondFilters = _mm_shuffle_epi8(filtersReg, _mm_set1_epi16(0x302u));
  // duplicate only the third 16 bits (fifth and sixth byte)
  // across 128 bit register
  thirdFilters = _mm_shuffle_epi8(filtersReg, _mm_set1_epi16(0x504u));
  // duplicate only the forth 16 bits (seventh and eighth byte)
  // across 128 bit register
  forthFilters = _mm_shuffle_epi8(filtersReg, _mm_set1_epi16(0x706u));

  filt1Reg = _mm_load_si128((__m128i const *)filt1_global);
  filt2Reg = _mm_load_si128((__m128i const *)filt2_global);
  filt3Reg = _mm_load_si128((__m128i const *)filt3_global);
  filt4Reg = _mm_load_si128((__m128i const *)filt4_global);

  for (i = 0; i < output_height; i++) {
    srcReg1 = _mm_loadu_si128((const __m128i *)(src_ptr - 3));

    // filter the source buffer
    srcRegFilt1_1= _mm_shuffle_epi8(srcReg1, filt1Reg);
    srcRegFilt2= _mm_shuffle_epi8(srcReg1, filt4Reg);

    // multiply 2 adjacent elements with the filter and add the result
    srcRegFilt1_1 = _mm_maddubs_epi16(srcRegFilt1_1, firstFilters);
    srcRegFilt2 = _mm_maddubs_epi16(srcRegFilt2, forthFilters);

    // add and saturate the results together
    srcRegFilt1_1 = _mm_adds_epi16(srcRegFilt1_1, srcRegFilt2);

    // filter the source buffer
    srcRegFilt3= _mm_shuffle_epi8(srcReg1, filt2Reg);
    srcRegFilt2= _mm_shuffle_epi8(srcReg1, filt3Reg);

    // multiply 2 adjacent elements with the filter and add the result
    srcRegFilt3 = _mm_maddubs_epi16(srcRegFilt3, secondFilters);
    srcRegFilt2 = _mm_maddubs_epi16(srcRegFilt2, thirdFilters);

    // add and saturate the results together
    srcRegFilt1_1 = _mm_adds_epi16(srcRegFilt1_1,
                                   _mm_min_epi16(srcRegFilt3, srcRegFilt2));

    // reading the next 16 bytes.
    // (part of it was being read by earlier read)
    srcReg2 = _mm_loadu_si128((const __m128i *)(src_ptr + 5));

    // add and saturate the results together
    srcRegFilt1_1 = _mm_adds_epi16(srcRegFilt1_1,
                                   _mm_max_epi16(srcRegFilt3, srcRegFilt2));

    // filter the source buffer
    srcRegFilt2_1= _mm_shuffle_epi8(srcReg2, filt1Reg);
    srcRegFilt2= _mm_shuffle_epi8(srcReg2, filt4Reg);

    // multiply 2 adjacent elements with the filter and add the result
    srcRegFilt2_1 = _mm_maddubs_epi16(srcRegFilt2_1, firstFilters);
    srcRegFilt2 = _mm_maddubs_epi16(srcRegFilt2, forthFilters);

    // add and saturate the results together
    srcRegFilt2_1 = _mm_adds_epi16(srcRegFilt2_1, srcRegFilt2);

    // filter the source buffer
    srcRegFilt3= _mm_shuffle_epi8(srcReg2, filt2Reg);
    srcRegFilt2= _mm_shuffle_epi8(srcReg2, filt3Reg);

    // multiply 2 adjacent elements with the filter and add the result
    srcRegFilt3 = _mm_maddubs_epi16(srcRegFilt3, secondFilters);
    srcRegFilt2 = _mm_maddubs_epi16(srcRegFilt2, thirdFilters);

    // add and saturate the results together
    srcRegFilt2_1 = _mm_adds_epi16(srcRegFilt2_1,
    _mm_min_epi16(srcRegFilt3, srcRegFilt2));
    srcRegFilt2_1 = _mm_adds_epi16(srcRegFilt2_1,
    _mm_max_epi16(srcRegFilt3, srcRegFilt2));

    srcRegFilt1_1 = _mm_adds_epi16(srcRegFilt1_1, addFilterReg64);
    srcRegFilt2_1 = _mm_adds_epi16(srcRegFilt2_1, addFilterReg64);

    // shift by 7 bit each 16 bit
    srcRegFilt1_1 = _mm_srai_epi16(srcRegFilt1_1, 7);
    srcRegFilt2_1 = _mm_srai_epi16(srcRegFilt2_1, 7);

    // shrink to 8 bit each 16 bits, the first lane contain the first
    // convolve result and the second lane contain the second convolve
    // result
    srcRegFilt1_1 = _mm_packus_epi16(srcRegFilt1_1, srcRegFilt2_1);

    src_ptr+=src_pixels_per_line;

    // save 16 bytes
    _mm_store_si128((__m128i*)output_ptr, srcRegFilt1_1);

    output_ptr+=output_pitch;
  }
}

void vp9_filter_block1d8_v8_intrin_ssse3(const uint8_t *src_ptr,
                                         ptrdiff_t src_pitch,
                                         uint8_t *output_ptr,
                                         ptrdiff_t out_pitch,
                                         uint32_t output_height,
                                         const int16_t *filter) {
  __m128i addFilterReg64, filtersReg, minReg;
  __m128i firstFilters, secondFilters, thirdFilters, forthFilters;
  __m128i srcRegFilt1, srcRegFilt2, srcRegFilt3, srcRegFilt5;
  __m128i srcReg1, srcReg2, srcReg3, srcReg4, srcReg5, srcReg6, srcReg7;
  __m128i srcReg8;
  unsigned int i;

  // create a register with 0,64,0,64,0,64,0,64,0,64,0,64,0,64,0,64
  addFilterReg64 = _mm_set1_epi32((int)0x0400040u);
  filtersReg = _mm_loadu_si128((const __m128i *)filter);
  // converting the 16 bit (short) to  8 bit (byte) and have the same data
  // in both lanes of 128 bit register.
  filtersReg =_mm_packs_epi16(filtersReg, filtersReg);

  // duplicate only the first 16 bits in the filter
  firstFilters = _mm_shuffle_epi8(filtersReg, _mm_set1_epi16(0x100u));
  // duplicate only the second 16 bits in the filter
  secondFilters = _mm_shuffle_epi8(filtersReg, _mm_set1_epi16(0x302u));
  // duplicate only the third 16 bits in the filter
  thirdFilters = _mm_shuffle_epi8(filtersReg, _mm_set1_epi16(0x504u));
  // duplicate only the forth 16 bits in the filter
  forthFilters = _mm_shuffle_epi8(filtersReg, _mm_set1_epi16(0x706u));

  // load the first 7 rows of 8 bytes
  srcReg1 = _mm_loadl_epi64((const __m128i *)src_ptr);
  srcReg2 = _mm_loadl_epi64((const __m128i *)(src_ptr + src_pitch));
  srcReg3 = _mm_loadl_epi64((const __m128i *)(src_ptr + src_pitch * 2));
  srcReg4 = _mm_loadl_epi64((const __m128i *)(src_ptr + src_pitch * 3));
  srcReg5 = _mm_loadl_epi64((const __m128i *)(src_ptr + src_pitch * 4));
  srcReg6 = _mm_loadl_epi64((const __m128i *)(src_ptr + src_pitch * 5));
  srcReg7 = _mm_loadl_epi64((const __m128i *)(src_ptr + src_pitch * 6));

  for (i = 0; i < output_height; i++) {
    // load the last 8 bytes
    srcReg8 = _mm_loadl_epi64((const __m128i *)(src_ptr + src_pitch * 7));

    // merge the result together
    srcRegFilt1 = _mm_unpacklo_epi8(srcReg1, srcReg2);
    srcRegFilt3 = _mm_unpacklo_epi8(srcReg3, srcReg4);

    // merge the result together
    srcRegFilt2 = _mm_unpacklo_epi8(srcReg5, srcReg6);
    srcRegFilt5 = _mm_unpacklo_epi8(srcReg7, srcReg8);

    // multiply 2 adjacent elements with the filter and add the result
    srcRegFilt1 = _mm_maddubs_epi16(srcRegFilt1, firstFilters);
    srcRegFilt3 = _mm_maddubs_epi16(srcRegFilt3, secondFilters);
    srcRegFilt2 = _mm_maddubs_epi16(srcRegFilt2, thirdFilters);
    srcRegFilt5 = _mm_maddubs_epi16(srcRegFilt5, forthFilters);

    // add and saturate the results together
    minReg = _mm_min_epi16(srcRegFilt2, srcRegFilt3);
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, srcRegFilt5);
    srcRegFilt2 = _mm_max_epi16(srcRegFilt2, srcRegFilt3);
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, minReg);
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, srcRegFilt2);
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, addFilterReg64);

    // shift by 7 bit each 16 bit
    srcRegFilt1 = _mm_srai_epi16(srcRegFilt1, 7);

    // shrink to 8 bit each 16 bits
    srcRegFilt1 = _mm_packus_epi16(srcRegFilt1, srcRegFilt1);

    src_ptr+=src_pitch;

    // shift down a row
    srcReg1 = srcReg2;
    srcReg2 = srcReg3;
    srcReg3 = srcReg4;
    srcReg4 = srcReg5;
    srcReg5 = srcReg6;
    srcReg6 = srcReg7;
    srcReg7 = srcReg8;

    // save only 8 bytes convolve result
    _mm_storel_epi64((__m128i*)&output_ptr[0], srcRegFilt1);

    output_ptr+=out_pitch;
  }
}

static void vp9_filter_block1d16_v8_intrin_ssse3(const uint8_t *src_ptr,
                                                 ptrdiff_t src_pitch,
                                                 uint8_t *output_ptr,
                                                 ptrdiff_t out_pitch,
                                                 uint32_t output_height,
                                                 const int16_t *filter) {
  __m128i addFilterReg64, filtersReg, srcRegFilt1, srcRegFilt3;
  __m128i firstFilters, secondFilters, thirdFilters, forthFilters;
  __m128i srcRegFilt5, srcRegFilt6, srcRegFilt7, srcRegFilt8;
  __m128i srcReg1, srcReg2, srcReg3, srcReg4, srcReg5, srcReg6, srcReg7;
  __m128i srcReg8;
  unsigned int i;

  // create a register with 0,64,0,64,0,64,0,64,0,64,0,64,0,64,0,64
  addFilterReg64 = _mm_set1_epi32((int)0x0400040u);
  filtersReg = _mm_loadu_si128((const __m128i *)filter);
  // converting the 16 bit (short) to  8 bit (byte) and have the same data
  // in both lanes of 128 bit register.
  filtersReg =_mm_packs_epi16(filtersReg, filtersReg);

  // duplicate only the first 16 bits in the filter
  firstFilters = _mm_shuffle_epi8(filtersReg, _mm_set1_epi16(0x100u));
  // duplicate only the second 16 bits in the filter
  secondFilters = _mm_shuffle_epi8(filtersReg, _mm_set1_epi16(0x302u));
  // duplicate only the third 16 bits in the filter
  thirdFilters = _mm_shuffle_epi8(filtersReg, _mm_set1_epi16(0x504u));
  // duplicate only the forth 16 bits in the filter
  forthFilters = _mm_shuffle_epi8(filtersReg, _mm_set1_epi16(0x706u));

  // load the first 7 rows of 16 bytes
  srcReg1 = _mm_loadu_si128((const __m128i *)(src_ptr));
  srcReg2 = _mm_loadu_si128((const __m128i *)(src_ptr + src_pitch));
  srcReg3 = _mm_loadu_si128((const __m128i *)(src_ptr + src_pitch * 2));
  srcReg4 = _mm_loadu_si128((const __m128i *)(src_ptr + src_pitch * 3));
  srcReg5 = _mm_loadu_si128((const __m128i *)(src_ptr + src_pitch * 4));
  srcReg6 = _mm_loadu_si128((const __m128i *)(src_ptr + src_pitch * 5));
  srcReg7 = _mm_loadu_si128((const __m128i *)(src_ptr + src_pitch * 6));

  for (i = 0; i < output_height; i++) {
    // load the last 16 bytes
    srcReg8 = _mm_loadu_si128((const __m128i *)(src_ptr + src_pitch * 7));

    // merge the result together
    srcRegFilt5 = _mm_unpacklo_epi8(srcReg1, srcReg2);
    srcRegFilt6 = _mm_unpacklo_epi8(srcReg7, srcReg8);
    srcRegFilt1 = _mm_unpackhi_epi8(srcReg1, srcReg2);
    srcRegFilt3 = _mm_unpackhi_epi8(srcReg7, srcReg8);

    // multiply 2 adjacent elements with the filter and add the result
    srcRegFilt5 = _mm_maddubs_epi16(srcRegFilt5, firstFilters);
    srcRegFilt6 = _mm_maddubs_epi16(srcRegFilt6, forthFilters);
    srcRegFilt1 = _mm_maddubs_epi16(srcRegFilt1, firstFilters);
    srcRegFilt3 = _mm_maddubs_epi16(srcRegFilt3, forthFilters);

    // add and saturate the results together
    srcRegFilt5 = _mm_adds_epi16(srcRegFilt5, srcRegFilt6);
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, srcRegFilt3);

    // merge the result together
    srcRegFilt3 = _mm_unpacklo_epi8(srcReg3, srcReg4);
    srcRegFilt6 = _mm_unpackhi_epi8(srcReg3, srcReg4);

    // multiply 2 adjacent elements with the filter and add the result
    srcRegFilt3 = _mm_maddubs_epi16(srcRegFilt3, secondFilters);
    srcRegFilt6 = _mm_maddubs_epi16(srcRegFilt6, secondFilters);

    // merge the result together
    srcRegFilt7 = _mm_unpacklo_epi8(srcReg5, srcReg6);
    srcRegFilt8 = _mm_unpackhi_epi8(srcReg5, srcReg6);

    // multiply 2 adjacent elements with the filter and add the result
    srcRegFilt7 = _mm_maddubs_epi16(srcRegFilt7, thirdFilters);
    srcRegFilt8 = _mm_maddubs_epi16(srcRegFilt8, thirdFilters);

    // add and saturate the results together
    srcRegFilt5 = _mm_adds_epi16(srcRegFilt5,
                                 _mm_min_epi16(srcRegFilt3, srcRegFilt7));
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1,
                                 _mm_min_epi16(srcRegFilt6, srcRegFilt8));

    // add and saturate the results together
    srcRegFilt5 = _mm_adds_epi16(srcRegFilt5,
                                 _mm_max_epi16(srcRegFilt3, srcRegFilt7));
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1,
                                 _mm_max_epi16(srcRegFilt6, srcRegFilt8));
    srcRegFilt5 = _mm_adds_epi16(srcRegFilt5, addFilterReg64);
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, addFilterReg64);

    // shift by 7 bit each 16 bit
    srcRegFilt5 = _mm_srai_epi16(srcRegFilt5, 7);
    srcRegFilt1 = _mm_srai_epi16(srcRegFilt1, 7);

    // shrink to 8 bit each 16 bits, the first lane contain the first
    // convolve result and the second lane contain the second convolve
    // result
    srcRegFilt1 = _mm_packus_epi16(srcRegFilt5, srcRegFilt1);

    src_ptr+=src_pitch;

    // shift down a row
    srcReg1 = srcReg2;
    srcReg2 = srcReg3;
    srcReg3 = srcReg4;
    srcReg4 = srcReg5;
    srcReg5 = srcReg6;
    srcReg6 = srcReg7;
    srcReg7 = srcReg8;

    // save 16 bytes convolve result
    _mm_store_si128((__m128i*)output_ptr, srcRegFilt1);

    output_ptr+=out_pitch;
  }
}

#if ARCH_X86_64
filter8_1dfunction vp9_filter_block1d16_v8_intrin_ssse3;
filter8_1dfunction vp9_filter_block1d16_h8_intrin_ssse3;
filter8_1dfunction vp9_filter_block1d8_v8_intrin_ssse3;
filter8_1dfunction vp9_filter_block1d8_h8_intrin_ssse3;
filter8_1dfunction vp9_filter_block1d4_v8_ssse3;
filter8_1dfunction vp9_filter_block1d4_h8_intrin_ssse3;
#define vp9_filter_block1d16_v8_ssse3 vp9_filter_block1d16_v8_intrin_ssse3
#define vp9_filter_block1d16_h8_ssse3 vp9_filter_block1d16_h8_intrin_ssse3
#define vp9_filter_block1d8_v8_ssse3 vp9_filter_block1d8_v8_intrin_ssse3
#define vp9_filter_block1d8_h8_ssse3 vp9_filter_block1d8_h8_intrin_ssse3
#define vp9_filter_block1d4_h8_ssse3 vp9_filter_block1d4_h8_intrin_ssse3
#else  // ARCH_X86
filter8_1dfunction vp9_filter_block1d16_v8_ssse3;
filter8_1dfunction vp9_filter_block1d16_h8_ssse3;
filter8_1dfunction vp9_filter_block1d8_v8_ssse3;
filter8_1dfunction vp9_filter_block1d8_h8_ssse3;
filter8_1dfunction vp9_filter_block1d4_v8_ssse3;
filter8_1dfunction vp9_filter_block1d4_h8_ssse3;
#endif  // ARCH_X86_64
filter8_1dfunction vp9_filter_block1d16_v8_avg_ssse3;
filter8_1dfunction vp9_filter_block1d16_h8_avg_ssse3;
filter8_1dfunction vp9_filter_block1d8_v8_avg_ssse3;
filter8_1dfunction vp9_filter_block1d8_h8_avg_ssse3;
filter8_1dfunction vp9_filter_block1d4_v8_avg_ssse3;
filter8_1dfunction vp9_filter_block1d4_h8_avg_ssse3;

filter8_1dfunction vp9_filter_block1d16_v2_ssse3;
filter8_1dfunction vp9_filter_block1d16_h2_ssse3;
filter8_1dfunction vp9_filter_block1d8_v2_ssse3;
filter8_1dfunction vp9_filter_block1d8_h2_ssse3;
filter8_1dfunction vp9_filter_block1d4_v2_ssse3;
filter8_1dfunction vp9_filter_block1d4_h2_ssse3;
filter8_1dfunction vp9_filter_block1d16_v2_avg_ssse3;
filter8_1dfunction vp9_filter_block1d16_h2_avg_ssse3;
filter8_1dfunction vp9_filter_block1d8_v2_avg_ssse3;
filter8_1dfunction vp9_filter_block1d8_h2_avg_ssse3;
filter8_1dfunction vp9_filter_block1d4_v2_avg_ssse3;
filter8_1dfunction vp9_filter_block1d4_h2_avg_ssse3;

// void vp9_convolve8_horiz_ssse3(const uint8_t *src, ptrdiff_t src_stride,
//                                uint8_t *dst, ptrdiff_t dst_stride,
//                                const int16_t *filter_x, int x_step_q4,
//                                const int16_t *filter_y, int y_step_q4,
//                                int w, int h);
// void vp9_convolve8_vert_ssse3(const uint8_t *src, ptrdiff_t src_stride,
//                               uint8_t *dst, ptrdiff_t dst_stride,
//                               const int16_t *filter_x, int x_step_q4,
//                               const int16_t *filter_y, int y_step_q4,
//                               int w, int h);
// void vp9_convolve8_avg_horiz_ssse3(const uint8_t *src, ptrdiff_t src_stride,
//                                    uint8_t *dst, ptrdiff_t dst_stride,
//                                    const int16_t *filter_x, int x_step_q4,
//                                    const int16_t *filter_y, int y_step_q4,
//                                    int w, int h);
// void vp9_convolve8_avg_vert_ssse3(const uint8_t *src, ptrdiff_t src_stride,
//                                   uint8_t *dst, ptrdiff_t dst_stride,
//                                   const int16_t *filter_x, int x_step_q4,
//                                   const int16_t *filter_y, int y_step_q4,
//                                   int w, int h);
FUN_CONV_1D(horiz, x_step_q4, filter_x, h, src, , ssse3);
FUN_CONV_1D(vert, y_step_q4, filter_y, v, src - src_stride * 3, , ssse3);
FUN_CONV_1D(avg_horiz, x_step_q4, filter_x, h, src, avg_, ssse3);
FUN_CONV_1D(avg_vert, y_step_q4, filter_y, v, src - src_stride * 3, avg_,
            ssse3);

// WIP ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include "vp9/common/vp9_common.h"
#include "vp9/common/vp9_convolve.h"
#include "vp9/common/vp9_filter.h"

#define TRANSPOSE_8X8(in0, in1, in2, in3, in4, in5, in6, in7, \
                      out0, out1, out2, out3, out4, out5, out6, out7) \
  {                                                     \
    const __m128i tr0_0 = _mm_unpacklo_epi8(in0, in1); \
    const __m128i tr0_1 = _mm_unpacklo_epi8(in2, in3); \
    const __m128i tr0_2 = _mm_unpacklo_epi8(in4, in5); \
    const __m128i tr0_3 = _mm_unpacklo_epi8(in6, in7); \
                                                        \
    const __m128i tr1_0 = _mm_unpacklo_epi16(tr0_0, tr0_1); \
    const __m128i tr1_1 = _mm_unpackhi_epi16(tr0_0, tr0_1); \
    const __m128i tr1_2 = _mm_unpacklo_epi16(tr0_2, tr0_3); \
    const __m128i tr1_3 = _mm_unpackhi_epi16(tr0_2, tr0_3); \
                                                            \
    const __m128i tr2_0 = _mm_unpacklo_epi32(tr1_0, tr1_2); \
    const __m128i tr2_1 = _mm_unpackhi_epi32(tr1_0, tr1_2); \
    const __m128i tr2_2 = _mm_unpacklo_epi32(tr1_1, tr1_3); \
    const __m128i tr2_3 = _mm_unpackhi_epi32(tr1_1, tr1_3); \
                                                            \
    out0 = _mm_unpacklo_epi64(tr2_0, tr2_0); \
    out1 = _mm_unpackhi_epi64(tr2_0, tr2_0); \
    out2 = _mm_unpacklo_epi64(tr2_1, tr2_1); \
    out3 = _mm_unpackhi_epi64(tr2_1, tr2_1); \
    out4 = _mm_unpacklo_epi64(tr2_2, tr2_2); \
    out5 = _mm_unpackhi_epi64(tr2_2, tr2_2); \
    out6 = _mm_unpacklo_epi64(tr2_3, tr2_3); \
    out7 = _mm_unpackhi_epi64(tr2_3, tr2_3); \
  }

static void filter_horiz_w8_ssse3(const uint8_t *src_x, int src_pitch,
                                  uint8_t *dst, const int16_t *x_filter) {
  __m128i minReg;
  __m128i srcRegFilt1, srcRegFilt2, srcRegFilt3, srcRegFilt5;
  const __m128i leftShift8 = _mm_set1_epi16(1 << 8);
  const __m128i filtersReg = _mm_load_si128((const __m128i *)x_filter);
  // pack and duplicate the first and second across 128 bit register
  const __m128i firstFilters = _mm_shuffle_epi8(filtersReg,
                                                _mm_set1_epi16(0x0200u));
  // pack and duplicate the third and forth across 128 bit register
  const __m128i secondFilters = _mm_shuffle_epi8(filtersReg,
                                                 _mm_set1_epi16(0x0604u));
  // pack and duplicate the fifth and sixth across 128 bit register
  const __m128i thirdFilters = _mm_shuffle_epi8(filtersReg,
                                                _mm_set1_epi16(0x0a08u));
  // pack and duplicate the seventh and eighth across 128 bit register
  const __m128i forthFilters = _mm_shuffle_epi8(filtersReg,
                                                _mm_set1_epi16(0x0e0cu));
  const __m128i A = _mm_loadl_epi64((const __m128i *)src_x);
  const __m128i B = _mm_loadl_epi64((const __m128i *)(src_x + src_pitch));
  const __m128i C = _mm_loadl_epi64((const __m128i *)(src_x + src_pitch * 2));
  const __m128i D = _mm_loadl_epi64((const __m128i *)(src_x + src_pitch * 3));
  const __m128i E = _mm_loadl_epi64((const __m128i *)(src_x + src_pitch * 4));
  const __m128i F = _mm_loadl_epi64((const __m128i *)(src_x + src_pitch * 5));
  const __m128i G = _mm_loadl_epi64((const __m128i *)(src_x + src_pitch * 6));
  const __m128i H = _mm_loadl_epi64((const __m128i *)(src_x + src_pitch * 7));
  // 00 01 10 11 02 03 12 13 04 05 14 15 06 07 16 17
  const __m128i tr0_0 = _mm_unpacklo_epi16(A, B);
  // 20 21 30 31 22 32 23 33 42 52 43 53 62 72 63 73
  const __m128i tr0_1 = _mm_unpacklo_epi16(C, D);
  // 40 41 50 51 42 43 52 53 44 45 54 55 46 47 56 57
  const __m128i tr0_2 = _mm_unpacklo_epi16(E, F);
  const __m128i tr0_3 = _mm_unpacklo_epi16(G, H);

  // 00 01 10 11 20 21 30 31 02 03 12 13 22 32 23 33
  const __m128i tr1_0 = _mm_unpacklo_epi32(tr0_0, tr0_1);
  // 04 05 14 15 42 52 43 53 06 07 16 17 62 72 63 73
  const __m128i tr1_1 = _mm_unpackhi_epi32(tr0_0, tr0_1);
  const __m128i tr1_2 = _mm_unpacklo_epi32(tr0_2, tr0_3);
  const __m128i tr1_3 = _mm_unpackhi_epi32(tr0_2, tr0_3);

  // 00 01 10 11 20 21 30 31 40 41 50 51 60 61 70 71
  srcRegFilt1 = _mm_unpacklo_epi64(tr1_0, tr1_2);
  srcRegFilt3 = _mm_unpackhi_epi64(tr1_0, tr1_2);
  srcRegFilt2 = _mm_unpacklo_epi64(tr1_1, tr1_3);
  srcRegFilt5 = _mm_unpackhi_epi64(tr1_1, tr1_3);

  // multiply 2 adjacent elements with the filter and add the result
  srcRegFilt1 = _mm_maddubs_epi16(srcRegFilt1, firstFilters);
  srcRegFilt3 = _mm_maddubs_epi16(srcRegFilt3, secondFilters);
  srcRegFilt2 = _mm_maddubs_epi16(srcRegFilt2, thirdFilters);
  srcRegFilt5 = _mm_maddubs_epi16(srcRegFilt5, forthFilters);

  // add and saturate the results together
  minReg = _mm_min_epi16(srcRegFilt2, srcRegFilt3);
  srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, srcRegFilt5);
  srcRegFilt2 = _mm_max_epi16(srcRegFilt2, srcRegFilt3);
  srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, minReg);
  srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, srcRegFilt2);
  // round and shift by 7 bit each 16 bit
  srcRegFilt1 = _mm_mulhrs_epi16(srcRegFilt1, leftShift8);
  // shrink to 8 bit each 16 bits
  srcRegFilt1 = _mm_packus_epi16(srcRegFilt1, srcRegFilt1);
  // save only 8 bytes convolve result
  _mm_storel_epi64((__m128i*)dst, srcRegFilt1);
}

void transpose8x8_to_dst(const uint8_t *src, ptrdiff_t src_stride,
                         uint8_t *dst, ptrdiff_t dst_stride) {
  __m128i A, B, C, D, E, F, G, H;

  A = _mm_loadl_epi64((const __m128i *)src);
  B = _mm_loadl_epi64((const __m128i *)(src + src_stride));
  C = _mm_loadl_epi64((const __m128i *)(src + src_stride * 2));
  D = _mm_loadl_epi64((const __m128i *)(src + src_stride * 3));
  E = _mm_loadl_epi64((const __m128i *)(src + src_stride * 4));
  F = _mm_loadl_epi64((const __m128i *)(src + src_stride * 5));
  G = _mm_loadl_epi64((const __m128i *)(src + src_stride * 6));
  H = _mm_loadl_epi64((const __m128i *)(src + src_stride * 7));

  TRANSPOSE_8X8(A, B, C, D, E, F, G, H,
                A, B, C, D, E, F, G, H);

  _mm_storel_epi64((__m128i*)dst, A);
  _mm_storel_epi64((__m128i*)(dst + dst_stride * 1), B);
  _mm_storel_epi64((__m128i*)(dst + dst_stride * 2), C);
  _mm_storel_epi64((__m128i*)(dst + dst_stride * 3), D);
  _mm_storel_epi64((__m128i*)(dst + dst_stride * 4), E);
  _mm_storel_epi64((__m128i*)(dst + dst_stride * 5), F);
  _mm_storel_epi64((__m128i*)(dst + dst_stride * 6), G);
  _mm_storel_epi64((__m128i*)(dst + dst_stride * 7), H);
}

static void scaledconvolve_horiz_w8(const uint8_t *src, ptrdiff_t src_stride,
                                    uint8_t *dst, ptrdiff_t dst_stride,
                                    const InterpKernel *x_filters,
                                    int x0_q4, int x_step_q4, int w, int h) {
  DECLARE_ALIGNED(16, uint8_t, temp[8 * 8]);
  int x, y, z;
  src -= SUBPEL_TAPS / 2 - 1;

  for (y = 0; y < h; y += 8) {
    int x_q4 = x0_q4;
    for (x = 0; x < w; x += 8) {
      // process 8 src_x steps
      for (z = 0; z < 8; ++z) {
        const uint8_t *const src_x = &src[x_q4 >> SUBPEL_BITS];
        const int16_t *const x_filter = x_filters[x_q4 & SUBPEL_MASK];
        if (x_q4 & SUBPEL_MASK) {
          filter_horiz_w8_ssse3(src_x, src_stride, temp + (z * 8), x_filter);
        } else {
          int i;
          for (i = 0; i < 8; ++i) {
            temp[z * 8 + i] = src_x[i * src_stride + 3];
          }
        }
        x_q4 += x_step_q4;
      }

      // transpose the 8x8 filters values back to dst
      transpose8x8_to_dst(temp, 8, dst + x, dst_stride);
    }

    src += src_stride * 8;
    dst += dst_stride * 8;
  }
}

static void filter_horiz_w4_ssse3(const uint8_t *src_ptr, ptrdiff_t src_pitch,
                                  uint8_t *dst, const int16_t *filter) {
  __m64 minReg;
  __m64 srcRegFilt1, srcRegFilt2, srcRegFilt3, srcRegFilt5;
  const __m64 leftShift8 = _mm_set1_pi16(1 << 8);
  const __m64 filtersReg = *((const __m64 *)filter);
  const __m64 filtersReg2 = *((const __m64 *)(filter + 4));
  // pack and duplicate the first and second across 128 bit register
  const __m64 firstFilters = _mm_shuffle_pi8(filtersReg,
                                             _mm_set1_pi16(0x0200u));
  // pack and duplicate the third and forth across 128 bit register
  const __m64 secondFilters = _mm_shuffle_pi8(filtersReg,
                                              _mm_set1_pi16(0x0604u));
  // pack and duplicate the fifth and sixth across 128 bit register
  const __m64 thirdFilters = _mm_shuffle_pi8(filtersReg2,
                                             _mm_set1_pi16(0x0a08u));
  // pack and duplicate the seventh and eighth across 128 bit register
  const __m64 forthFilters = _mm_shuffle_pi8(filtersReg2,
                                             _mm_set1_pi16(0x0e0cu));
  const __m64 A = *((const __m64 *)src_ptr);
  const __m64 B = *((const __m64 *)(src_ptr + src_pitch));
  const __m64 C = *((const __m64 *)(src_ptr + src_pitch * 2));
  const __m64 D = *((const __m64 *)(src_ptr + src_pitch * 3));
  // TRANSPOSE...
  // 00 01 02 03 04 05 06 07
  // 10 11 12 13 14 15 16 17
  // 20 21 22 23 24 25 26 27
  // 30 31 32 33 34 35 36 37
  //
  // TO
  //
  // 00 10 20 30
  // 01 11 21 31
  // 02 12 22 32
  // 03 13 23 33
  // 04 14 24 34
  // 05 15 25 35
  // 06 16 26 36
  // 07 17 27 37
  //
  // 00 01 10 11 02 03 12 13
  const __m64 tr0_0 = _mm_unpacklo_pi16(A, B);
  // 20 21 30 31 22 23 32 33
  const __m64 tr0_1 = _mm_unpacklo_pi16(C, D);
  // 04 05 14 15 06 07 16 17
  const __m64 tr0_2 = _mm_unpackhi_pi16(A, B);
  // 24 25 34 35 26 27 36 37
  const __m64 tr0_3 = _mm_unpackhi_pi16(C, D);

  // 00 01 10 11 20 21 30 31
  srcRegFilt1 = _mm_unpacklo_pi32(tr0_0, tr0_1);
  // 02 03 12 13 22 23 32 33
  srcRegFilt3 = _mm_unpackhi_pi32(tr0_0, tr0_1);
  // 04 05 14 15 24 25 34 35
  srcRegFilt2 = _mm_unpacklo_pi32(tr0_2, tr0_3);
  // 06 07 16 17 26 27 36 37
  srcRegFilt5 = _mm_unpackhi_pi32(tr0_2, tr0_3);

  // multiply 2 adjacent elements with the filter and add the result
  srcRegFilt1 = _mm_maddubs_pi16(srcRegFilt1, firstFilters);
  srcRegFilt3 = _mm_maddubs_pi16(srcRegFilt3, secondFilters);
  srcRegFilt2 = _mm_maddubs_pi16(srcRegFilt2, thirdFilters);
  srcRegFilt5 = _mm_maddubs_pi16(srcRegFilt5, forthFilters);

  // add and saturate the results together
  minReg = _mm_min_pi16(srcRegFilt2, srcRegFilt3);
  srcRegFilt1 = _mm_adds_pi16(srcRegFilt1, srcRegFilt5);
  srcRegFilt2 = _mm_max_pi16(srcRegFilt2, srcRegFilt3);
  srcRegFilt1 = _mm_adds_pi16(srcRegFilt1, minReg);
  srcRegFilt1 = _mm_adds_pi16(srcRegFilt1, srcRegFilt2);
  // round and shift by 7 bit each 16 bit
  srcRegFilt1 = _mm_mulhrs_pi16(srcRegFilt1, leftShift8);
  // shrink to 8 bit each 16 bits
  srcRegFilt1 = _mm_packs_pu16(srcRegFilt1, srcRegFilt1);
  // save only 4 bytes
  *(int *)dst = _mm_cvtsi64_si32(srcRegFilt1);
}

void transpose4x4_to_dst(const uint8_t *src, ptrdiff_t src_stride,
                         uint8_t *dst, ptrdiff_t dst_stride) {
  __m64 A = _mm_cvtsi32_si64(*(const int *)src);
  __m64 B = _mm_cvtsi32_si64(*(const int *)(src + src_stride));
  __m64 C = _mm_cvtsi32_si64(*(const int *)(src + src_stride * 2));
  __m64 D = _mm_cvtsi32_si64(*(const int *)(src + src_stride * 3));
  // 00 10 01 11 02 12 03 13
  const __m64 tr0_0 = _mm_unpacklo_pi8(A, B);
  // 20 30 21 31 22 32 23 33
  const __m64 tr0_1 = _mm_unpacklo_pi8(C, D);
  // 00 10 20 30 01 11 21 31
  A = _mm_unpacklo_pi16(tr0_0, tr0_1);
  // 02 12 22 32 03 13 23 33
  C = _mm_unpackhi_pi16(tr0_0, tr0_1);

  B = _mm_unpackhi_pi32(A, A);
  D = _mm_unpackhi_pi32(C, C);

  *(int *)(dst) =  _mm_cvtsi64_si32(A);
  *(int *)(dst + dst_stride) =  _mm_cvtsi64_si32(B);
  *(int *)(dst + dst_stride * 2) =  _mm_cvtsi64_si32(C);
  *(int *)(dst + dst_stride * 3) =  _mm_cvtsi64_si32(D);
}

static void scaledconvolve_horiz_w4(const uint8_t *src, ptrdiff_t src_stride,
                                    uint8_t *dst, ptrdiff_t dst_stride,
                                    const InterpKernel *x_filters,
                                    int x0_q4, int x_step_q4, int w, int h) {
  DECLARE_ALIGNED(16, uint8_t, temp[4 * 4]);
  int x, y, z;
  src -= SUBPEL_TAPS / 2 - 1;

  for (y = 0; y < h; y += 4) {
    int x_q4 = x0_q4;
    for (x = 0; x < w; x += 4) {
      // process 4 src_x steps
      for (z = 0; z < 4; ++z) {
        const uint8_t *const src_x = &src[x_q4 >> SUBPEL_BITS];
        const int16_t *const x_filter = x_filters[x_q4 & SUBPEL_MASK];
        if (x_q4 & SUBPEL_MASK) {
          filter_horiz_w4_ssse3(src_x, src_stride, temp + (z * 4), x_filter);
        } else {
          int i;
          for (i = 0; i < 4; ++i) {
            temp[z * 4 + i] = src_x[i * src_stride + 3];
          }
        }
        x_q4 += x_step_q4;
      }

      // transpose the 4x4 filters values back to dst
      transpose4x4_to_dst(temp, 4, dst + x, dst_stride);
    }

    src += src_stride * 4;
    dst += dst_stride * 4;
  }
}

static void filter_vert_w4_ssse3(const uint8_t *src_ptr, ptrdiff_t src_pitch,
                                 uint8_t *dst, const int16_t *filter) {
  __m64 minReg;
  __m64 srcRegFilt1, srcRegFilt2, srcRegFilt3, srcRegFilt5;
  const __m64 leftShift8 = _mm_set1_pi16(1 << 8);
  const __m64 filtersReg = *((const __m64 *)filter);
  const __m64 filtersReg2 = *((const __m64 *)(filter + 4));
  // pack and duplicate the first and second across 128 bit register
  const __m64 firstFilters = _mm_shuffle_pi8(filtersReg,
                                             _mm_set1_pi16(0x0200u));
  // pack and duplicate the third and forth across 128 bit register
  const __m64 secondFilters = _mm_shuffle_pi8(filtersReg,
                                              _mm_set1_pi16(0x0604u));
  // pack and duplicate the fifth and sixth across 128 bit register
  const __m64 thirdFilters = _mm_shuffle_pi8(filtersReg2,
                                             _mm_set1_pi16(0x0a08u));
  // pack and duplicate the seventh and eighth across 128 bit register
  const __m64 forthFilters = _mm_shuffle_pi8(filtersReg2,
                                             _mm_set1_pi16(0x0e0cu));
  const __m64 A = _mm_cvtsi32_si64(*(const int *)src_ptr);
  const __m64 B = _mm_cvtsi32_si64(*(const int *)(src_ptr + src_pitch));
  const __m64 C = _mm_cvtsi32_si64(*(const int *)(src_ptr + src_pitch * 2));
  const __m64 D = _mm_cvtsi32_si64(*(const int *)(src_ptr + src_pitch * 3));
  const __m64 E = _mm_cvtsi32_si64(*(const int *)(src_ptr + src_pitch * 4));
  const __m64 F = _mm_cvtsi32_si64(*(const int *)(src_ptr + src_pitch * 5));
  const __m64 G = _mm_cvtsi32_si64(*(const int *)(src_ptr + src_pitch * 6));
  const __m64 H = _mm_cvtsi32_si64(*(const int *)(src_ptr + src_pitch * 7));

  // merge the result together
  srcRegFilt1 = _mm_unpacklo_pi8(A, B);
  srcRegFilt3 = _mm_unpacklo_pi8(C, D);

  // merge the result together
  srcRegFilt2 = _mm_unpacklo_pi8(E, F);
  srcRegFilt5 = _mm_unpacklo_pi8(G, H);

  // multiply 2 adjacent elements with the filter and add the result
  srcRegFilt1 = _mm_maddubs_pi16(srcRegFilt1, firstFilters);
  srcRegFilt3 = _mm_maddubs_pi16(srcRegFilt3, secondFilters);
  srcRegFilt2 = _mm_maddubs_pi16(srcRegFilt2, thirdFilters);
  srcRegFilt5 = _mm_maddubs_pi16(srcRegFilt5, forthFilters);

  // add and saturate the results together
  minReg = _mm_min_pi16(srcRegFilt2, srcRegFilt3);
  srcRegFilt1 = _mm_adds_pi16(srcRegFilt1, srcRegFilt5);
  srcRegFilt2 = _mm_max_pi16(srcRegFilt2, srcRegFilt3);
  srcRegFilt1 = _mm_adds_pi16(srcRegFilt1, minReg);
  srcRegFilt1 = _mm_adds_pi16(srcRegFilt1, srcRegFilt2);
  // round and shift by 7 bit each 16 bit
  srcRegFilt1 = _mm_mulhrs_pi16(srcRegFilt1, leftShift8);
  // shrink to 8 bit each 16 bits
  srcRegFilt1 = _mm_packs_pu16(srcRegFilt1, srcRegFilt1);
  // save only 4 bytes
  *(int *)dst = _mm_cvtsi64_si32(srcRegFilt1);
}

static void scaledconvolve_vert_w4(const uint8_t *src, ptrdiff_t src_stride,
                                   uint8_t *dst, ptrdiff_t dst_stride,
                                   const InterpKernel *y_filters,
                                   int y0_q4, int y_step_q4, int w, int h) {
  int y;
  int y_q4 = y0_q4;

  src -= src_stride * (SUBPEL_TAPS / 2 - 1);
  for (y = 0; y < h; ++y) {
    const unsigned char *src_y = &src[(y_q4 >> SUBPEL_BITS) * src_stride];
    const int16_t *const y_filter = y_filters[y_q4 & SUBPEL_MASK];

    if (y_q4 & SUBPEL_MASK) {
      filter_vert_w4_ssse3(src_y, src_stride, &dst[y * dst_stride], y_filter);
    } else {
      memcpy(&dst[y * dst_stride], &src_y[3 * src_stride], w);
    }

    y_q4 += y_step_q4;
  }
}

static void filter_vert_w8_ssse3(const uint8_t *src_ptr, ptrdiff_t src_pitch,
                                 uint8_t *dst, const int16_t *filter) {
  __m128i minReg;
  __m128i srcRegFilt1, srcRegFilt2, srcRegFilt3, srcRegFilt5;
  __m128i srcReg1, srcReg2, srcReg3, srcReg4, srcReg5, srcReg6, srcReg7;
  __m128i srcReg8;
  const __m128i leftShift8 = _mm_set1_epi16(1 << 8);
  const __m128i filtersReg = _mm_load_si128((const __m128i *)filter);
  // pack and duplicate the first and second across 128 bit register
  const __m128i firstFilters = _mm_shuffle_epi8(filtersReg,
                                                _mm_set1_epi16(0x0200u));
  // pack and duplicate the third and forth across 128 bit register
  const __m128i secondFilters = _mm_shuffle_epi8(filtersReg,
                                                 _mm_set1_epi16(0x0604u));
  // pack and duplicate the fifth and sixth across 128 bit register
  const __m128i thirdFilters = _mm_shuffle_epi8(filtersReg,
                                                _mm_set1_epi16(0x0a08u));
  // pack and duplicate the seventh and eighth across 128 bit register
  const __m128i forthFilters = _mm_shuffle_epi8(filtersReg,
                                                _mm_set1_epi16(0x0e0cu));
  srcReg1 = _mm_loadl_epi64((const __m128i *)src_ptr);
  srcReg2 = _mm_loadl_epi64((const __m128i *)(src_ptr + src_pitch));
  srcReg3 = _mm_loadl_epi64((const __m128i *)(src_ptr + src_pitch * 2));
  srcReg4 = _mm_loadl_epi64((const __m128i *)(src_ptr + src_pitch * 3));
  srcReg5 = _mm_loadl_epi64((const __m128i *)(src_ptr + src_pitch * 4));
  srcReg6 = _mm_loadl_epi64((const __m128i *)(src_ptr + src_pitch * 5));
  srcReg7 = _mm_loadl_epi64((const __m128i *)(src_ptr + src_pitch * 6));
  srcReg8 = _mm_loadl_epi64((const __m128i *)(src_ptr + src_pitch * 7));

  // merge the result together
  srcRegFilt1 = _mm_unpacklo_epi8(srcReg1, srcReg2);
  srcRegFilt3 = _mm_unpacklo_epi8(srcReg3, srcReg4);

  // merge the result together
  srcRegFilt2 = _mm_unpacklo_epi8(srcReg5, srcReg6);
  srcRegFilt5 = _mm_unpacklo_epi8(srcReg7, srcReg8);

  // multiply 2 adjacent elements with the filter and add the result
  srcRegFilt1 = _mm_maddubs_epi16(srcRegFilt1, firstFilters);
  srcRegFilt3 = _mm_maddubs_epi16(srcRegFilt3, secondFilters);
  srcRegFilt2 = _mm_maddubs_epi16(srcRegFilt2, thirdFilters);
  srcRegFilt5 = _mm_maddubs_epi16(srcRegFilt5, forthFilters);

  // add and saturate the results together
  minReg = _mm_min_epi16(srcRegFilt2, srcRegFilt3);
  srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, srcRegFilt5);
  srcRegFilt2 = _mm_max_epi16(srcRegFilt2, srcRegFilt3);
  srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, minReg);
  srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, srcRegFilt2);
  // round and shift by 7 bit each 16 bit
  srcRegFilt1 = _mm_mulhrs_epi16(srcRegFilt1, leftShift8);
  // shrink to 8 bit each 16 bits
  srcRegFilt1 = _mm_packus_epi16(srcRegFilt1, srcRegFilt1);
  // save only 8 bytes convolve result
  _mm_storel_epi64((__m128i*)dst, srcRegFilt1);
}

static void scaledconvolve_vert_w8(const uint8_t *src, ptrdiff_t src_stride,
                                   uint8_t *dst, ptrdiff_t dst_stride,
                                   const InterpKernel *y_filters,
                                   int y0_q4, int y_step_q4, int w, int h) {
  int y;
  int y_q4 = y0_q4;

  src -= src_stride * (SUBPEL_TAPS / 2 - 1);
  for (y = 0; y < h; ++y) {
    const unsigned char *src_y = &src[(y_q4 >> SUBPEL_BITS) * src_stride];
    const int16_t *const y_filter = y_filters[y_q4 & SUBPEL_MASK];
    if (y_q4 & SUBPEL_MASK) {
      filter_vert_w8_ssse3(src_y, src_stride, &dst[y * dst_stride], y_filter);
    } else {
      memcpy(&dst[y * dst_stride], &src_y[3 * src_stride], w);
    }
    y_q4 += y_step_q4;
  }
}

static void filter_vert_w16_ssse3(const uint8_t *src_ptr, ptrdiff_t src_pitch,
                                  uint8_t *dst, const int16_t *filter,
                                  int w) {
  __m128i srcRegFilt1, srcRegFilt3;
  __m128i srcRegFilt5, srcRegFilt6, srcRegFilt7, srcRegFilt8;
  __m128i srcReg1, srcReg2, srcReg3, srcReg4, srcReg5, srcReg6, srcReg7;
  __m128i srcReg8;
  const __m128i leftShift8 = _mm_set1_epi16(1 << 8);
  const __m128i filtersReg = _mm_load_si128((const __m128i *)filter);

  // pack and duplicate the first and second across 128 bit register
  const __m128i firstFilters = _mm_shuffle_epi8(filtersReg,
                                                _mm_set1_epi16(0x0200u));
  // pack and duplicate the third and forth across 128 bit register
  const __m128i secondFilters = _mm_shuffle_epi8(filtersReg,
                                                 _mm_set1_epi16(0x0604u));
  // pack and duplicate the fifth and sixth across 128 bit register
  const __m128i thirdFilters = _mm_shuffle_epi8(filtersReg,
                                                _mm_set1_epi16(0x0a08u));
  // pack and duplicate the seventh and eighth across 128 bit register
  const __m128i forthFilters = _mm_shuffle_epi8(filtersReg,
                                                _mm_set1_epi16(0x0e0cu));
  int i;

  for (i = 0; i < w; i += 16) {
    srcReg1 = _mm_loadu_si128((const __m128i *)(src_ptr));
    srcReg2 = _mm_loadu_si128((const __m128i *)(src_ptr + src_pitch));
    srcReg3 = _mm_loadu_si128((const __m128i *)(src_ptr + src_pitch * 2));
    srcReg4 = _mm_loadu_si128((const __m128i *)(src_ptr + src_pitch * 3));
    srcReg5 = _mm_loadu_si128((const __m128i *)(src_ptr + src_pitch * 4));
    srcReg6 = _mm_loadu_si128((const __m128i *)(src_ptr + src_pitch * 5));
    srcReg7 = _mm_loadu_si128((const __m128i *)(src_ptr + src_pitch * 6));
    srcReg8 = _mm_loadu_si128((const __m128i *)(src_ptr + src_pitch * 7));
    src_ptr += 16;

    // merge the result together
    srcRegFilt5 = _mm_unpacklo_epi8(srcReg1, srcReg2);
    srcRegFilt6 = _mm_unpacklo_epi8(srcReg7, srcReg8);
    srcRegFilt1 = _mm_unpackhi_epi8(srcReg1, srcReg2);
    srcRegFilt3 = _mm_unpackhi_epi8(srcReg7, srcReg8);

    // multiply 2 adjacent elements with the filter and add the result
    srcRegFilt5 = _mm_maddubs_epi16(srcRegFilt5, firstFilters);
    srcRegFilt6 = _mm_maddubs_epi16(srcRegFilt6, forthFilters);
    srcRegFilt1 = _mm_maddubs_epi16(srcRegFilt1, firstFilters);
    srcRegFilt3 = _mm_maddubs_epi16(srcRegFilt3, forthFilters);

    // add and saturate the results together
    srcRegFilt5 = _mm_adds_epi16(srcRegFilt5, srcRegFilt6);
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1, srcRegFilt3);

    // merge the result together
    srcRegFilt3 = _mm_unpacklo_epi8(srcReg3, srcReg4);
    srcRegFilt6 = _mm_unpackhi_epi8(srcReg3, srcReg4);

    // multiply 2 adjacent elements with the filter and add the result
    srcRegFilt3 = _mm_maddubs_epi16(srcRegFilt3, secondFilters);
    srcRegFilt6 = _mm_maddubs_epi16(srcRegFilt6, secondFilters);

    // merge the result together
    srcRegFilt7 = _mm_unpacklo_epi8(srcReg5, srcReg6);
    srcRegFilt8 = _mm_unpackhi_epi8(srcReg5, srcReg6);

    // multiply 2 adjacent elements with the filter and add the result
    srcRegFilt7 = _mm_maddubs_epi16(srcRegFilt7, thirdFilters);
    srcRegFilt8 = _mm_maddubs_epi16(srcRegFilt8, thirdFilters);

    // add and saturate the results together
    srcRegFilt5 = _mm_adds_epi16(srcRegFilt5,
                                 _mm_min_epi16(srcRegFilt3, srcRegFilt7));
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1,
                                 _mm_min_epi16(srcRegFilt6, srcRegFilt8));

    // add and saturate the results together
    srcRegFilt5 = _mm_adds_epi16(srcRegFilt5,
                                 _mm_max_epi16(srcRegFilt3, srcRegFilt7));
    srcRegFilt1 = _mm_adds_epi16(srcRegFilt1,
                                 _mm_max_epi16(srcRegFilt6, srcRegFilt8));
    // round and shift by 7 bit each 16 bit
    srcRegFilt5 = _mm_mulhrs_epi16(srcRegFilt5, leftShift8);
    srcRegFilt1 = _mm_mulhrs_epi16(srcRegFilt1, leftShift8);
    // shrink to 8 bit each 16 bits, the first lane contain the first
    // convolve result and the second lane contain the second convolve
    // result
    srcRegFilt1 = _mm_packus_epi16(srcRegFilt5, srcRegFilt1);

     // save 16 bytes convolve result
    _mm_store_si128((__m128i*)&dst[i], srcRegFilt1);
  }
}

static void scaledconvolve_vert_w16(const uint8_t *src, ptrdiff_t src_stride,
                                    uint8_t *dst, ptrdiff_t dst_stride,
                                    const InterpKernel *y_filters,
                                    int y0_q4, int y_step_q4, int w, int h) {
  int y;
  int y_q4 = y0_q4;

  src -= src_stride * (SUBPEL_TAPS / 2 - 1);
  for (y = 0; y < h; ++y) {
    const unsigned char *src_y = &src[(y_q4 >> SUBPEL_BITS) * src_stride];
    const int16_t *const y_filter = y_filters[y_q4 & SUBPEL_MASK];
    if (y_q4 & SUBPEL_MASK) {
      filter_vert_w16_ssse3(src_y, src_stride, &dst[y * dst_stride], y_filter,
                            w);
    } else {
      memcpy(&dst[y * dst_stride], &src_y[3 * src_stride], w);
    }
    y_q4 += y_step_q4;
  }
}

static void scaledconvolve2d(const uint8_t *src, ptrdiff_t src_stride,
                             uint8_t *dst, ptrdiff_t dst_stride,
                             const InterpKernel *const x_filters,
                             int x0_q4, int x_step_q4,
                             const InterpKernel *const y_filters,
                             int y0_q4, int y_step_q4,
                             int w, int h) {
  // Note: Fixed size intermediate buffer, temp, places limits on parameters.
  // 2d filtering proceeds in 2 steps:
  //   (1) Interpolate horizontally into an intermediate buffer, temp.
  //   (2) Interpolate temp vertically to derive the sub-pixel result.
  // Deriving the maximum number of rows in the temp buffer (135):
  // --Smallest scaling factor is x1/2 ==> y_step_q4 = 32 (Normative).
  // --Largest block size is 64x64 pixels.
  // --64 rows in the downscaled frame span a distance of (64 - 1) * 32 in the
  //   original frame (in 1/16th pixel units).
  // --Must round-up because block may be located at sub-pixel position.
  // --Require an additional SUBPEL_TAPS rows for the 8-tap filter tails.
  // --((64 - 1) * 32 + 15) >> 4 + 8 = 135.
  DECLARE_ALIGNED(16, uint8_t, temp[(135 + 8) * 64]);
  int intermediate_height =
          (((h - 1) * y_step_q4 + y0_q4) >> SUBPEL_BITS) + SUBPEL_TAPS;

  assert(w <= 64);
  assert(h <= 64);
  assert(y_step_q4 <= 32);
  assert(x_step_q4 <= 32);

  if (w >= 8) {
    scaledconvolve_horiz_w8(src - src_stride * (SUBPEL_TAPS / 2 - 1),
                            src_stride, temp, 64, x_filters, x0_q4, x_step_q4,
                            w, intermediate_height);
  } else {
    scaledconvolve_horiz_w4(src - src_stride * (SUBPEL_TAPS / 2 - 1),
                            src_stride, temp, 64, x_filters, x0_q4, x_step_q4,
                            w, intermediate_height);
  }

  if (w >= 16) {
    scaledconvolve_vert_w16(temp + 64 * (SUBPEL_TAPS / 2 - 1), 64, dst,
                            dst_stride, y_filters, y0_q4, y_step_q4, w, h);
  } else if (w == 8) {
    scaledconvolve_vert_w8(temp + 64 * (SUBPEL_TAPS / 2 - 1), 64, dst,
                           dst_stride, y_filters, y0_q4, y_step_q4, w, h);
  } else {
    scaledconvolve_vert_w4(temp + 64 * (SUBPEL_TAPS / 2 - 1), 64, dst,
                           dst_stride, y_filters, y0_q4, y_step_q4, w, h);
  }
}

static const InterpKernel *get_filter_base(const int16_t *filter) {
  // NOTE: This assumes that the filter table is 256-byte aligned.
  // TODO(agrange) Modify to make independent of table alignment.
  return (const InterpKernel *)(((intptr_t)filter) & ~((intptr_t)0xFF));
}

static int get_filter_offset(const int16_t *f, const InterpKernel *base) {
  return (int)((const InterpKernel *)(intptr_t)f - base);
}

void vp9_scaledconvolve8_ssse3(const uint8_t *src, ptrdiff_t src_stride,
                               uint8_t *dst, ptrdiff_t dst_stride,
                               const int16_t *filter_x, int x_step_q4,
                               const int16_t *filter_y, int y_step_q4,
                               int w, int h) {
  const InterpKernel *const filters_x = get_filter_base(filter_x);
  const int x0_q4 = get_filter_offset(filter_x, filters_x);

  const InterpKernel *const filters_y = get_filter_base(filter_y);
  const int y0_q4 = get_filter_offset(filter_y, filters_y);

  scaledconvolve2d(src, src_stride, dst, dst_stride,
                   filters_x, x0_q4, x_step_q4,
                   filters_y, y0_q4, y_step_q4, w, h);
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// void vp9_convolve8_ssse3(const uint8_t *src, ptrdiff_t src_stride,
//                          uint8_t *dst, ptrdiff_t dst_stride,
//                          const int16_t *filter_x, int x_step_q4,
//                          const int16_t *filter_y, int y_step_q4,
//                          int w, int h);
// void vp9_convolve8_avg_ssse3(const uint8_t *src, ptrdiff_t src_stride,
//                              uint8_t *dst, ptrdiff_t dst_stride,
//                              const int16_t *filter_x, int x_step_q4,
//                              const int16_t *filter_y, int y_step_q4,
//                              int w, int h);
FUN_CONV_2D(, ssse3, scaledconvolve8_ssse3);
FUN_CONV_2D(avg_ , ssse3, convolve8_avg_c);
