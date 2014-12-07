/*
 *  Copyright (c) 2013 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/codec_factory.h"
#include "test/decode_test_driver.h"
#include "test/encode_test_driver.h"
#include "test/ivf_video_source.h"
#include "test/i420_video_source.h"
#include "test/md5_helper.h"
#include "test/util.h"
#include "test/webm_video_source.h"
#include "vpx_ports/vpx_timer.h"
#include "./vpx_version.h"

using std::tr1::make_tuple;

namespace {

#define VIDEO_NAME 0
#define THREADS 1

const int kMaxPsnr = 100;
const double kUsecsInSec = 1000000.0;

/*
 DecodePerfTest takes a tuple of filename + number of threads to decode with
 */
typedef std::tr1::tuple<const char *, unsigned> DecodePerfParam;

const DecodePerfParam kVP9DecodePerfVectors[] = {
  make_tuple("vp90-2-bbb_426x240_tile_1x1_180kbps.webm", 1),
  make_tuple("vp90-2-bbb_640x360_tile_1x2_337kbps.webm", 2),
  make_tuple("vp90-2-bbb_854x480_tile_1x2_651kbps.webm", 2),
  make_tuple("vp90-2-bbb_1280x720_tile_1x4_1310kbps.webm", 4),
  make_tuple("vp90-2-bbb_1920x1080_tile_1x1_2581kbps.webm", 1),
  make_tuple("vp90-2-bbb_1920x1080_tile_1x4_2586kbps.webm", 4),
  make_tuple("vp90-2-bbb_1920x1080_tile_1x4_fpm_2304kbps.webm", 4),
  make_tuple("vp90-2-sintel_426x182_tile_1x1_171kbps.webm", 1),
  make_tuple("vp90-2-sintel_640x272_tile_1x2_318kbps.webm", 2),
  make_tuple("vp90-2-sintel_854x364_tile_1x2_621kbps.webm", 2),
  make_tuple("vp90-2-sintel_1280x546_tile_1x4_1257kbps.webm", 4),
  make_tuple("vp90-2-sintel_1920x818_tile_1x4_fpm_2279kbps.webm", 4),
  make_tuple("vp90-2-tos_426x178_tile_1x1_181kbps.webm", 1),
  make_tuple("vp90-2-tos_640x266_tile_1x2_336kbps.webm", 2),
  make_tuple("vp90-2-tos_854x356_tile_1x2_656kbps.webm", 2),
  make_tuple("vp90-2-tos_854x356_tile_1x2_fpm_546kbps.webm", 2),
  make_tuple("vp90-2-tos_1280x534_tile_1x4_1306kbps.webm", 4),
  make_tuple("vp90-2-tos_1280x534_tile_1x4_fpm_952kbps.webm", 4),
  make_tuple("vp90-2-tos_1920x800_tile_1x4_fpm_2335kbps.webm", 4),
};

/*
 In order to reflect real world performance as much as possible, Perf tests
 *DO NOT* do any correctness checks. Please run them alongside correctness
 tests to ensure proper codec integrity. Furthermore, in this test we
 deliberately limit the amount of system calls we make to avoid OS
 preemption.

 TODO(joshualitt) create a more detailed perf measurement test to collect
   power/temp/min max frame decode times/etc
 */

class DecodePerfTest : public ::testing::TestWithParam<DecodePerfParam> {
};

TEST_P(DecodePerfTest, PerfTest) {
  const char *const video_name = GET_PARAM(VIDEO_NAME);
  const unsigned threads = GET_PARAM(THREADS);

  libvpx_test::WebMVideoSource video(video_name);
  video.Init();

  vpx_codec_dec_cfg_t cfg = vpx_codec_dec_cfg_t();
  cfg.threads = threads;
  libvpx_test::VP9Decoder decoder(cfg, 0);

  vpx_usec_timer t;
  vpx_usec_timer_start(&t);

  for (video.Begin(); video.cxdata() != NULL; video.Next()) {
    decoder.DecodeFrame(video.cxdata(), video.frame_size());
  }

  vpx_usec_timer_mark(&t);
  const double elapsed_secs = double(vpx_usec_timer_elapsed(&t))
                              / kUsecsInSec;
  const unsigned frames = video.frame_number();
  const double fps = double(frames) / elapsed_secs;

  printf("{\n");
  printf("\t\"type\" : \"decode_perf_test\",\n");
  printf("\t\"version\" : \"%s\",\n", VERSION_STRING_NOSP);
  printf("\t\"videoName\" : \"%s\",\n", video_name);
  printf("\t\"threadCount\" : %u,\n", threads);
  printf("\t\"decodeTimeSecs\" : %f,\n", elapsed_secs);
  printf("\t\"totalFrames\" : %u,\n", frames);
  printf("\t\"framesPerSecond\" : %f\n", fps);
  printf("}\n");
}

INSTANTIATE_TEST_CASE_P(VP9, DecodePerfTest,
                        ::testing::ValuesIn(kVP9DecodePerfVectors));

static void mem_put_le16(char *const mem, const unsigned int val) {
  mem[0] = val;
  mem[1] = val >> 8;
}

static void mem_put_le32(char *const mem, const unsigned int val) {
  mem[0] = val;
  mem[1] = val >> 8;
  mem[2] = val >> 16;
  mem[3] = val >> 24;
}

static void write_ivf_file_header(const vpx_codec_enc_cfg_t *const cfg,
                                  int frame_cnt, FILE *const outfile) {
  char header[32];

  header[0] = 'D';
  header[1] = 'K';
  header[2] = 'I';
  header[3] = 'F';
  mem_put_le16(header + 4,  0);                   /* version */
  mem_put_le16(header + 6,  32);                  /* headersize */
  mem_put_le32(header + 8,  0x30395056);          /* fourcc (vp9) */
  mem_put_le16(header + 12, cfg->g_w);            /* width */
  mem_put_le16(header + 14, cfg->g_h);            /* height */
  mem_put_le32(header + 16, cfg->g_timebase.den); /* rate */
  mem_put_le32(header + 20, cfg->g_timebase.num); /* scale */
  mem_put_le32(header + 24, frame_cnt);           /* length */
  mem_put_le32(header + 28, 0);                   /* unused */

  (void)fwrite(header, 1, 32, outfile);
}

static void write_ivf_frame_size(FILE *const outfile, const size_t size) {
  char header[4];
  mem_put_le32(header, static_cast<unsigned int>(size));
  (void)fwrite(header, 1, 4, outfile);
}

static void write_ivf_frame_header(const vpx_codec_cx_pkt_t *const pkt,
                                   FILE *const outfile) {
  char header[12];
  vpx_codec_pts_t pts;

  if (pkt->kind != VPX_CODEC_CX_FRAME_PKT)
    return;

  pts = pkt->data.frame.pts;
  mem_put_le32(header, static_cast<unsigned int>(pkt->data.frame.sz));
  mem_put_le32(header + 4, pts & 0xFFFFFFFF);
  mem_put_le32(header + 8, pts >> 32);

  (void)fwrite(header, 1, 12, outfile);
}

class VP9NewEncodeDecodePerfTest
    : public ::libvpx_test::EncoderTest,
      public ::libvpx_test::CodecTestWithParam<libvpx_test::TestMode> {
 protected:
	VP9NewEncodeDecodePerfTest()
      : EncoderTest(GET_PARAM(0)),
        min_psnr_(kMaxPsnr),
        nframes_(0),
        encoding_mode_(GET_PARAM(1)),
        speed_(0) {}

  virtual ~VP9NewEncodeDecodePerfTest() {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(encoding_mode_);

    cfg_.g_lag_in_frames = 0;
    cfg_.rc_min_quantizer = 2;
    cfg_.rc_max_quantizer = 56;
    cfg_.rc_dropframe_thresh = 0;
    cfg_.rc_undershoot_pct = 50;
    cfg_.rc_overshoot_pct = 50;
    cfg_.rc_buf_sz = 1000;
    cfg_.rc_buf_initial_sz = 500;
    cfg_.rc_buf_optimal_sz = 600;
    cfg_.rc_resize_allowed = 0;
    cfg_.rc_end_usage = VPX_VBR;
  }

  virtual void PreEncodeFrameHook(::libvpx_test::VideoSource *video,
                                  ::libvpx_test::Encoder *encoder) {
    if (video->frame() == 1) {
      encoder->Control(VP8E_SET_CPUUSED, speed_);
      encoder->Control(VP9E_SET_FRAME_PARALLEL_DECODING, 1);
      encoder->Control(VP9E_SET_TILE_COLUMNS, 1);
    }
  }
  virtual void BeginPassHook(unsigned int /*pass*/) {
    outfile_ = fopen("niklas_new.ivf", "wb");
  }

  virtual void EndPassHook() {
    if (outfile_) {
      if (!fseek(outfile_, 0, SEEK_SET))
        write_ivf_file_header(&cfg_, out_frames_, outfile_);
      fclose(outfile_);
      outfile_ = NULL;
    }
  }

  virtual void FramePktHook(const vpx_codec_cx_pkt_t *pkt) {
    ++out_frames_;

    // Write initial file header if first frame.
    if (pkt->data.frame.pts == 0)
      write_ivf_file_header(&cfg_, 0, outfile_);

    // Write frame header and data.
    write_ivf_frame_header(pkt, outfile_);
    (void)fwrite(pkt->data.frame.buf, 1, pkt->data.frame.sz, outfile_);

    printf("%d\n",pkt->data.frame.sz);
  }

  virtual bool DoDecode() { return 0; }

  void set_speed(unsigned int speed) {
    speed_ = speed;
  }

 private:
  double min_psnr_;
  unsigned int nframes_;
  libvpx_test::TestMode encoding_mode_;
  unsigned speed_;
  FILE *outfile_;
  unsigned int out_frames_;
};


struct EncodePerfTestVideo {
  EncodePerfTestVideo(const char *name_, uint32_t width_, uint32_t height_,
                      uint32_t bitrate_, int frames_)
      : name(name_),
        width(width_),
        height(height_),
        bitrate(bitrate_),
        frames(frames_) {}
  const char *name;
  uint32_t width;
  uint32_t height;
  uint32_t bitrate;
  int frames;
};

const EncodePerfTestVideo kVP9EncodePerfTestVectors[] = {
  EncodePerfTestVideo("niklas_1280_720_30.yuv", 1280, 720, 600, 470),
};

TEST_P(VP9NewEncodeDecodePerfTest, PerfTest) {
  SetUp();

  int i = 0;
  const vpx_rational timebase = { 33333333, 1000000000 };
  cfg_.g_timebase = timebase;
  cfg_.rc_target_bitrate = kVP9EncodePerfTestVectors[i].bitrate;

  init_flags_ = VPX_CODEC_USE_PSNR;

  const char *video_name = kVP9EncodePerfTestVectors[i].name;
  libvpx_test::I420VideoSource video(
	  video_name,
	  kVP9EncodePerfTestVectors[i].width,
	  kVP9EncodePerfTestVectors[i].height,
	  timebase.den, timebase.num, 0,
	  kVP9EncodePerfTestVectors[i].frames);
  set_speed(2);

  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));

  const char *const decode_name = "niklas_new.ivf";
  const unsigned threads = 4;

  libvpx_test::IVFVideoSource decode_video(decode_name);
  decode_video.Init();

  vpx_codec_dec_cfg_t cfg = vpx_codec_dec_cfg_t();
  cfg.threads = threads;
  libvpx_test::VP9Decoder decoder(cfg, 0);

  vpx_usec_timer t;
  vpx_usec_timer_start(&t);

  for (decode_video.Begin(); decode_video.cxdata() != NULL; decode_video.Next()) {
    decoder.DecodeFrame(decode_video.cxdata(), decode_video.frame_size());
  }

  vpx_usec_timer_mark(&t);
  const double elapsed_secs = double(vpx_usec_timer_elapsed(&t))
                / kUsecsInSec;
  const unsigned decode_frames = decode_video.frame_number();
  const double fps = double(decode_frames) / elapsed_secs;

  printf("{\n");
  printf("\t\"type\" : \"decode_perf_test\",\n");
  printf("\t\"version\" : \"%s\",\n", VERSION_STRING_NOSP);
  printf("\t\"videoName\" : \"%s\",\n", decode_name);
  printf("\t\"threadCount\" : %u,\n", threads);
  printf("\t\"decodeTimeSecs\" : %f,\n", elapsed_secs);
  printf("\t\"totalFrames\" : %u,\n", decode_frames);
  printf("\t\"framesPerSecond\" : %f\n", fps);
  printf("}\n");

}

VP9_INSTANTIATE_TEST_CASE(
  VP9NewEncodeDecodePerfTest, ::testing::Values(::libvpx_test::kTwoPassGood));
}  // namespace
