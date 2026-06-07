#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <FS.h>
#include "esp_err.h"

typedef struct {
    uint32_t bytes;          // total bytes read (== flash size on success)
    char     sha256_hex[65]; // lowercase hex SHA-256 over the whole image
    bool     wrote_file;     // true if also written to a file
} backup_result_t;

// Called periodically with cumulative progress (bytes done, total).
typedef void (*backup_progress_cb)(uint32_t done, uint32_t total);

// Read the entire flash (0..flash_size) in chunks, streaming a SHA-256.
// If `out` != NULL, also write the raw image to that open file. `cb` may be NULL.
esp_err_t backup_full_flash(fs::File *out, uint32_t flash_size,
                            backup_result_t *res, backup_progress_cb cb);

// Recompute the SHA-256 of an already-open file (to verify a backup on SD).
// Writes 64 lowercase hex chars + NUL into out_hex. `cb` may be NULL.
esp_err_t sha256_file(fs::File *in, char out_hex[65], backup_progress_cb cb);
