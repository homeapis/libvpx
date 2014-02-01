/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "./ivfdec.h"
#include "./video_reader.h"

#include <stdlib.h>
#include <string.h>

static const char *IVF_SIGNATURE = "DKIF";

struct VpxVideoReaderStruct {
  FILE *file;
  unsigned char *buffer;
  size_t buffer_size;
  size_t frame_size;
  unsigned int codec_fourcc;
  int width;
  int height;
};

VpxVideoReader *vpx_video_reader_open(FILE *file) {
  char raw_hdr[32];
  VpxVideoReader *reader;

  if (fread(raw_hdr, 1, 32, file) != 32)
    return NULL;  // Can't read file header;

  if (memcmp(IVF_SIGNATURE, raw_hdr, 4) != 0)
    return NULL;  // Wrong IVF signature

  if (mem_get_le16(raw_hdr + 4) != 0)
    return NULL;  // Wrong IVF version

  reader = malloc(sizeof(*reader));
  reader->file = file;
  reader->buffer = NULL;
  reader->buffer_size = 0;
  reader->frame_size = 0;
  reader->codec_fourcc = mem_get_le32(raw_hdr + 8);
  reader->width = mem_get_le16(raw_hdr + 12);
  reader->height = mem_get_le16(raw_hdr + 14);
  return reader;
}

void vpx_video_reader_close(VpxVideoReader *reader) {
  if (reader) {
    fclose(reader->file);
    free(reader->buffer);
    free(reader);
  }
}

int vpx_video_reader_read_frame(VpxVideoReader *reader) {
  return !ivf_read_frame(reader->file, &reader->buffer, &reader->frame_size,
                         &reader->buffer_size);
}

const unsigned char *vpx_video_reader_get_frame(VpxVideoReader *reader,
                                                size_t *size) {
  if (size)
    *size = reader->frame_size;

  return reader->buffer;
}

void vpx_video_reader_get_info(VpxVideoReader *reader, VpxVideoInfo *info) {
  info->frame_width = reader->width;
  info->frame_height = reader->height;
  info->codec_fourcc = reader->codec_fourcc;
}
