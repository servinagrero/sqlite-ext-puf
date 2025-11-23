#include "../include/functions.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

inline uint32_t
hamming_weight(const uint8_t* v, size_t len)
{
  uint32_t dist = 0;
  for (size_t i = 0; i < len; i++) {
    dist += POPCOUNT(v[i]);
  }
  return dist;
}

inline float
hamming_weight_frac(const uint8_t* v, size_t len)
{
  return (float)hamming_weight(v, len) / (len * 8);
}

inline int32_t
hamming_dist(const uint8_t* fst, const uint8_t* snd, size_t len)
{
  uint32_t dist = 0;
  for (size_t i = 0; i < len; i++) {
    uint8_t x = fst[i] ^ snd[i];
    dist += POPCOUNT(x);
  }
  return dist;
}

inline float
hamming_dist_frac(const uint8_t* fst, const uint8_t* snd, size_t len)
{
  uint32_t dist = hamming_dist(fst, snd, len);
  return (float)dist / (len * 8);
}

inline float
entropy_shannon(const unsigned char* v, size_t len)
{
  float p = hamming_weight_frac(v, len);
  if (p <= 0.0 || p >= 1.0) {
    return 0.0;
  } else {
    return -((XLOG2X(p)) + (XLOG2X(1 - p)));
  }
}
