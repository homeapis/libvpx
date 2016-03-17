#include "vp10/common/x86/vp10_txfm1d_sse2.h"

static inline void int16_array_with_stride_to_int32_array_without_stride(const int16_t *input, int stride, int32_t *output, int txfm1d_size) {
  int r, c;
  for(r = 0; r < txfm1d_size; r++) {
    for(c = 0; c < txfm1d_size; c++) {
      output[r*txfm1d_size + c] = (int32_t)input[r*stride + c];
    }
  }
}

typedef void (*TxfmFuncSSE2)(const M128I *input, M128I *output,
                        const int8_t *cos_bit, const int8_t *stage_range);

static inline TxfmFuncSSE2 fwd_txfm_type_to_func(TXFM_TYPE txfm_type) {
  switch(txfm_type) {
    case TXFM_TYPE_DCT4:
      return vp10_fdct4_new_sse2;
      break;
    case TXFM_TYPE_DCT8:
      return vp10_fdct8_new_sse2;
      break;
    case TXFM_TYPE_DCT16:
      return vp10_fdct16_new_sse2;
      break;
    case TXFM_TYPE_DCT32:
      return vp10_fdct32_new_sse2;
      break;
    case TXFM_TYPE_DCT64:
      return vp10_fdct64_new_sse2;
      break;
    case TXFM_TYPE_ADST4:
      return vp10_fadst4_new_sse2;
      break;
    case TXFM_TYPE_ADST8:
      return vp10_fadst8_new_sse2;
      break;
    case TXFM_TYPE_ADST16:
      return vp10_fadst16_new_sse2;
      break;
    case TXFM_TYPE_ADST32:
      return vp10_fadst32_new_sse2;
      break;
    default:
      assert(0);
  }
  return NULL;
}

static inline void fwd_txfm2d_sse2(const int16_t *input, int32_t *output,
                                   const int stride, const TXFM_2D_CFG *cfg,
                                   int32_t *txfm_buf) {
  const int txfm_size = cfg->txfm_size;
  const int8_t *shift = cfg->shift;
  const int8_t *stage_range_col = cfg->stage_range_col;
  const int8_t *stage_range_row = cfg->stage_range_row;
  const int8_t *cos_bit_col = cfg->cos_bit_col;
  const int8_t *cos_bit_row = cfg->cos_bit_row;
  const TxfmFuncSSE2 txfm_func_col = fwd_txfm_type_to_func(cfg->txfm_type_col);
  const TxfmFuncSSE2 txfm_func_row = fwd_txfm_type_to_func(cfg->txfm_type_row);

  M128I* buf_128 = (M128I*)txfm_buf;
  M128I* out_128 = (M128I*)output;
  int num_per_128 = 4;
  int txfm2d_size_128 = txfm_size*txfm_size/num_per_128;

  int16_array_with_stride_to_int32_array_without_stride(input, stride, txfm_buf, txfm_size);
  round_shift_array_32_sse2(buf_128, out_128, txfm2d_size_128, -shift[0]);
  txfm_func_col(out_128, buf_128, cos_bit_col, stage_range_col);
  round_shift_array_32_sse2(buf_128, out_128, txfm2d_size_128, -shift[1]);
  transpose_32(txfm_size, out_128, buf_128);
  txfm_func_row(buf_128, out_128, cos_bit_row, stage_range_row);
  round_shift_array_32_sse2(out_128, buf_128, txfm2d_size_128, -shift[2]);
  transpose_32(txfm_size, buf_128, out_128);
}


void vp10_fwd_txfm2d_4x4_sse2(const int16_t *input, int32_t *output,
                              const int stride, const TXFM_2D_CFG *cfg,
                              const int bd) {
  int32_t txfm_buf[16];
  (void)bd;
  fwd_txfm2d_sse2(input, output, stride, cfg, txfm_buf);
}

void vp10_fwd_txfm2d_8x8_sse2(const int16_t *input, int32_t *output,
                              const int stride, const TXFM_2D_CFG *cfg,
                              const int bd) {
  int32_t txfm_buf[64];
  (void)bd;
  fwd_txfm2d_sse2(input, output, stride, cfg, txfm_buf);
}

void vp10_fwd_txfm2d_16x16_sse2(const int16_t *input, int32_t *output,
                                const int stride, const TXFM_2D_CFG *cfg,
                                const int bd) {
  int32_t txfm_buf[256];
  (void)bd;
  fwd_txfm2d_sse2(input, output, stride, cfg, txfm_buf);
}

void vp10_fwd_txfm2d_32x32_sse2(const int16_t *input, int32_t *output,
                                const int stride, const TXFM_2D_CFG *cfg,
                                const int bd) {
  int32_t txfm_buf[1024];
  (void)bd;
  fwd_txfm2d_sse2(input, output, stride, cfg, txfm_buf);
}

void vp10_fwd_txfm2d_64x64_sse2(const int16_t *input, int32_t *output,
                                const int stride, const TXFM_2D_CFG *cfg,
                                const int bd) {
  int32_t txfm_buf[4096];
  (void)bd;
  fwd_txfm2d_sse2(input, output, stride, cfg, txfm_buf);
}

