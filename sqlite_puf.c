#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1

#ifdef _WIN32
#define POPCOUNT(X) __builtin_popcnt((X))
#else
#define POPCOUNT(X) __builtin_popcount((X))
#endif

#define XLOG2X(X) ((X) * log2((X)))
#define XLOG2Y(X, Y) ((X) * log2((Y)))

typedef int (*distance_fn)(const uint8_t *, const uint8_t *, size_t);
typedef float (*distance_frac_fn)(const uint8_t *, const uint8_t *, size_t);

static inline uint32_t hamming_weight(const uint8_t *a, size_t len)
{
	uint32_t dist = 0;
	for (size_t i = 0; i < len; i++) {
		dist += POPCOUNT(a[i]);
	}
	return dist;
}

static void hamming_weight_func(sqlite3_context *context, int argc,
				sqlite3_value **argv)
{
	if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
		sqlite3_result_null(context);
		return;
	}
	int len = sqlite3_value_bytes(argv[0]);
	const uint8_t *vec = sqlite3_value_blob(argv[0]);

	int result = hamming_weight(vec, len);
	sqlite3_result_int(context, result);
}

static inline float hamming_weight_frac(const uint8_t *a, size_t len)
{
	return (float)hamming_weight(a, len) / (len * 8);
}

static void hamming_weight_frac_func(sqlite3_context *context, int argc,
				     sqlite3_value **argv)
{
	if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
		sqlite3_result_null(context);
		return;
	}
	int len = sqlite3_value_bytes(argv[0]);
	const uint8_t *vec = sqlite3_value_blob(argv[0]);

	float result = hamming_weight_frac(vec, len);
	sqlite3_result_double(context, result);
}

static inline uint32_t hamming_dist(const uint8_t *a, const uint8_t *b,
				    size_t len)
{
	uint32_t dist = 0;
	for (size_t i = 0; i < len; i++) {
		uint8_t x = a[i] ^ b[i];
		dist += POPCOUNT(x);
	}
	return dist;
}

static inline float hamming_dist_frac(const uint8_t *a, const uint8_t *b,
				      size_t len)
{
	uint32_t dist = hamming_dist(a, b, len);
	return (float)dist / (len * 8);
}

static void hamming_func(sqlite3_context *context, int argc,
			 sqlite3_value **argv)
{
	int len_fst = sqlite3_value_bytes(argv[0]);
	int len_snd = sqlite3_value_bytes(argv[1]);
	const uint8_t *vec_fst = sqlite3_value_blob(argv[0]);
	const uint8_t *vec_snd = sqlite3_value_blob(argv[1]);

	// We should have already check that all blobs have the same length when
	// inserting them, but we do it for sanity check
	if (len_fst != len_snd) {
		sqlite3_result_error(context, "Blobs must be same size", -1);
		return;
	}

	int result = hamming_dist(vec_fst, vec_snd, len_fst);
	sqlite3_result_int(context, result);
}

static void frac_hamming_func(sqlite3_context *context, int argc,
			      sqlite3_value **argv)
{
	int len_fst = sqlite3_value_bytes(argv[0]);
	int len_snd = sqlite3_value_bytes(argv[1]);
	const uint8_t *vec_fst = sqlite3_value_blob(argv[0]);
	const uint8_t *vec_snd = sqlite3_value_blob(argv[1]);

	if (len_fst != len_snd) {
		sqlite3_result_error(context, "Blobs must be same size", -1);
		return;
	}

	float result = hamming_dist_frac(vec_fst, vec_snd, len_fst);
	sqlite3_result_double(context, result);
}

static inline float compute_entropy(const unsigned char *a, size_t len)
{
	float p = hamming_weight_frac(a, len);
	if (p <= 0.0 || p >= 1.0) {
		return 0.0;
	} else {
		return -((XLOG2X(p)) + (XLOG2X(1 - p)));
	}
}

static void entropy_func(sqlite3_context *context, int argc,
			 sqlite3_value **argv)
{
	if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
		sqlite3_result_null(context);
		return;
	}
	int len = sqlite3_value_bytes(argv[0]);
	const uint8_t *vec = sqlite3_value_blob(argv[0]);

	float result = compute_entropy(vec, len);
	sqlite3_result_double(context, result);
}


// This Bitaliasing context persists while the database loops through rows
typedef struct {
	unsigned long *bit_counts;
	size_t len_bytes;	  // Cache the PUF response length
	unsigned long total_rows; // Total number of rows processed
	int is_error;		  // Flag to stop processing if bad data found
} BAContext;


// Called once for every row in the query.
static void bitaliasing_step(sqlite3_context *context, int argc,
			     sqlite3_value **argv)
{
	// A. Get persistent context
	// This struct is zeroed out by SQLite on the very first call
	BAContext *ctx = sqlite3_aggregate_context(context, sizeof(BAContext));
	if (!ctx || ctx->is_error)
		return;

	// B. Handle Input
	if (sqlite3_value_type(argv[0]) == SQLITE_NULL)
		return;
	int current_len = sqlite3_value_bytes(argv[0]);
	const unsigned char *blob = sqlite3_value_blob(argv[0]);

	// C. FIRST ROW INITIALIZATION
	if (ctx->bit_counts == NULL) {
		ctx->len_bytes = current_len;

		// Allocate counters: 1 counter per BIT (Bytes * 8)
		// We use calloc to ensure they start at 0
		ctx->bit_counts = (unsigned long *)calloc(
			current_len * 8, sizeof(unsigned long));

		if (ctx->bit_counts == NULL) {
			sqlite3_result_error_nomem(context);
			ctx->is_error = 1;
			return;
		}
	}

	// D. Consistency Check
	// If row 2 is smaller/larger than row 1, we must abort
	if (current_len != ctx->len_bytes) {
		sqlite3_result_error(
			context,
			"Blobs have varying lengths. Cannot compute vector average.",
			-1);
		// We do NOT free here. We mark error. xFinal will clean up.
		ctx->is_error = 1;
		return;
	}

	// E. BIT MANIPULATION LOOP
	// Iterate over bytes
	for (int i = 0; i < current_len; i++) {
		unsigned char byte = blob[i];

		// Iterate over bits (0 to 7)
		for (int bit = 0; bit < 8; bit++) {
			// Check if bit is set.
			// This assumes Bit 0 is LSB.
			// If you want MSB as index 0, use: (byte >> (7 - bit))
			// & 1
			if ((byte >> bit) & 1) {
				// Calculate index in the giant flat array
				int flat_idx = (i * 8) + bit;
				ctx->bit_counts[flat_idx]++;
			}
		}
	}

	ctx->total_rows++;
}

static void bitaliasing_final(sqlite3_context *context)
{
	BAContext *ctx = sqlite3_aggregate_context(context, 0);

	// Handle Cases where Step was never called or Error occurred
	if (!ctx || ctx->bit_counts == NULL) {
		sqlite3_result_null(context);
		return;
	}

	// If we hit an error flag during steps, we still need to free memory,
	// but we don't return a result.
	if (ctx->is_error) {
		free(ctx->bit_counts);
		return;
	}

	int total_bits = ctx->len_bytes * 8;

	// We malloc this because it might be too large for the stack
	double *result_vector = (double *)malloc(total_bits * sizeof(double));

	if (result_vector == NULL) {
		free(ctx->bit_counts);
		sqlite3_result_error_nomem(context);
		return;
	}

	for (int i = 0; i < total_bits; i++) {
		result_vector[i] = (double)ctx->bit_counts[i] / ctx->total_rows;
	}

	free(ctx->bit_counts);
	ctx->bit_counts = NULL; // Safety

	// We pass 'free' as the destructor. SQLite will call
	// free(result_vector) automatically after it is done sending the blob
	// to the user. This is Zero-Copy for us!
	sqlite3_result_blob(context, result_vector, total_bits * sizeof(double),
			    free);
}

// Entry point
#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_extension_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi)
{
	SQLITE_EXTENSION_INIT2(pApi);
	int err;
	err = sqlite3_create_function(db, "HW", 1, SQLITE_UTF8, 0,
				      hamming_weight_func, 0, 0);
	err = sqlite3_create_function(db, "FHW", 1, SQLITE_UTF8, 0,
				      hamming_weight_frac_func, 0, 0);
	err = sqlite3_create_function(db, "HD", 2, SQLITE_UTF8, 0, hamming_func,
				      0, 0);
	err = sqlite3_create_function(db, "FHD", 2, SQLITE_UTF8, 0,
				      frac_hamming_func, 0, 0);
	err = sqlite3_create_function(db, "entropy", 1, SQLITE_UTF8, 0,
				      entropy_func, 0, 0);

	// bitaliasing_step = function called for every row
	// bitaliasing_final = function called at the end
	return sqlite3_create_function(db, "bitaliasing", 1, SQLITE_UTF8, 0, 0,
				       bitaliasing_step, bitaliasing_final);
	return err;
}
