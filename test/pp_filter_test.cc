/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "third_party/googletest/src/include/gtest/gtest.h"
extern "C" {
  #include "vpx_mem/vpx_mem.h"
  #include "./vpx_rtcd.h"
  #include "vpx/vpx_integer.h"
}

namespace {

// Test routine for the VP8 post-processing function
// vp8_post_proc_down_and_across_c.

TEST(Vp8PostProcessingFilterTest, FilterOutputCheck) {
  // Size of the underlying data block that will be filtered.
  const uint32_t block_width  = 16;
  const uint32_t block_height = 16;

  // 5-tap filter needs 2 padding rows above and below the block in the input.
  const uint32_t input_width = block_width;
  const uint32_t input_height = block_height + 4;
  const uint32_t input_stride = input_width;
  const uint32_t input_size = input_width * input_height;

  // Filter extends output block by 8 samples at left and right edges.
  const uint32_t output_width = block_width + 16;
  const uint32_t output_height = block_height;
  const uint32_t output_stride = output_width;
  const uint32_t output_size = output_width * output_height;

  uint8_t *src_image = reinterpret_cast<uint8_t*>(vpx_calloc(input_size, 1));
  uint8_t *dst_image = reinterpret_cast<uint8_t*>(vpx_calloc(output_size, 1));

  // Pointers to top-left pixel of block in the input and output images.
  uint8_t *src_image_ptr = src_image + (input_stride << 1);
  uint8_t *dst_image_ptr = dst_image + 8;

  // Initialize pixels in the input:
  //   block pixels to value 1,
  //   border pixels to value 10.
  (void)vpx_memset(src_image, 10, input_size);
  uint8_t *pixel_ptr = src_image_ptr;
  for (uint32_t i = 0; i < block_height; ++i) {
    for (uint32_t j = 0; j < block_width; ++j) {
      pixel_ptr[j] = 1;
    }
    pixel_ptr += input_stride;
  }

  // Initialize pixels in the output to 99.
  (void)vpx_memset(dst_image, 99, output_size);

  vp8_post_proc_down_and_across_c(src_image_ptr, dst_image_ptr, input_stride,
                                  output_stride, block_height, block_width,
                                  255);

  // Expected output values for the filtered block.
  static const uint8_t expected_data[block_width*block_height] = {
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};

  uint32_t pixel = 0;
  pixel_ptr = dst_image;
  for (uint32_t i = 0; i < block_height; ++i) {
    for (uint32_t j = 0; j < block_width; ++j) {
      EXPECT_EQ(pixel_ptr[j], expected_data[pixel])
        << "Vp8PostProcessingFilterTest failed with invalid filter output";
      ++pixel;
    }
    pixel_ptr += output_stride;
  }

  // Free allocated memory.
  vpx_free(src_image);
  vpx_free(dst_image);
};

}  // namespace
