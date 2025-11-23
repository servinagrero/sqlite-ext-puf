#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1

#include "../include/functions.h"

static void
hamming_weight_func(sqlite3_context* context, int argc, sqlite3_value** argv)
{
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  int len = sqlite3_value_bytes(argv[0]);
  const uint8_t* vec = sqlite3_value_blob(argv[0]);

  int result = hamming_weight(vec, len);
  sqlite3_result_int(context, result);
}

static void
hamming_weight_frac_func(sqlite3_context* context,
                         int argc,
                         sqlite3_value** argv)
{
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  int len = sqlite3_value_bytes(argv[0]);
  const uint8_t* vec = sqlite3_value_blob(argv[0]);

  float result = hamming_weight_frac(vec, len);
  sqlite3_result_double(context, result);
}

static void
hamming_func(sqlite3_context* context, int argc, sqlite3_value** argv)
{
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL ||
      sqlite3_value_type(argv[1]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }

  int len_fst = sqlite3_value_bytes(argv[0]);
  int len_snd = sqlite3_value_bytes(argv[1]);

  // We should have already check that all blobs have the same length when
  // inserting them, but we do it for sanity check
  if (len_fst != len_snd) {
    sqlite3_result_error(context, "Blobs must be same size", -1);
    return;
  }

  const uint8_t* vec_fst = sqlite3_value_blob(argv[0]);
  const uint8_t* vec_snd = sqlite3_value_blob(argv[1]);

  int result = hamming_dist(vec_fst, vec_snd, len_fst);
  sqlite3_result_int(context, result);
}

static void
frac_hamming_func(sqlite3_context* context, int argc, sqlite3_value** argv)
{
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL ||
      sqlite3_value_type(argv[1]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  int len_fst = sqlite3_value_bytes(argv[0]);
  int len_snd = sqlite3_value_bytes(argv[1]);

  if (len_fst != len_snd) {
    sqlite3_result_error(context, "Blobs must be same size", -1);
    return;
  }

  const uint8_t* vec_fst = sqlite3_value_blob(argv[0]);
  const uint8_t* vec_snd = sqlite3_value_blob(argv[1]);

  float result = hamming_dist_frac(vec_fst, vec_snd, len_fst);
  sqlite3_result_double(context, result);
}

static void
entropy_func(sqlite3_context* context, int argc, sqlite3_value** argv)
{
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  int len = sqlite3_value_bytes(argv[0]);
  const uint8_t* vec = sqlite3_value_blob(argv[0]);

  float result = entropy_shannon(vec, len);
  sqlite3_result_double(context, result);
}

// This Bitaliasing context persists while the database loops through rows
typedef struct
{
  unsigned long* bit_counts;
  size_t len_bytes;         // Cache the PUF response length
  unsigned long total_rows; // Total number of rows processed
  int is_error;             // Flag to stop processing if bad data found
} BAContext;

// Called once for every row in the query.
static void
bitaliasing_step(sqlite3_context* context, int argc, sqlite3_value** argv)
{
  // This struct is zeroed out by SQLite on the very first call
  BAContext* ctx = sqlite3_aggregate_context(context, sizeof(BAContext));
  if (!ctx || ctx->is_error)
    return;

  if (sqlite3_value_type(argv[0]) == SQLITE_NULL)
    return;
  int current_len = sqlite3_value_bytes(argv[0]);
  const unsigned char* blob = sqlite3_value_blob(argv[0]);

  if (ctx->bit_counts == NULL) {
    ctx->len_bytes = current_len;

    ctx->bit_counts =
      (unsigned long*)calloc(current_len * 8, sizeof(unsigned long));

    if (ctx->bit_counts == NULL) {
      sqlite3_result_error_nomem(context);
      ctx->is_error = 1;
      return;
    }
  }

  // Sanity check to ensure all blobs have the same size
  if (current_len != ctx->len_bytes) {
    sqlite3_result_error(
      context,
      "Blobs have varying lengths. Cannot compute vector average.",
      -1);
    // We do not free here. We mark error. xFinal will clean up.
    ctx->is_error = 1;
    return;
  }

  for (int i = 0; i < current_len; i++) {
    unsigned char byte = blob[i];

    for (int bit = 0; bit < 8; bit++) {
      if ((byte >> bit) & 1) {
        int flat_idx = (i * 8) + bit;
        ctx->bit_counts[flat_idx]++;
      }
    }
  }

  ctx->total_rows++;
}

static void
bitaliasing_final(sqlite3_context* context)
{
  BAContext* ctx = sqlite3_aggregate_context(context, 0);

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

  double* result_vector = (double*)malloc(total_bits * sizeof(double));

  if (result_vector == NULL) {
    free(ctx->bit_counts);
    sqlite3_result_error_nomem(context);
    return;
  }

  for (int i = 0; i < total_bits; i++) {
    result_vector[i] = (double)ctx->bit_counts[i] / ctx->total_rows;
  }

  free(ctx->bit_counts);
  ctx->bit_counts = NULL;

  // We pass 'free' as the destructor. SQLite will call
  // free(result_vector) automatically after it is done sending the blob
  // to the user. This is Zero-Copy for us!
  sqlite3_result_blob(
    context, result_vector, total_bits * sizeof(double), free);
}

typedef struct
{
  unsigned long* bit_matches;
  size_t ref_len;
  size_t num_samples;
  int is_error;
} RelContext;

// Called once for every row in the query.
static void
reliability_step(sqlite3_context* context, int argc, sqlite3_value** argv)
{
  // A. Get persistent context
  // This struct is zeroed out by SQLite on the very first call
  RelContext* ctx = sqlite3_aggregate_context(context, sizeof(RelContext));
  if (!ctx || ctx->is_error)
    return;

  if (sqlite3_value_type(argv[0]) == SQLITE_NULL)
    return;
  int ref_len = sqlite3_value_bytes(argv[0]);
  const uint8_t* ref = sqlite3_value_blob(argv[0]);

  if (sqlite3_value_type(argv[1]) == SQLITE_NULL)
    return;
  int vec_len = sqlite3_value_bytes(argv[1]);
  const uint8_t* vec = sqlite3_value_blob(argv[1]);

  if (ctx->bit_matches == NULL) {
    ctx->ref_len = ref_len;

    ctx->bit_matches = (unsigned long*)calloc(ref_len * 8, sizeof(size_t));

    if (ctx->bit_matches == NULL) {
      sqlite3_result_error_nomem(context);
      ctx->is_error = 1;
      return;
    }
  }

  // If row 2 is smaller/larger than row 1, we must abort
  if (ref_len != vec_len) {
    sqlite3_result_error(
      context,
      "Blobs have varying lengths. Cannot compute vector average.",
      -1);
    // We do not free here. We mark error. xFinal will clean up.
    ctx->is_error = 1;
    return;
  }

  for (size_t i = 0; i < ref_len; i++) {
    for (int bit = 0; bit < 8; bit++) {
      int flat_idx = (i * 8) + bit;
      ctx->bit_matches[flat_idx] +=
        ((ref[i] >> bit) & 1) == ((vec[i] >> bit) & 1);
    }
  }

  ctx->num_samples++;
}

static void
reliability_final(sqlite3_context* context)
{
  RelContext* ctx = sqlite3_aggregate_context(context, 0);

  if (!ctx || ctx->bit_matches == NULL) {
    sqlite3_result_null(context);
    return;
  }

  if (ctx->is_error) {
    free(ctx->bit_matches);
    return;
  }

  int total_bits = ctx->ref_len * 8;

  double* result_vector = (double*)malloc(total_bits * sizeof(double));

  if (result_vector == NULL) {
    free(ctx->bit_matches);
    sqlite3_result_error_nomem(context);
    return;
  }

  for (int i = 0; i < total_bits; i++) {
    result_vector[i] = (double)ctx->bit_matches[i] / ctx->num_samples;
  }

  free(ctx->bit_matches);
  ctx->bit_matches = NULL;

  sqlite3_result_blob(
    context, result_vector, total_bits * sizeof(double), free);
}

#ifdef _WIN32
__declspec(dllexport)
#endif
int
sqlite3_extension_init(sqlite3* db,
                       char** pzErrMsg,
                       const sqlite3_api_routines* pApi)
{
  SQLITE_EXTENSION_INIT2(pApi);
  int err;

  err = sqlite3_create_function(
    db, "HW", 1, SQLITE_UTF8, 0, hamming_weight_func, 0, 0);
  err = sqlite3_create_function(
    db, "FHW", 1, SQLITE_UTF8, 0, hamming_weight_frac_func, 0, 0);
  err =
    sqlite3_create_function(db, "HD", 2, SQLITE_UTF8, 0, hamming_func, 0, 0);
  err = sqlite3_create_function(
    db, "FHD", 2, SQLITE_UTF8, 0, frac_hamming_func, 0, 0);
  err = sqlite3_create_function(
    db, "entropy", 1, SQLITE_UTF8, 0, entropy_func, 0, 0);

  err = sqlite3_create_function(db,
                                "bitaliasing",
                                1,
                                SQLITE_UTF8,
                                0,
                                0,
                                bitaliasing_step,
                                bitaliasing_final);

  err = sqlite3_create_function(db,
                                "reliability",
                                2,
                                SQLITE_UTF8,
                                0,
                                0,
                                reliability_step,
                                reliability_final);

  return err;
}
