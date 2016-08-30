/*
 *  Copyright (c) 2015 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AOM_PORTS_MSVC_H_
#define AOM_PORTS_MSVC_H_
#ifdef _MSC_VER

#include "./aom_config.h"

#if _MSC_VER < 1900  // VS2015 provides snprintf
#define snprintf _snprintf
#endif  // _MSC_VER < 1900

#if _MSC_VER < 1800  // VS2013 provides round
#include <math.h>
static INLINE double round(double x) {
  if (x < 0)
    return ceil(x - 0.5);
  else
    return floor(x + 0.5);
}

static INLINE float roundf(float x) {
  if (x < 0)
    return (float)ceil(x - 0.5f);
  else
    return (float)floor(x + 0.5f);
}

static INLINE long lroundf(float x) {
  if (x < 0)
    return (long)(x - 0.5f);
  else
    return (long)(x + 0.5f);
}
#endif  // _MSC_VER < 1800

#endif  // _MSC_VER
#endif  // AOM_PORTS_MSVC_H_
