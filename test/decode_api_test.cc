/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "third_party/googletest/src/include/gtest/gtest.h"

#include "test/ivf_video_source.h"
#include "./vpx_config.h"
#include "vpx/vp8dx.h"
#include "vpx/vpx_decoder.h"

namespace {

#define NELEMENTS(x) static_cast<int>(sizeof(x) / sizeof(x[0]))

TEST(DecodeAPI, InvalidParams) {
  static const vpx_codec_iface_t *kCodecs[] = {
#if CONFIG_VP8_DECODER
    &vpx_codec_vp8_dx_algo,
#endif
#if CONFIG_VP9_DECODER
    &vpx_codec_vp9_dx_algo,
#endif
  };
  uint8_t buf[1] = {0};
  vpx_codec_ctx_t dec;

  EXPECT_EQ(VPX_CODEC_INVALID_PARAM, vpx_codec_dec_init(NULL, NULL, NULL, 0));
  EXPECT_EQ(VPX_CODEC_INVALID_PARAM, vpx_codec_dec_init(&dec, NULL, NULL, 0));
  EXPECT_EQ(VPX_CODEC_INVALID_PARAM, vpx_codec_decode(NULL, NULL, 0, NULL, 0));
  EXPECT_EQ(VPX_CODEC_INVALID_PARAM, vpx_codec_decode(NULL, buf, 0, NULL, 0));
  EXPECT_EQ(VPX_CODEC_INVALID_PARAM,
            vpx_codec_decode(NULL, buf, NELEMENTS(buf), NULL, 0));
  EXPECT_EQ(VPX_CODEC_INVALID_PARAM,
            vpx_codec_decode(NULL, NULL, NELEMENTS(buf), NULL, 0));
  EXPECT_EQ(VPX_CODEC_INVALID_PARAM, vpx_codec_destroy(NULL));
  EXPECT_TRUE(vpx_codec_error(NULL) != NULL);

  for (int i = 0; i < NELEMENTS(kCodecs); ++i) {
    EXPECT_EQ(VPX_CODEC_INVALID_PARAM,
              vpx_codec_dec_init(NULL, kCodecs[i], NULL, 0));

    EXPECT_EQ(VPX_CODEC_OK, vpx_codec_dec_init(&dec, kCodecs[i], NULL, 0));
    EXPECT_EQ(VPX_CODEC_UNSUP_BITSTREAM,
              vpx_codec_decode(&dec, buf, NELEMENTS(buf), NULL, 0));
    EXPECT_EQ(VPX_CODEC_INVALID_PARAM,
              vpx_codec_decode(&dec, NULL, NELEMENTS(buf), NULL, 0));
    EXPECT_EQ(VPX_CODEC_INVALID_PARAM,
              vpx_codec_decode(&dec, buf, 0, NULL, 0));

    EXPECT_EQ(VPX_CODEC_OK, vpx_codec_destroy(&dec));
  }
}

#if CONFIG_VP9_DECODER

void TestVp9Controls(vpx_codec_ctx_t *dec) {
  static const int kControls[] = {
    VP8D_GET_LAST_REF_UPDATES,
    VP8D_GET_FRAME_CORRUPTED,
    VP9D_GET_DISPLAY_SIZE,
  };
  int val[2];

  for (int i = 0; i < NELEMENTS(kControls); ++i) {
    EXPECT_EQ(VPX_CODEC_OK, vpx_codec_control_(dec, kControls[i], val));
    EXPECT_EQ(VPX_CODEC_INVALID_PARAM,
              vpx_codec_control_(dec, kControls[i], NULL));
  }
}

TEST(DecodeAPI, Vp9InvalidDecode) {
  const vpx_codec_iface_t *const codec = &vpx_codec_vp9_dx_algo;
  const char filename[] =
      "invalid-vp90-2-00-quantizer-00.webm.ivf.s5861_r01-05_b6-.v2.ivf";
  libvpx_test::IVFVideoSource video(filename);
  video.Init();
  video.Begin();
  ASSERT_TRUE(!HasFailure());

  vpx_codec_ctx_t dec;
  EXPECT_EQ(VPX_CODEC_OK, vpx_codec_dec_init(&dec, codec, NULL, 0));
  EXPECT_EQ(VPX_CODEC_MEM_ERROR,
            vpx_codec_decode(&dec, video.cxdata(), video.frame_size(), NULL,
                             0));
  TestVp9Controls(&dec);
  EXPECT_EQ(VPX_CODEC_OK, vpx_codec_destroy(&dec));
}
#endif  // CONFIG_VP9_DECODER

}  // namespace
