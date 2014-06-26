/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include <climits>
#include <vector>
#include "third_party/googletest/src/include/gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"
#include "test/y4m_video_source.h"

namespace {

const int kMaxPsnr = 100;

class CpuSpeedTest : public ::libvpx_test::EncoderTest,
    public ::libvpx_test::CodecTestWith2Params<
        libvpx_test::TestMode, int> {
 protected:
  CpuSpeedTest()
      : EncoderTest(GET_PARAM(0)),
        encoding_mode_(GET_PARAM(1)),
        set_cpu_used_(GET_PARAM(2)),
        psnr_(kMaxPsnr) {}
  virtual ~CpuSpeedTest() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);
    if (encoding_mode_ != ::libvpx_test::kRealTime) {
      cfg_.g_lag_in_frames = 25;
      cfg_.rc_end_usage = VPX_VBR;
    } else {
      cfg_.g_lag_in_frames = 0;
      cfg_.rc_end_usage = VPX_CBR;
    }
  }

  virtual void BeginPassHook(unsigned int /*pass*/) {
    psnr_ = kMaxPsnr;
  }

  virtual void PreEncodeFrameHook(::libvpx_test::VideoSource *video,
                                  ::libvpx_test::Encoder *encoder) {
    if (video->frame() == 1) {
      encoder->Control(VP8E_SET_CPUUSED, set_cpu_used_);
      if (encoding_mode_ != ::libvpx_test::kRealTime) {
        encoder->Control(VP8E_SET_ENABLEAUTOALTREF, 1);
        encoder->Control(VP8E_SET_ARNR_MAXFRAMES, 7);
        encoder->Control(VP8E_SET_ARNR_STRENGTH, 5);
        encoder->Control(VP8E_SET_ARNR_TYPE, 3);
      }
    }
  }

  virtual void FramePktHook(const vpx_codec_cx_pkt_t *pkt) {
    if (pkt->data.frame.flags & VPX_FRAME_IS_KEY) {
    }
  }

  virtual void PSNRPktHook(const vpx_codec_cx_pkt_t *pkt) {
    if (pkt->data.psnr.psnr[0] < psnr_)
      psnr_ = pkt->data.psnr.psnr[0];
  }

  double GetMinPsnr() const { return psnr_; }

  ::libvpx_test::TestMode encoding_mode_;
  int set_cpu_used_;
  double psnr_;
};

TEST_P(CpuSpeedTest, TestQ0) {
  // Validate that this non multiple of 64 wide clip encodes and decodes
  // without a mismatch when passing in a very low max q.  This pushes
  // the encoder to producing lots of big partitions which will likely
  // extend into the border and test the border condition.
  cfg_.rc_2pass_vbr_minsection_pct = 5;
  cfg_.rc_2pass_vbr_minsection_pct = 2000;
  cfg_.rc_target_bitrate = 400;
  cfg_.rc_max_quantizer = 0;
  cfg_.rc_min_quantizer = 0;

  ::libvpx_test::I420VideoSource video("hantro_odd.yuv", 208, 144, 30, 1, 0,
                                       20);

  init_flags_ = VPX_CODEC_USE_PSNR;

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  const double psnr_lossless = GetMinPsnr();
  EXPECT_GE(psnr_lossless, kMaxPsnr);
}

TEST_P(CpuSpeedTest, TestScreencastQ0) {
  ::libvpx_test::Y4mVideoSource video("screendata.y4m", 0, 25);
  cfg_.g_timebase = video.timebase();
  cfg_.rc_2pass_vbr_minsection_pct = 5;
  cfg_.rc_2pass_vbr_minsection_pct = 2000;
  cfg_.rc_target_bitrate = 400;
  cfg_.rc_max_quantizer = 0;
  cfg_.rc_min_quantizer = 0;

  init_flags_ = VPX_CODEC_USE_PSNR;

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
  const double psnr_lossless = GetMinPsnr();
  EXPECT_GE(psnr_lossless, kMaxPsnr);
}

TEST_P(CpuSpeedTest, TestEncodeHighBitrate) {
  // Validate that this non multiple of 64 wide clip encodes and decodes
  // without a mismatch when passing in a very low max q.  This pushes
  // the encoder to producing lots of big partitions which will likely
  // extend into the border and test the border condition.
  cfg_.rc_2pass_vbr_minsection_pct = 5;
  cfg_.rc_2pass_vbr_minsection_pct = 2000;
  cfg_.rc_target_bitrate = 12000;
  cfg_.rc_max_quantizer = 10;
  cfg_.rc_min_quantizer = 0;

  ::libvpx_test::I420VideoSource video("hantro_odd.yuv", 208, 144, 30, 1, 0,
                                       20);

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}
TEST_P(CpuSpeedTest, TestLowBitrate) {
  // Validate that this clip encodes and decodes without a mismatch
  // when passing in a very high min q.  This pushes the encoder to producing
  // lots of small partitions which might will test the other condition.

  cfg_.rc_2pass_vbr_minsection_pct = 5;
  cfg_.rc_2pass_vbr_minsection_pct = 2000;
  cfg_.rc_target_bitrate = 200;
  cfg_.rc_min_quantizer = 40;

  ::libvpx_test::I420VideoSource video("hantro_odd.yuv", 208, 144, 30, 1, 0,
                                       20);

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

using std::tr1::make_tuple;

#define VP9_FACTORY \
  static_cast<const libvpx_test::CodecFactory*> (&libvpx_test::kVP9)

VP9_INSTANTIATE_TEST_CASE(
    CpuSpeedTest,
    ::testing::Values(::libvpx_test::kTwoPassGood, ::libvpx_test::kOnePassGood,
                      ::libvpx_test::kRealTime),
    ::testing::Range(0, 8));
}  // namespace
