#include <stdio.h>

#include "vp9/common/vp9_palette.h"

#if CONFIG_PALETTE
int generate_palette(const uint8_t *src, int stride, int rows, int cols,
                     uint8_t *palette, int *count, uint8_t *map) {
  int i, r, c, n = 0;
  uint8_t val;
  int hash[256], val_count[256];
  memset(hash, 0, sizeof(hash));
  memset(val_count, 0, sizeof(val_count));

  for (r = 0; r < rows; r++) {
    for (c = 0; c < cols; c++) {
      val = src[r * stride + c];
      val_count[val]++;
    }
  }

  n = 0;
  for (i = 0; i < 256; i++) {
    if (val_count[i]) {
      if (n + 1 > PALETTE_MAX_SIZE)
        return 0;
      palette[n] = i;
      hash[i] = n;
      count[n] = val_count[i];
      n++;
    }
  }

  for (r = 0; r < rows; r++) {
    for (c = 0; c < cols; c++) {
      val = src[r * stride + c];
      map[r * cols + c] = hash[val];
    }
  }

  return n;
}

int nearest_number(int num, int denom, int l_bound, int r_bound) {
  int i, this_diff, x = l_bound, best_diff = abs(denom * l_bound - num);

  for (i = l_bound + 1; i <= r_bound; i++) {
    this_diff = abs(denom * i - num);
    if (this_diff < best_diff) {
      best_diff = this_diff;
      x = i;
    }
  }

  return x;
}

void reduce_palette(uint8_t *palette, int *count, int n, uint8_t *map,
                    int rows, int cols) {
  int i, r, c, best_idx;
  int best_num, best_denom, this_num, this_denom;

  if (0) {
    printf("input: \n");
    for (i = 0; i < n - 1; i++) {
      printf("%4d", palette[i]);
    }
    printf("\n");
  }

  best_idx = 0;
  best_num = 1;
  best_denom = 0;

  for (i = 0; i < n - 1; i++) {
    this_num = count[i] * count[i + 1] * (palette[i + 1] - palette[i]);
    this_denom = count[i] + count[i + 1];
    if (this_num * best_denom < this_denom * best_num) {
      best_idx = i;
      best_num = this_num;
      best_denom = this_denom;
    }
  }

  i = best_idx;
  palette[i] = nearest_number(count[i] * palette[i] +
                              count[i + 1] * palette[i + 1],
                              count[i] + count[i + 1],
                              palette[i], palette[i + 1]);
  count[i] = count[i] + count[i + 1];

  if (0) {
    printf("best idx is %4d, palette is %4d \n", i, palette[i]);
  }

  if (best_idx < n - 2) {
    for (i = best_idx + 1; i < n - 1; i++) {
      palette[i] = palette[i + 1];
      count[i] = count[i + 1];
    }
  }

  for (r = 0; r < rows; r++) {
    for (c = 0; c < cols; c++) {
      if (map[r * cols + c] > best_idx)
        map[r * cols + c]--;
    }
  }

  if (0) {
    printf("output: \n");
    for (i = 0; i < n - 1; i++) {
      printf("%4d", palette[i]);
    }
    printf("\n");

    scanf("%d", &i);
  }
}

int run_lengh_encoding(uint8_t *seq, int n, uint16_t *runs, int max_run) {
  int this_run, i, l = 0;
  uint8_t symbol;

  for (i = 0; i < n; ) {
    if ((l + 2) > (2 * max_run - 1))
      return 0;

    symbol = seq[i];
    runs[l++] = symbol;
    this_run = 1;
    i++;
    while (seq[i] == symbol && i < n) {
      i++;
      this_run++;
    }
    runs[l++] = this_run;
  }

  return l;
}

int run_lengh_decoding(uint16_t *runs, int l, uint8_t *seq) {
  int i, j = 0;

  for (i = 0; i < l; i += 2) {
    memset(seq + j, runs[i], runs[i + 1]);
    j += runs[i + 1];
  }

  return j;
}

void transpose_block(uint8_t *seq_in, uint8_t *seq_out, int rows, int cols) {
  int r, c;
  uint8_t seq_dup[4096];
  memcpy(seq_dup, seq_in, rows * cols);

  for (r = 0; r < cols; r++) {
    for (c = 0; c < rows; c++) {
      seq_out[r * rows + c] = seq_dup[c * cols + r];
    }
  }
}

void palette_color_insersion(uint8_t *old_colors, int *m, uint8_t *new_colors,
                             int n, int *count) {
  int k = *m;
  int i, j, l, idx, min_idx = -1;
  uint8_t val;

  if (n <= 0)
    return;

  i = 0;
  while (i < k) {
    count[i]--;
    i++;
  }

  for (i = 0; i < n; i++) {
    val = new_colors[i];
    j = 0;
    while (val > old_colors[j] && j < k)
      j++;

    if (j < k && val == old_colors[j]) {
      count[j] += 3;
      continue;
    }

    idx = j;
    k++;
    if (k > PALETTE_BUF_SIZE) {
      k--;
      min_idx = 0;
      for (l = 1; l < k; l++)
        if (count[l] < count[min_idx])
          min_idx = l;

      l = min_idx;
      while (l < k - 1) {
        old_colors[l] = old_colors[l + 1];
        count[l] = count[l + 1];
        l++;
      }
    }

    if (min_idx < 0 || idx <= min_idx)
      j = idx;
    else
      j = idx - 1;

    if (j == k) {
      old_colors[k] = val;
      count[k] = 3;
    } else {
      for (l = k; l > j; l--) {
        old_colors[l] = old_colors[l - 1];
        count[l] = count[l - 1];
      }

      old_colors[j] = val;
      count[j] = 3;
    }

    /*
    k++;
    if (k > PALETTE_BUF_SIZE) {
      min_idx = 0;
      for (j = 1; j < k; j++)
        if (count[j] < count[min_idx])
          min_idx = j;

      j = min_idx;
      while (j < k - 1) {
        old_colors[j] = old_colors[j + 1];
        count[j] = count[j + 1];
        j++;
      }

      k--;
    }
    */
  }

  *m = k;
}

void palette_color_insersion1(uint8_t *old_colors, int *m, int *count,
                              MB_MODE_INFO *mbmi) {
  int k = *m, n = mbmi->palette_literal_size;
  int i, j, l, idx, min_idx = -1;
  uint8_t *new_colors = mbmi->palette_literal_colors;
  uint8_t val;

  if (mbmi->palette_indexed_size > 0) {
    for (i = 0; i < mbmi->palette_indexed_size; i++)
      count[mbmi->palette_indexed_colors[i]] +=
          (8 - abs(mbmi->palette_color_delta[i]));
  }

  i = 0;
  while (i < k) {
    count[i] -= 1;
    i++;
  }

  if (n <= 0)
    return;

  for (i = 0; i < n; i++) {
    val = new_colors[i];
    j = 0;
    while (val > old_colors[j] && j < k)
      j++;

    if (j < k && val == old_colors[j]) {
      count[j] += 8;
      continue;
    }

    idx = j;
    k++;
    if (k > PALETTE_BUF_SIZE) {
      k--;
      min_idx = 0;
      for (l = 1; l < k; l++)
        if (count[l] < count[min_idx])
          min_idx = l;

      l = min_idx;
      while (l < k - 1) {
        old_colors[l] = old_colors[l + 1];
        count[l] = count[l + 1];
        l++;
      }
    }

    if (min_idx < 0 || idx <= min_idx)
      j = idx;
    else
      j = idx - 1;

    if (j == k) {
      old_colors[k] = val;
      count[k] = 8;
    } else {
      for (l = k; l > j; l--) {
        old_colors[l] = old_colors[l - 1];
        count[l] = count[l - 1];
      }

      old_colors[j] = val;
      count[j] = 8;
    }
  }

  *m = k;
}

int palette_color_lookup(uint8_t *dic, int n, uint8_t val, int bits) {
  int j, min, arg_min = 0, i = 1;


  if (n < 1)
    return -1;

  min = abs(val - dic[0]);
  arg_min = 0;
  while (i < n) {
    j = abs(val - dic[i]);
    if ( j < min) {
      min = j;
      arg_min = i;
    }
    i++;
  }

  if (min < (1 << bits))
    return arg_min;
  else
    return -1;
   /* */

/*
  i = 0;
  while (i < n) {
    if (dic[i] == val)
      return i;
    i++;
  }

  return -1;
  */
}

int get_bit_depth(int n) {
  int i = 1, p = 2;
  while (p < n) {
    i++;
    p = p << 1;
  }

  return i;
}

int palette_max_run(BLOCK_SIZE bsize) {
  int table[BLOCK_SIZES] = {
       8,  8,  8, 16,  // BLOCK_4X4,   BLOCK_4X8,   BLOCK_8X4,   BLOCK_8X8
      16, 16, 16, 32,  // BLOCK_8X16,  BLOCK_16X8,  BLOCK_16X16, BLOCK_16X32
      32, 32, 32, 32,  // BLOCK_32X16, BLOCK_32X32, BLOCK_32X64, BLOCK_64X32
      32               // BLOCK_64X64
  };

  return table[bsize];
}
#endif
