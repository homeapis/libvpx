/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef TEST_CODEC_FACTORY_H_
#define TEST_CODEC_FACTORY_H_

extern "C" {
#include "./vpx_config.h"
#include "vpx/vpx_decoder.h"
#include "vpx/vpx_encoder.h"
#if CONFIG_VP8_ENCODER || CONFIG_VP9_ENCODER
#include "vpx/vp8cx.h"
#endif
#if CONFIG_VP8_DECODER || CONFIG_VP9_DECODER
#include "vpx/vp8dx.h"
#endif
}

#include "test/decode_test_driver.h"
#include "test/encode_test_driver.h"
namespace libvpx_test {

class CodecFactory {
 public:
  CodecFactory() {}

  virtual Decoder* CreateDecoder(vpx_codec_dec_cfg_t cfg,
                                 unsigned long deadline) const = 0;

  virtual Encoder* CreateEncoder(vpx_codec_enc_cfg_t cfg,
                                 unsigned long deadline,
                                 const unsigned long init_flags,
                                 TwopassStatsStore *stats) const = 0;

  virtual vpx_codec_err_t DefaultEncoderConfig(vpx_codec_enc_cfg_t *cfg,
                                               int usage) const = 0;

  virtual ~CodecFactory() {}
};

/* Provide CodecTestWith<n>Params classes for a variable number of parameters
 * to avoid having to include a pointer to the CodecFactory in every test
 * definition.
 */
class unused {};

template<class T1>
class CodecTestWithParam : public ::testing::TestWithParam<
    std::tr1::tuple< const libvpx_test::CodecFactory*, T1 > > {
};

template<class T1, class T2>
class CodecTestWith2Params : public ::testing::TestWithParam<
    std::tr1::tuple< const libvpx_test::CodecFactory*, T1, T2 > > {
};

template<class T1, class T2, class T3>
class CodecTestWith3Params : public ::testing::TestWithParam<
    std::tr1::tuple< const libvpx_test::CodecFactory*, T1, T2, T3 > > {
};

/*
 * VP8 Codec Definitions
 */
#if CONFIG_VP8
class VP8Decoder : public Decoder {
 public:
  VP8Decoder(vpx_codec_dec_cfg_t cfg, unsigned long deadline) :
      Decoder(cfg, deadline) {}

 protected:
  virtual const vpx_codec_iface_t* CodecInterface() const {
#if CONFIG_VP8_DECODER
    return &vpx_codec_vp8_dx_algo;
#else
    return NULL;
#endif
  }
};

class VP8Encoder : public Encoder {
 public:
  VP8Encoder(vpx_codec_enc_cfg_t cfg, unsigned long deadline,
             const unsigned long init_flags, TwopassStatsStore *stats) :
      Encoder(cfg, deadline, init_flags, stats) {}

 protected:
  virtual const vpx_codec_iface_t* CodecInterface() const {
#if CONFIG_VP8_ENCODER
    return &vpx_codec_vp8_cx_algo;
#else
    return NULL;
#endif
  }
};

class VP8CodecFactory : public CodecFactory {
 public:
  VP8CodecFactory() : CodecFactory() {}

  virtual Decoder* CreateDecoder(vpx_codec_dec_cfg_t cfg,
                                 unsigned long deadline) const {
#if CONFIG_VP8_DECODER
    return new VP8Decoder(cfg, deadline);
#else
    return NULL;
#endif
  }

#if CONFIG_VP8_ENCODER
  virtual Encoder* CreateEncoder(vpx_codec_enc_cfg_t cfg,
                                 unsigned long deadline,
                                 const unsigned long init_flags,
                                 TwopassStatsStore *stats) const {
    return new VP8Encoder(cfg, deadline, init_flags, stats);
  }

  virtual vpx_codec_err_t DefaultEncoderConfig(vpx_codec_enc_cfg_t *cfg,
                                               int usage) const {
    return vpx_codec_enc_config_default(&vpx_codec_vp8_cx_algo, cfg, usage);
  }
#else
  virtual Encoder* CreateEncoder(vpx_codec_enc_cfg_t cfg,
                                 unsigned long deadline,
                                 const unsigned long init_flags,
                                 TwopassStatsStore *stats) const {
    return NULL;
  }

  virtual vpx_codec_err_t DefaultEncoderConfig(vpx_codec_enc_cfg_t *cfg,
                                               int usage) const {
    return VPX_CODEC_INCAPABLE;
  }
#endif
};

static const libvpx_test::VP8CodecFactory kVP8;

#define VP8_INSTANTIATE_TEST_CASE(test, params)\
  INSTANTIATE_TEST_CASE_P(VP8, test, \
    ::testing::Combine(::testing::Values(\
      static_cast<const libvpx_test::CodecFactory*>(&libvpx_test::kVP8)), \
                       params))
#else
#define VP8_INSTANTIATE_TEST_CASE(test, params)
#endif


/*
 * VP9 Codec Definitions
 */
#if CONFIG_VP9
class VP9Decoder : public Decoder {
 public:
  VP9Decoder(vpx_codec_dec_cfg_t cfg, unsigned long deadline) :
      Decoder(cfg, deadline) {}

 protected:
  virtual const vpx_codec_iface_t* CodecInterface() const {
#if CONFIG_VP9_DECODER
    return &vpx_codec_vp9_dx_algo;
#else
    return NULL;
#endif
  }
};

class VP9Encoder : public Encoder {
 public:
  VP9Encoder(vpx_codec_enc_cfg_t cfg, unsigned long deadline,
             const unsigned long init_flags, TwopassStatsStore *stats) :
      Encoder(cfg, deadline, init_flags, stats) {}

 protected:
  virtual const vpx_codec_iface_t* CodecInterface() const {
#if CONFIG_VP9_ENCODER
    return &vpx_codec_vp9_cx_algo;
#else
    return NULL;
#endif
  }
};

class VP9CodecFactory : public CodecFactory {
 public:
  VP9CodecFactory() : CodecFactory() {}

  virtual Decoder* CreateDecoder(vpx_codec_dec_cfg_t cfg,
                                 unsigned long deadline) const {
#if CONFIG_VP9_DECODER
    return new VP9Decoder(cfg, deadline);
#else
    return NULL;
#endif
  }

#if CONFIG_VP9_ENCODER
  virtual Encoder* CreateEncoder(vpx_codec_enc_cfg_t cfg,
                                 unsigned long deadline,
                                 const unsigned long init_flags,
                                 TwopassStatsStore *stats) const {
    return new VP9Encoder(cfg, deadline, init_flags, stats);
  }

  virtual vpx_codec_err_t DefaultEncoderConfig(vpx_codec_enc_cfg_t *cfg,
                                               int usage) const {
    return vpx_codec_enc_config_default(&vpx_codec_vp9_cx_algo, cfg, usage);
  }
#else
  virtual Encoder* CreateEncoder(vpx_codec_enc_cfg_t cfg,
                                 unsigned long deadline,
                                 const unsigned long init_flags,
                                 TwopassStatsStore *stats) const {
    return NULL;
  }

  virtual vpx_codec_err_t DefaultEncoderConfig(vpx_codec_enc_cfg_t *cfg,
                                               int usage) const {
    return VPX_CODEC_INVALID_PARAM;
  }
#endif
};

static const libvpx_test::VP9CodecFactory kVP9;

#define VP9_INSTANTIATE_TEST_CASE(test, params)\
  INSTANTIATE_TEST_CASE_P(VP9, test, \
    ::testing::Combine(::testing::Values(\
      static_cast<const libvpx_test::CodecFactory*>(&libvpx_test::kVP9)), \
                       params))
#else
#define VP9_INSTANTIATE_TEST_CASE(test, params)
#endif


}  // namespace libvpx_test

#endif  // TEST_CODEC_FACTORY_H_
