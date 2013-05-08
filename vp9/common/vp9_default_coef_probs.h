/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
*/


/*Generated file, included by vp9_entropy.c*/

#if CONFIG_BALANCED_COEFTREE
static const vp9_coeff_probs default_coef_probs_4x4[BLOCK_TYPES] = {
  { /* block Type 0 */
    { /* Intra */
      { /* Coeff Band 0 */
        {   6,  29, 241 },
        {  25,  73, 159 },
        {  33,  63,  22 }
      }, { /* Coeff Band 1 */
        {  65, 111, 169 },
        {  63, 109, 166 },
        {  66, 121, 102 },
        {  62, 112,  49 },
        {  47,  81,  12 },
        {  19,  31,   1 }
      }, { /* Coeff Band 2 */
        {  63,  90, 222 },
        {  70, 116, 182 },
        {  69, 130,  72 },
        {  55, 102,  14 },
        {  34,  59,   1 },
        {  13,  21,   1 }
      }, { /* Coeff Band 3 */
        {  81,  88, 235 },
        {  82, 114, 204 },
        {  80, 144,  70 },
        {  61, 114,  11 },
        {  41,  73,   1 },
        {  17,  29,   1 }
      }, { /* Coeff Band 4 */
        {  51,  69, 246 },
        {  63, 103, 221 },
        {  72, 145,  84 },
        {  58, 112,  13 },
        {  37,  64,   1 },
        {  14,  25,   1 }
      }, { /* Coeff Band 5 */
        {  29,  75, 247 },
        {  26,  75, 240 },
        {  40, 125, 156 },
        {  39, 111,  56 },
        {  26,  66,  13 },
        {  10,  27,   2 }
      }
    }, { /* Inter */
      { /* Coeff Band 0 */
        {  21,  27, 253 },
        {  52,  61, 239 },
        {  79,  99, 183 }
      }, { /* Coeff Band 1 */
        { 110,  87, 248 },
        {  87,  93, 240 },
        {  89, 154, 109 },
        {  70, 125,  24 },
        {  43,  75,   3 },
        {  18,  32,   1 }
      }, { /* Coeff Band 2 */
        {  95,  67, 252 },
        {  98, 116, 232 },
        {  87, 162,  58 },
        {  63, 119,   9 },
        {  37,  68,   1 },
        {  17,  29,   1 }
      }, { /* Coeff Band 3 */
        {  84,  48, 254 },
        {  93, 101, 242 },
        {  95, 162,  91 },
        {  71, 126,  16 },
        {  46,  82,   1 },
        {  25,  43,   1 }
      }, { /* Coeff Band 4 */
        {  37,  34, 255 },
        {  57,  74, 249 },
        {  77, 154, 115 },
        {  59, 117,  16 },
        {  33,  65,   1 },
        {  17,  33,   1 }
      }, { /* Coeff Band 5 */
        {  20,  39, 255 },
        {  20,  48, 253 },
        {  42, 126, 185 },
        {  43, 122,  78 },
        {  36,  88,  19 },
        {  30,  54,   1 }
      }
    }
  }, { /* block Type 1 */
    { /* Intra */
      { /* Coeff Band 0 */
        {   6,  37, 248 },
        {  23,  83, 205 },
        {  37,  95,  95 }
      }, { /* Coeff Band 1 */
        {  68,  86, 234 },
        {  51,  64, 238 },
        {  69, 107, 191 },
        {  69, 120, 108 },
        {  58,  95,  33 },
        {  39,  58,   9 }
      }, { /* Coeff Band 2 */
        {  68,  59, 246 },
        {  69,  84, 236 },
        {  83, 141, 143 },
        {  73, 130,  37 },
        {  48,  86,   5 },
        {  22,  40,   1 }
      }, { /* Coeff Band 3 */
        {  99,  57, 249 },
        {  91, 102, 231 },
        {  91, 150, 127 },
        {  73, 131,  40 },
        {  48,  87,   9 },
        {  21,  40,   1 }
      }, { /* Coeff Band 4 */
        {  52,  50, 252 },
        {  59,  79, 243 },
        {  74, 143, 140 },
        {  61, 122,  35 },
        {  37,  71,   4 },
        {  16,  38,   1 }
      }, { /* Coeff Band 5 */
        {  33,  61, 253 },
        {  30,  64, 247 },
        {  45, 123, 164 },
        {  38, 113,  63 },
        {  28,  75,  20 },
        {  12,  37,   6 }
      }
    }, { /* Inter */
      { /* Coeff Band 0 */
        {   4,   8, 255 },
        {  27,  42, 249 },
        {  64,  94, 216 }
      }, { /* Coeff Band 1 */
        {  68,  47, 254 },
        {  53,  45, 252 },
        {  81, 130, 188 },
        {  70, 120, 101 },
        {  58,  91,  62 },
        {  58,  61,  45 }
      }, { /* Coeff Band 2 */
        {  64,  39, 254 },
        {  72,  66, 249 },
        {  91, 158, 131 },
        {  74, 138,  30 },
        {  48,  98,   4 },
        {  25,  52,   1 }
      }, { /* Coeff Band 3 */
        {  70,  34, 255 },
        {  75,  65, 250 },
        {  90, 147, 153 },
        {  75, 135,  54 },
        {  57, 106,  19 },
        {  34,  47,   7 }
      }, { /* Coeff Band 4 */
        {  33,  31, 255 },
        {  43,  50, 253 },
        {  68, 136, 181 },
        {  63, 132,  63 },
        {  43,  77,   3 },
        {   1, 127,   1 }
      }, { /* Coeff Band 5 */
        {  24,  39, 255 },
        {  21,  39, 254 },
        {  46, 124, 208 },
        {  47, 130,  97 },
        {  26,  82,  29 },
        {  64,  42, 204 }
      }
    }
  }
};
static const vp9_coeff_probs default_coef_probs_8x8[BLOCK_TYPES] = {
  { /* block Type 0 */
    { /* Intra */
      { /* Coeff Band 0 */
        {   9,  40, 242 },
        {  25,  81, 135 },
        {  27,  52,  14 }
      }, { /* Coeff Band 1 */
        {  99, 126, 106 },
        {  78,  94, 157 },
        {  80, 114,  79 },
        {  70, 107,  28 },
        {  51,  78,   7 },
        {  26,  39,   1 }
      }, { /* Coeff Band 2 */
        { 135, 130, 164 },
        { 112, 138, 132 },
        {  93, 143,  35 },
        {  70, 113,   7 },
        {  40,  67,   1 },
        {  15,  26,   1 }
      }, { /* Coeff Band 3 */
        { 154, 127, 206 },
        { 128, 161, 130 },
        {  94, 153,  23 },
        {  62, 109,   2 },
        {  31,  57,   1 },
        {  11,  20,   1 }
      }, { /* Coeff Band 4 */
        { 163,  96, 239 },
        { 136, 155, 175 },
        {  95, 156,  28 },
        {  61, 106,   4 },
        {  31,  56,   1 },
        {  12,  21,   1 }
      }, { /* Coeff Band 5 */
        { 135,  62, 250 },
        { 122, 134, 214 },
        {  97, 157,  50 },
        {  66, 109,   7 },
        {  37,  59,   1 },
        {  17,  25,   1 }
      }
    }, { /* Inter */
      { /* Coeff Band 0 */
        {  24,  28, 254 },
        {  54,  64, 237 },
        {  80, 107, 128 }
      }, { /* Coeff Band 1 */
        { 145,  95, 243 },
        { 100,  80, 238 },
        { 108, 150, 121 },
        {  89, 132,  44 },
        {  66,  93,  12 },
        {  42,  49,   4 }
      }, { /* Coeff Band 2 */
        { 149,  89, 245 },
        { 127, 141, 200 },
        {  93, 156,  24 },
        {  63, 108,   4 },
        {  33,  60,   1 },
        {  12,  23,   1 }
      }, { /* Coeff Band 3 */
        { 151,  89, 248 },
        { 134, 152, 198 },
        {  92, 159,  25 },
        {  60, 109,   4 },
        {  30,  58,   1 },
        {  11,  21,   1 }
      }, { /* Coeff Band 4 */
        { 161,  81, 250 },
        { 139, 153, 208 },
        {  99, 168,  23 },
        {  63, 115,   2 },
        {  33,  62,   1 },
        {  13,  25,   1 }
      }, { /* Coeff Band 5 */
        { 137,  56, 253 },
        { 127, 133, 228 },
        {  99, 167,  47 },
        {  66, 117,   5 },
        {  36,  65,   1 },
        {  15,  29,   1 }
      }
    }
  }, { /* block Type 1 */
    { /* Intra */
      { /* Coeff Band 0 */
        {   5,  33, 248 },
        {  24,  81, 196 },
        {  43,  95,  76 }
      }, { /* Coeff Band 1 */
        {  93, 117, 206 },
        {  65,  78, 228 },
        {  82, 127, 156 },
        {  74, 131,  58 },
        {  46,  80,   5 },
        {  17,  27,   1 }
      }, { /* Coeff Band 2 */
        { 107,  92, 232 },
        {  90, 106, 222 },
        {  91, 156, 104 },
        {  72, 129,  19 },
        {  45,  78,   1 },
        {  19,  34,   1 }
      }, { /* Coeff Band 3 */
        { 124,  74, 245 },
        { 109, 121, 221 },
        {  97, 161,  82 },
        {  70, 123,  13 },
        {  43,  75,   1 },
        {  20,  35,   1 }
      }, { /* Coeff Band 4 */
        { 132,  55, 249 },
        { 112, 117, 224 },
        {  97, 162,  61 },
        {  69, 120,   5 },
        {  41,  71,   1 },
        {  18,  33,   1 }
      }, { /* Coeff Band 5 */
        { 132,  70, 248 },
        { 118, 134, 216 },
        { 101, 166,  51 },
        {  71, 122,   5 },
        {  41,  69,   1 },
        {  16,  27,   1 }
      }
    }, { /* Inter */
      { /* Coeff Band 0 */
        {   4,  13, 255 },
        {  25,  51, 247 },
        {  58, 103, 165 }
      }, { /* Coeff Band 1 */
        {  81,  52, 253 },
        {  50,  34, 252 },
        {  94, 118, 200 },
        {  88, 127, 121 },
        {  76, 114,  57 },
        {  57,  85,  21 }
      }, { /* Coeff Band 2 */
        {  99,  51, 253 },
        {  96,  83, 244 },
        {  99, 166,  89 },
        {  72, 126,  16 },
        {  40,  71,   1 },
        {  13,  23,   1 }
      }, { /* Coeff Band 3 */
        { 107,  50, 253 },
        { 106,  91, 243 },
        { 103, 166,  76 },
        {  71, 121,   7 },
        {  38,  65,   1 },
        {  13,  18,   1 }
      }, { /* Coeff Band 4 */
        { 119,  44, 254 },
        { 118,  98, 241 },
        { 107, 168,  55 },
        {  72, 124,   5 },
        {  39,  68,   1 },
        {  15,  23,   1 }
      }, { /* Coeff Band 5 */
        { 127,  54, 252 },
        { 122, 119, 232 },
        { 111, 171,  59 },
        {  82, 134,   6 },
        {  54,  87,   1 },
        {   9,  37,   1 }
      }
    }
  }
};
static const vp9_coeff_probs default_coef_probs_16x16[BLOCK_TYPES] = {
  { /* block Type 0 */
    { /* Intra */
      { /* Coeff Band 0 */
        {  25,  97,  14 },
        {  24,  66,   2 },
        {  14,  27,   1 }
      }, { /* Coeff Band 1 */
        {  66, 104,  50 },
        {  60,  90,  87 },
        {  59,  92,  48 },
        {  52,  81,  16 },
        {  33,  53,   2 },
        {  13,  21,   1 }
      }, { /* Coeff Band 2 */
        { 106, 115, 105 },
        {  91, 116,  97 },
        {  77, 116,  26 },
        {  55,  86,   3 },
        {  34,  55,   1 },
        {  16,  26,   1 }
      }, { /* Coeff Band 3 */
        { 142, 130, 123 },
        { 111, 142,  68 },
        {  80, 125,   8 },
        {  52,  86,   1 },
        {  30,  51,   1 },
        {  13,  23,   1 }
      }, { /* Coeff Band 4 */
        { 171, 143, 164 },
        { 128, 163,  78 },
        {  86, 138,   6 },
        {  53,  89,   1 },
        {  27,  48,   1 },
        {  11,  20,   1 }
      }, { /* Coeff Band 5 */
        { 192, 112, 230 },
        { 147, 177,  92 },
        {  89, 143,   3 },
        {  52,  89,   1 },
        {  27,  49,   1 },
        {  12,  21,   1 }
      }
    }, { /* Inter */
      { /* Coeff Band 0 */
        {  59, 228,  59 },
        {  64, 181,  13 },
        {  55, 103,   2 }
      }, { /* Coeff Band 1 */
        { 150, 118, 207 },
        {  86,  56, 236 },
        { 101, 121, 153 },
        {  90, 123,  65 },
        {  66,  90,  16 },
        {  37,  50,   2 }
      }, { /* Coeff Band 2 */
        { 153,  99, 227 },
        { 120, 140, 158 },
        {  90, 140,  24 },
        {  62,  99,   4 },
        {  38,  60,   1 },
        {  19,  32,   1 }
      }, { /* Coeff Band 3 */
        { 170, 108, 228 },
        { 131, 156, 143 },
        {  91, 143,  14 },
        {  57,  94,   2 },
        {  31,  51,   1 },
        {  13,  22,   1 }
      }, { /* Coeff Band 4 */
        { 183, 105, 233 },
        { 138, 165, 136 },
        {  91, 146,   7 },
        {  54,  90,   1 },
        {  27,  46,   1 },
        {  12,  20,   1 }
      }, { /* Coeff Band 5 */
        { 197,  97, 241 },
        { 152, 181, 111 },
        {  94, 147,   3 },
        {  54,  89,   1 },
        {  29,  50,   1 },
        {  15,  24,   1 }
      }
    }
  }, { /* block Type 1 */
    { /* Intra */
      { /* Coeff Band 0 */
        {   6,  39, 247 },
        {  30,  88, 161 },
        {  39,  78,  30 }
      }, { /* Coeff Band 1 */
        { 100, 133, 147 },
        {  74,  86, 197 },
        {  87, 126,  99 },
        {  77, 121,  29 },
        {  50,  81,   3 },
        {  17,  28,   1 }
      }, { /* Coeff Band 2 */
        { 116, 109, 201 },
        { 101, 125, 172 },
        {  93, 146,  51 },
        {  73, 119,  10 },
        {  46,  76,   1 },
        {  17,  29,   1 }
      }, { /* Coeff Band 3 */
        { 156, 108, 214 },
        { 130, 159, 140 },
        { 101, 158,  26 },
        {  72, 119,   2 },
        {  41,  68,   1 },
        {  15,  26,   1 }
      }, { /* Coeff Band 4 */
        { 173,  96, 234 },
        { 133, 156, 172 },
        {  99, 161,  26 },
        {  65, 110,   2 },
        {  35,  59,   1 },
        {  15,  26,   1 }
      }, { /* Coeff Band 5 */
        { 184,  82, 243 },
        { 146, 166, 161 },
        { 104, 161,  13 },
        {  65, 107,   2 },
        {  34,  58,   1 },
        {  15,  26,   1 }
      }
    }, { /* Inter */
      { /* Coeff Band 0 */
        {   2,  23, 254 },
        {  18,  76, 231 },
        {  37,  97, 104 }
      }, { /* Coeff Band 1 */
        {  83,  64, 249 },
        {  35,  22, 253 },
        {  81,  86, 226 },
        {  87, 112, 172 },
        {  81, 113, 115 },
        {  67,  91,  92 }
      }, { /* Coeff Band 2 */
        { 106,  65, 249 },
        {  99,  93, 234 },
        { 106, 160,  91 },
        {  83, 134,  24 },
        {  49,  90,   5 },
        {  20,  39,   1 }
      }, { /* Coeff Band 3 */
        { 125,  68, 251 },
        { 121, 114, 230 },
        { 113, 166,  72 },
        {  81, 130,  12 },
        {  47,  81,   1 },
        {  25,  37,   1 }
      }, { /* Coeff Band 4 */
        { 142,  67, 251 },
        { 133, 127, 228 },
        { 114, 173,  44 },
        {  77, 124,   5 },
        {  42,  67,   1 },
        {  33,  39,   1 }
      }, { /* Coeff Band 5 */
        { 170,  58, 251 },
        { 150, 155, 203 },
        { 117, 173,  28 },
        {  80, 129,   2 },
        {  50,  77,   1 },
        {  17,  48,   1 }
      }
    }
  }
};
static const vp9_coeff_probs default_coef_probs_32x32[BLOCK_TYPES] = {
  { /* block Type 0 */
    { /* Intra */
      { /* Coeff Band 0 */
        {  29, 114,  75 },
        {  25,  59,   4 },
        {  12,  22,   1 }
      }, { /* Coeff Band 1 */
        {  69, 105,  60 },
        {  63,  95,  91 },
        {  60,  97,  52 },
        {  52,  86,  17 },
        {  31,  51,   2 },
        {   9,  14,   1 }
      }, { /* Coeff Band 2 */
        { 106, 112,  97 },
        {  86, 104, 116 },
        {  73, 105,  40 },
        {  52,  76,   7 },
        {  28,  43,   1 },
        {  11,  17,   1 }
      }, { /* Coeff Band 3 */
        { 136, 115, 129 },
        { 106, 131,  70 },
        {  72, 111,   9 },
        {  43,  68,   1 },
        {  24,  39,   1 },
        {  11,  17,   1 }
      }, { /* Coeff Band 4 */
        { 165, 126, 138 },
        { 117, 144,  55 },
        {  74, 115,   5 },
        {  44,  72,   1 },
        {  25,  42,   1 },
        {  11,  18,   1 }
      }, { /* Coeff Band 5 */
        { 220, 141, 208 },
        { 152, 179,  33 },
        {  85, 130,   3 },
        {  48,  78,   1 },
        {  25,  42,   1 },
        {  11,  19,   1 }
      }
    }, { /* Inter */
      { /* Coeff Band 0 */
        {  29, 175, 184 },
        {  34, 165,  29 },
        {  23, 101,  38 }
      }, { /* Coeff Band 1 */
        { 116,  94, 230 },
        {  51,  29, 249 },
        {  75,  66, 228 },
        {  88, 103, 160 },
        {  76,  97,  69 },
        {  52,  55,  22 }
      }, { /* Coeff Band 2 */
        { 127,  70, 242 },
        { 107, 112, 202 },
        {  92, 140,  41 },
        {  65, 101,   6 },
        {  37,  60,   1 },
        {  18,  29,   1 }
      }, { /* Coeff Band 3 */
        { 145,  64, 245 },
        { 121, 128, 191 },
        {  92, 142,  24 },
        {  61,  97,   2 },
        {  34,  56,   1 },
        {  16,  25,   1 }
      }, { /* Coeff Band 4 */
        { 165,  57, 247 },
        { 132, 141, 179 },
        {  92, 143,  14 },
        {  57,  93,   2 },
        {  31,  51,   1 },
        {  14,  23,   1 }
      }, { /* Coeff Band 5 */
        { 204,  29, 252 },
        { 156, 165, 137 },
        {  97, 143,   7 },
        {  58,  91,   2 },
        {  32,  51,   1 },
        {  14,  23,   1 }
      }
    }
  }, { /* block Type 1 */
    { /* Intra */
      { /* Coeff Band 0 */
        {   6,  56, 235 },
        {  25,  83,  98 },
        {  30,  56,  11 }
      }, { /* Coeff Band 1 */
        {  92, 131,  87 },
        {  65,  83, 179 },
        {  77, 114, 100 },
        {  70, 109,  38 },
        {  47,  74,   5 },
        {  17,  25,   1 }
      }, { /* Coeff Band 2 */
        { 112, 121, 153 },
        {  95, 118, 139 },
        {  84, 123,  46 },
        {  63,  96,  12 },
        {  36,  57,   1 },
        {  12,  18,   1 }
      }, { /* Coeff Band 3 */
        { 146, 129, 171 },
        { 119, 153,  88 },
        {  88, 134,  16 },
        {  58,  92,   2 },
        {  30,  48,   1 },
        {   9,  17,   1 }
      }, { /* Coeff Band 4 */
        { 168, 121, 205 },
        { 130, 160, 109 },
        {  91, 141,  10 },
        {  55,  87,   1 },
        {  28,  46,   1 },
        {  13,  21,   1 }
      }, { /* Coeff Band 5 */
        { 210, 130, 216 },
        { 148, 181,  48 },
        {  88, 137,   3 },
        {  51,  83,   1 },
        {  28,  46,   1 },
        {  13,  22,   1 }
      }
    }, { /* Inter */
      { /* Coeff Band 0 */
        {   2,  46, 253 },
        {  11, 128, 204 },
        {  17, 125,  65 }
      }, { /* Coeff Band 1 */
        {  84,  49, 253 },
        {  18,   7, 255 },
        {  58,  31, 251 },
        {  91,  74, 234 },
        {  88,  99, 191 },
        {  70,  93, 149 }
      }, { /* Coeff Band 2 */
        { 114,  59, 251 },
        { 103,  87, 234 },
        {  99, 157,  85 },
        {  77, 126,  19 },
        {  42,  74,   3 },
        {  14,  47,   1 }
      }, { /* Coeff Band 3 */
        { 117,  55, 252 },
        { 116, 100, 232 },
        { 110, 161,  66 },
        {  83, 131,   9 },
        {  56,  83,   1 },
        {   1,   1,   1 }
      }, { /* Coeff Band 4 */
        { 143,  53, 253 },
        { 135, 115, 236 },
        { 124, 179,  38 },
        {  82, 129,   2 },
        {  60,  65,   1 },
        {  64,  42, 204 }
      }, { /* Coeff Band 5 */
        { 173,  28, 255 },
        { 164, 130, 240 },
        { 131, 181,  70 },
        {  84, 132,   6 },
        {  69,  68,   1 },
        {   1,   1,   1 }
      }
    }
  }
};
#else
static const vp9_coeff_probs_model default_coef_probs_4x4[BLOCK_TYPES] = {
  { /* block Type 0 */
    { /* Intra */
      { /* Coeff Band 0 */
        { 208,  32, 178,},
        { 102,  43, 132,},
        {  15,  36,  68,}
      }, { /* Coeff Band 1 */
        {  71,  91, 178,},
        {  72,  88, 174,},
        {  40,  79, 154,},
        {  21,  68, 126,},
        {   7,  49,  84,},
        {   1,  20,  32,}
      }, { /* Coeff Band 2 */
        { 108, 110, 206,},
        {  72,  98, 191,},
        {  26,  77, 152,},
        {   7,  57, 106,},
        {   1,  35,  60,},
        {   1,  14,  22,}
      }, { /* Coeff Band 3 */
        { 105, 139, 222,},
        {  76, 118, 205,},
        {  21,  88, 164,},
        {   5,  63, 118,},
        {   1,  42,  74,},
        {   1,  18,  30,}
      }, { /* Coeff Band 4 */
        { 143, 117, 233,},
        {  99, 104, 214,},
        {  26,  81, 170,},
        {   6,  60, 116,},
        {   1,  38,  65,},
        {   1,  15,  26,}
      }, { /* Coeff Band 5 */
        { 155,  74, 238,},
        { 152,  64, 223,},
        {  67,  55, 182,},
        {  27,  44, 127,},
        {   9,  27,  69,},
        {   2,  11,  28,}
      }
    }, { /* Inter */
      { /* Coeff Band 0 */
        { 207, 112, 234,},
        { 145, 120, 212,},
        {  77, 114, 177,}
      }, { /* Coeff Band 1 */
        {  93, 174, 243,},
        { 100, 144, 231,},
        {  28, 101, 186,},
        {   9,  73, 132,},
        {   2,  44,  76,},
        {   1,  19,  33,}
      }, { /* Coeff Band 2 */
        { 116, 175, 246,},
        {  78, 142, 231,},
        {  14,  93, 177,},
        {   4,  65, 122,},
        {   1,  38,  69,},
        {   1,  18,  30,}
      }, { /* Coeff Band 3 */
        { 138, 183, 249,},
        {  93, 147, 237,},
        {  21, 104, 187,},
        {   6,  73, 131,},
        {   1,  47,  83,},
        {   1,  26,  44,}
      }, { /* Coeff Band 4 */
        { 188, 143, 252,},
        { 137, 124, 241,},
        {  32,  89, 188,},
        {   7,  61, 122,},
        {   1,  34,  66,},
        {   1,  18,  34,}
      }, { /* Coeff Band 5 */
        { 198,  92, 253,},
        { 189,  79, 244,},
        {  78,  61, 200,},
        {  34,  50, 146,},
        {  11,  38,  93,},
        {   1,  31,  55,}
      }
    }
  }, { /* block Type 1 */
    { /* Intra */
      { /* Coeff Band 0 */
        { 207,  35, 219,},
        { 126,  46, 182,},
        {  51,  47, 125,}
      }, { /* Coeff Band 1 */
        { 114, 124, 220,},
        { 142, 116, 213,},
        {  81, 101, 190,},
        {  42,  83, 155,},
        {  16,  62, 104,},
        {   6,  40,  60,}
      }, { /* Coeff Band 2 */
        { 139, 149, 228,},
        { 115, 127, 221,},
        {  43, 100, 189,},
        {  13,  77, 141,},
        {   3,  49,  88,},
        {   1,  23,  41,}
      }, { /* Coeff Band 3 */
        { 119, 185, 236,},
        {  89, 140, 224,},
        {  34, 105, 189,},
        {  14,  78, 142,},
        {   5,  49,  90,},
        {   1,  22,  41,}
      }, { /* Coeff Band 4 */
        { 162, 142, 244,},
        { 129, 120, 231,},
        {  44,  90, 189,},
        {  14,  65, 132,},
        {   3,  38,  72,},
        {   1,  17,  39,}
      }, { /* Coeff Band 5 */
        { 167,  96, 247,},
        { 163,  84, 234,},
        {  70,  63, 185,},
        {  30,  44, 132,},
        {  13,  30,  80,},
        {   5,  13,  38,}
      }
    }, { /* Inter */
      { /* Coeff Band 0 */
        { 242,  90, 246,},
        { 186, 102, 228,},
        { 102, 108, 203,}
      }, { /* Coeff Band 1 */
        { 152, 169, 250,},
        { 164, 149, 242,},
        {  63, 108, 204,},
        {  39,  83, 153,},
        {  31,  66, 108,},
        {  27,  65,  71,}
      }, { /* Coeff Band 2 */
        { 161, 174, 250,},
        { 133, 150, 239,},
        {  32, 105, 197,},
        {  10,  78, 147,},
        {   2,  49,  99,},
        {   1,  26,  53,}
      }, { /* Coeff Band 3 */
        { 160, 187, 251,},
        { 131, 155, 241,},
        {  42, 108, 198,},
        {  18,  81, 151,},
        {   9,  60, 112,},
        {   5,  35,  49,}
      }, { /* Coeff Band 4 */
        { 195, 141, 253,},
        { 169, 128, 245,},
        {  62,  91, 204,},
        {  23,  70, 150,},
        {   2,  44,  78,},
        {   1,   1, 128,}
      }, { /* Coeff Band 5 */
        { 195, 104, 253,},
        { 197,  92, 248,},
        {  88,  71, 214,},
        {  39,  56, 160,},
        {  18,  28,  90,},
        { 128, 128, 128,}
      }
    }
  }
};
static const vp9_coeff_probs_model default_coef_probs_8x8[BLOCK_TYPES] = {
  { /* block Type 0 */
    { /* Intra */
      { /* Coeff Band 0 */
        { 196,  40, 199,},
        {  83,  38, 128,},
        {  10,  29,  55,}
      }, { /* Coeff Band 1 */
        {  33, 114, 160,},
        {  69, 107, 155,},
        {  30,  91, 138,},
        {  12,  74, 115,},
        {   4,  52,  80,},
        {   1,  27,  40,}
      }, { /* Coeff Band 2 */
        {  38, 159, 190,},
        {  34, 130, 182,},
        {  10,  97, 153,},
        {   3,  71, 115,},
        {   1,  41,  68,},
        {   1,  16,  27,}
      }, { /* Coeff Band 3 */
        {  41, 184, 214,},
        {  24, 142, 199,},
        {   6,  97, 159,},
        {   1,  63, 110,},
        {   1,  32,  58,},
        {   1,  12,  21,}
      }, { /* Coeff Band 4 */
        {  54, 207, 231,},
        {  32, 156, 213,},
        {   7,  98, 164,},
        {   2,  62, 108,},
        {   1,  32,  57,},
        {   1,  13,  22,}
      }, { /* Coeff Band 5 */
        {  89, 208, 239,},
        {  53, 155, 223,},
        {  12, 102, 170,},
        {   3,  67, 111,},
        {   1,  38,  60,},
        {   1,  18,  26,}
      }
    }, { /* Inter */
      { /* Coeff Band 0 */
        { 205, 121, 244,},
        { 140, 120, 211,},
        {  51, 100, 152,}
      }, { /* Coeff Band 1 */
        {  66, 196, 236,},
        {  99, 164, 223,},
        {  29, 122, 187,},
        {  14,  95, 145,},
        {   6,  68,  97,},
        {   3,  43,  50,}
      }, { /* Coeff Band 2 */
        {  66, 202, 238,},
        {  45, 155, 218,},
        {   6,  96, 163,},
        {   2,  64, 110,},
        {   1,  34,  61,},
        {   1,  13,  24,}
      }, { /* Coeff Band 3 */
        {  66, 204, 242,},
        {  38, 158, 222,},
        {   6,  95, 166,},
        {   2,  61, 111,},
        {   1,  31,  59,},
        {   1,  12,  22,}
      }, { /* Coeff Band 4 */
        {  63, 214, 245,},
        {  38, 164, 228,},
        {   5, 101, 174,},
        {   1,  64, 116,},
        {   1,  34,  63,},
        {   1,  14,  26,}
      }, { /* Coeff Band 5 */
        {  91, 214, 246,},
        {  55, 162, 233,},
        {  10, 104, 179,},
        {   2,  67, 119,},
        {   1,  37,  66,},
        {   1,  16,  30,}
      }
    }
  }, { /* block Type 1 */
    { /* Intra */
      { /* Coeff Band 0 */
        { 211,  32, 212,},
        { 121,  47, 171,},
        {  40,  51, 118,}
      }, { /* Coeff Band 1 */
        {  71, 129, 209,},
        { 118, 122, 206,},
        {  53, 104, 184,},
        {  20,  81, 148,},
        {   3,  47,  82,},
        {   1,  18,  28,}
      }, { /* Coeff Band 2 */
        {  86, 162, 220,},
        {  84, 134, 216,},
        {  26, 102, 186,},
        {   7,  75, 135,},
        {   1,  46,  79,},
        {   1,  20,  35,}
      }, { /* Coeff Band 3 */
        {  89, 191, 232,},
        {  67, 148, 223,},
        {  19, 105, 183,},
        {   5,  72, 127,},
        {   1,  44,  76,},
        {   1,  21,  36,}
      }, { /* Coeff Band 4 */
        {  94, 210, 236,},
        {  68, 153, 224,},
        {  14, 103, 178,},
        {   2,  70, 122,},
        {   1,  42,  72,},
        {   1,  19,  34,}
      }, { /* Coeff Band 5 */
        {  87, 200, 238,},
        {  55, 151, 225,},
        {  11, 106, 179,},
        {   2,  72, 124,},
        {   1,  42,  70,},
        {   1,  17,  28,}
      }
    }, { /* Inter */
      { /* Coeff Band 0 */
        { 238,  66, 250,},
        { 178,  84, 226,},
        {  76,  83, 168,}
      }, { /* Coeff Band 1 */
        { 137, 176, 246,},
        { 176, 160, 237,},
        {  68, 128, 205,},
        {  40, 105, 167,},
        {  22,  84, 131,},
        {  11,  60,  91,}
      }, { /* Coeff Band 2 */
        { 124, 192, 247,},
        { 103, 161, 234,},
        {  19, 108, 190,},
        {   6,  74, 131,},
        {   1,  41,  72,},
        {   1,  14,  24,}
      }, { /* Coeff Band 3 */
        { 118, 200, 248,},
        {  91, 166, 235,},
        {  16, 110, 186,},
        {   3,  72, 124,},
        {   1,  39,  66,},
        {   1,  14,  19,}
      }, { /* Coeff Band 4 */
        { 112, 213, 248,},
        {  80, 172, 234,},
        {  11, 112, 182,},
        {   2,  73, 126,},
        {   1,  40,  69,},
        {   1,  16,  24,}
      }, { /* Coeff Band 5 */
        { 100, 209, 245,},
        {  65, 164, 232,},
        {  11, 117, 186,},
        {   2,  83, 136,},
        {   1,  55,  88,},
        {   1,  10,  38,}
      }
    }
  }
};
static const vp9_coeff_probs_model default_coef_probs_16x16[BLOCK_TYPES] = {
  { /* block Type 0 */
    { /* Intra */
      { /* Coeff Band 0 */
        {   8,  26, 101,},
        {   2,  25,  67,},
        {   1,  15,  28,}
      }, { /* Coeff Band 1 */
        {  22,  73, 118,},
        {  43,  73, 116,},
        {  24,  66, 105,},
        {   9,  54,  85,},
        {   2,  34,  54,},
        {   1,  14,  22,}
      }, { /* Coeff Band 2 */
        {  34, 123, 149,},
        {  34, 106, 147,},
        {  10,  81, 123,},
        {   2,  56,  87,},
        {   1,  35,  56,},
        {   1,  17,  27,}
      }, { /* Coeff Band 3 */
        {  27, 159, 171,},
        {  17, 119, 162,},
        {   3,  81, 128,},
        {   1,  53,  87,},
        {   1,  31,  52,},
        {   1,  14,  24,}
      }, { /* Coeff Band 4 */
        {  24, 189, 200,},
        {  14, 136, 184,},
        {   2,  87, 140,},
        {   1,  54,  90,},
        {   1,  28,  49,},
        {   1,  12,  21,}
      }, { /* Coeff Band 5 */
        {  32, 220, 227,},
        {  12, 155, 200,},
        {   1,  90, 144,},
        {   1,  53,  90,},
        {   1,  28,  50,},
        {   1,  13,  22,}
      }
    }, { /* Inter */
      { /* Coeff Band 0 */
        {   5,  61, 234,},
        {   3,  65, 184,},
        {   1,  56, 104,}
      }, { /* Coeff Band 1 */
        {  46, 183, 210,},
        { 122, 166, 202,},
        {  49, 125, 177,},
        {  22,  99, 142,},
        {   8,  69,  95,},
        {   2,  38,  51,}
      }, { /* Coeff Band 2 */
        {  56, 196, 218,},
        {  38, 141, 195,},
        {   7,  93, 147,},
        {   2,  63, 101,},
        {   1,  39,  61,},
        {   1,  20,  33,}
      }, { /* Coeff Band 3 */
        {  44, 206, 223,},
        {  27, 147, 200,},
        {   4,  93, 147,},
        {   1,  58,  95,},
        {   1,  32,  52,},
        {   1,  14,  23,}
      }, { /* Coeff Band 4 */
        {  39, 216, 227,},
        {  22, 152, 204,},
        {   2,  92, 148,},
        {   1,  55,  91,},
        {   1,  28,  47,},
        {   1,  13,  21,}
      }, { /* Coeff Band 5 */
        {  34, 228, 234,},
        {  13, 161, 208,},
        {   1,  95, 148,},
        {   1,  55,  90,},
        {   1,  30,  51,},
        {   1,  16,  25,}
      }
    }
  }, { /* block Type 1 */
    { /* Intra */
      { /* Coeff Band 0 */
        { 204,  33, 217,},
        {  93,  48, 151,},
        {  18,  43,  86,}
      }, { /* Coeff Band 1 */
        {  43, 121, 184,},
        {  93, 117, 177,},
        {  33, 101, 158,},
        {  11,  81, 129,},
        {   2,  51,  82,},
        {   1,  18,  29,}
      }, { /* Coeff Band 2 */
        {  63, 154, 199,},
        {  53, 128, 191,},
        {  14,  99, 160,},
        {   4,  75, 122,},
        {   1,  47,  77,},
        {   1,  18,  30,}
      }, { /* Coeff Band 3 */
        {  48, 193, 210,},
        {  26, 145, 201,},
        {   6, 104, 165,},
        {   1,  73, 120,},
        {   1,  42,  69,},
        {   1,  16,  27,}
      }, { /* Coeff Band 4 */
        {  47, 213, 225,},
        {  32, 153, 212,},
        {   6, 102, 168,},
        {   1,  66, 111,},
        {   1,  36,  60,},
        {   1,  16,  27,}
      }, { /* Coeff Band 5 */
        {  46, 225, 232,},
        {  24, 162, 214,},
        {   3, 106, 165,},
        {   1,  66, 108,},
        {   1,  35,  59,},
        {   1,  16,  27,}
      }
    }, { /* Inter */
      { /* Coeff Band 0 */
        { 229,  28, 245,},
        { 151,  44, 210,},
        {  55,  48, 131,}
      }, { /* Coeff Band 1 */
        { 126, 165, 239,},
        { 199, 158, 231,},
        { 102, 136, 209,},
        {  64, 116, 181,},
        {  44,  98, 151,},
        {  44,  81, 119,}
      }, { /* Coeff Band 2 */
        { 108, 185, 239,},
        {  91, 155, 224,},
        {  20, 116, 185,},
        {   8,  86, 141,},
        {   3,  50,  92,},
        {   1,  21,  40,}
      }, { /* Coeff Band 3 */
        {  94, 198, 243,},
        {  67, 164, 228,},
        {  14, 120, 185,},
        {   4,  83, 134,},
        {   1,  48,  82,},
        {   1,  26,  38,}
      }, { /* Coeff Band 4 */
        {  82, 210, 245,},
        {  55, 170, 231,},
        {   8, 118, 184,},
        {   2,  78, 126,},
        {   1,  43,  68,},
        {   1,  34,  40,}
      }, { /* Coeff Band 5 */
        {  65, 228, 241,},
        {  33, 173, 226,},
        {   5, 120, 180,},
        {   1,  81, 130,},
        {   1,  51,  78,},
        {   1,  18,  49,}
      }
    }
  }
};
static const vp9_coeff_probs_model default_coef_probs_32x32[BLOCK_TYPES] = {
  { /* block Type 0 */
    { /* Intra */
      { /* Coeff Band 0 */
        {  37,  34, 137,},
        {   3,  26,  60,},
        {   1,  13,  23,}
      }, { /* Coeff Band 1 */
        {  26,  77, 122,},
        {  43,  76, 123,},
        {  25,  67, 112,},
        {   9,  54,  90,},
        {   2,  32,  52,},
        {   1,  10,  15,}
      }, { /* Coeff Band 2 */
        {  32, 122, 143,},
        {  46, 105, 143,},
        {  17,  79, 116,},
        {   4,  53,  78,},
        {   1,  29,  44,},
        {   1,  12,  18,}
      }, { /* Coeff Band 3 */
        {  33, 157, 160,},
        {  20, 116, 152,},
        {   4,  74, 114,},
        {   1,  44,  69,},
        {   1,  25,  40,},
        {   1,  12,  18,}
      }, { /* Coeff Band 4 */
        {  25, 183, 174,},
        {  13, 124, 159,},
        {   2,  75, 117,},
        {   1,  45,  73,},
        {   1,  26,  43,},
        {   1,  12,  19,}
      }, { /* Coeff Band 5 */
        {  13, 232, 223,},
        {   4, 155, 187,},
        {   1,  86, 131,},
        {   1,  49,  79,},
        {   1,  26,  43,},
        {   1,  12,  20,}
      }
    }, { /* Inter */
      { /* Coeff Band 0 */
        {  51,  37, 227,},
        {   9,  36, 172,},
        {  21,  26, 112,}
      }, { /* Coeff Band 1 */
        {  79, 169, 219,},
        { 177, 166, 216,},
        { 119, 141, 196,},
        {  63, 117, 165,},
        {  30,  87, 117,},
        {  14,  56,  60,}
      }, { /* Coeff Band 2 */
        {  88, 195, 225,},
        {  66, 145, 202,},
        {  12,  97, 152,},
        {   3,  66, 103,},
        {   1,  38,  61,},
        {   1,  19,  30,}
      }, { /* Coeff Band 3 */
        {  79, 211, 228,},
        {  50, 151, 205,},
        {   7,  95, 149,},
        {   1,  62,  98,},
        {   1,  35,  57,},
        {   1,  17,  26,}
      }, { /* Coeff Band 4 */
        {  68, 225, 230,},
        {  39, 156, 206,},
        {   4,  94, 147,},
        {   1,  58,  94,},
        {   1,  32,  52,},
        {   1,  15,  24,}
      }, { /* Coeff Band 5 */
        {  45, 248, 234,},
        {  19, 169, 204,},
        {   2,  98, 145,},
        {   1,  59,  92,},
        {   1,  33,  52,},
        {   1,  15,  24,}
      }
    }
  }, { /* block Type 1 */
    { /* Intra */
      { /* Coeff Band 0 */
        { 179,  23, 200,},
        {  60,  33, 113,},
        {   8,  31,  59,}
      }, { /* Coeff Band 1 */
        {  27, 103, 158,},
        {  90, 101, 159,},
        {  39,  91, 146,},
        {  16,  75, 120,},
        {   3,  48,  76,},
        {   1,  18,  26,}
      }, { /* Coeff Band 2 */
        {  45, 137, 177,},
        {  47, 117, 167,},
        {  16,  90, 136,},
        {   6,  65, 100,},
        {   1,  37,  58,},
        {   1,  13,  19,}
      }, { /* Coeff Band 3 */
        {  36, 171, 194,},
        {  19, 129, 178,},
        {   5,  90, 139,},
        {   1,  59,  93,},
        {   1,  31,  49,},
        {   1,  10,  18,}
      }, { /* Coeff Band 4 */
        {  37, 197, 210,},
        {  20, 142, 191,},
        {   3,  93, 144,},
        {   1,  56,  88,},
        {   1,  29,  47,},
        {   1,  14,  22,}
      }, { /* Coeff Band 5 */
        {  19, 227, 223,},
        {   6, 152, 192,},
        {   1,  89, 138,},
        {   1,  52,  84,},
        {   1,  29,  47,},
        {   1,  14,  23,}
      }
    }, { /* Inter */
      { /* Coeff Band 0 */
        { 205,  14, 245,},
        {  97,  19, 213,},
        {  31,  20, 144,}
      }, { /* Coeff Band 1 */
        { 137, 182, 245,},
        { 231, 185, 242,},
        { 170, 175, 229,},
        { 107, 157, 213,},
        {  77, 126, 183,},
        {  69,  96, 149,}
      }, { /* Coeff Band 2 */
        { 107, 196, 241,},
        {  92, 162, 221,},
        {  20, 108, 181,},
        {   7,  80, 132,},
        {   2,  43,  75,},
        {   1,  15,  48,}
      }, { /* Coeff Band 3 */
        { 107, 202, 244,},
        {  77, 167, 224,},
        {  14, 117, 179,},
        {   3,  84, 134,},
        {   1,  57,  84,},
        {   1,   1,   1,}
      }, { /* Coeff Band 4 */
        {  88, 219, 248,},
        {  61, 178, 234,},
        {   6, 127, 188,},
        {   1,  83, 130,},
        {   1,  61,  66,},
        { 128, 128, 128,}
      }, { /* Coeff Band 5 */
        {  73, 243, 250,},
        {  42, 197, 242,},
        {  10, 137, 197,},
        {   2,  85, 134,},
        {   1,  70,  69,},
        {   1,   1,   1,}
      }
    }
  }
};
#endif
