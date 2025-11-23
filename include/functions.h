#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define POPCOUNT(X) __builtin_popcnt((X))
#else
#define POPCOUNT(X) __builtin_popcount((X))
#endif

#define XLOG2X(X) ((X) * log2((X)))
#define XLOG2Y(X, Y) ((X) * log2((Y)))

uint32_t
hamming_weight(const uint8_t*, size_t len);
float
hamming_weight_frac(const uint8_t*, size_t len);

int32_t
hamming_dist(const uint8_t*, const uint8_t*, size_t len);
float
hamming_dist_frac(const uint8_t*, const uint8_t*, size_t len);

float
entropy_shannon(const uint8_t*, size_t len);
