#include "vpx_util/vpx_write_yuv_frame.h"

#if defined(OUTPUT_YUV_SRC) || defined(OUTPUT_YUV_DENOISED) || \
    defined(OUTPUT_YUV_SKINMAP)
void vpx_write_yuv_frame(FILE *yuv_file, YV12_BUFFER_CONFIG *s) {
  unsigned char *src = s->y_buffer;
  int h = s->y_crop_height;

  do {
    fwrite(src, s->y_width, 1, yuv_file);
    src += s->y_stride;
  } while (--h);

  src = s->u_buffer;
  h = s->uv_crop_height;

  do {
    fwrite(src, s->uv_width, 1, yuv_file);
    src += s->uv_stride;
  } while (--h);

  src = s->v_buffer;
  h = s->uv_crop_height;

  do {
    fwrite(src, s->uv_width, 1, yuv_file);
    src += s->uv_stride;
  } while (--h);
}
#endif