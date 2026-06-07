#include "backup.h"
#include <Arduino.h>
#include <string.h>
#include "esp_flash.h"
#include "mbedtls/md.h"   // generic MD API — stable across mbedtls 2.x / 3.x

#define CHUNK 4096

static void to_hex(const uint8_t *in, size_t n, char *out) {
    static const char *d = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[2*i] = d[in[i] >> 4]; out[2*i+1] = d[in[i] & 0xf]; }
    out[2*n] = '\0';
}

esp_err_t backup_full_flash(fs::File *out, uint32_t flash_size,
                            backup_result_t *res, backup_progress_cb cb) {
    memset(res, 0, sizeof(*res));
    static uint8_t buf[CHUNK];   // static: off the stack

    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    if (!info || mbedtls_md_setup(&md, info, 0) != 0) { mbedtls_md_free(&md); return ESP_FAIL; }
    mbedtls_md_starts(&md);

    uint32_t off = 0;
    esp_err_t err = ESP_OK;
    while (off < flash_size) {
        uint32_t n = (flash_size - off) < CHUNK ? (flash_size - off) : CHUNK;
        err = esp_flash_read(NULL /* default chip */, buf, off, n);
        if (err != ESP_OK) break;
        mbedtls_md_update(&md, buf, n);
        if (out) {
            if (out->write(buf, n) != n) { err = ESP_FAIL; break; }
        }
        off += n;
        if (cb) cb(off, flash_size);
    }

    if (err == ESP_OK) {
        uint8_t digest[32];
        mbedtls_md_finish(&md, digest);
        to_hex(digest, sizeof(digest), res->sha256_hex);
        res->bytes = off;
        res->wrote_file = (out != nullptr);
    }
    mbedtls_md_free(&md);
    return err;
}

esp_err_t sha256_file(fs::File *in, char out_hex[65], backup_progress_cb cb) {
    out_hex[0] = '\0';
    if (!in) return ESP_FAIL;
    static uint8_t buf[CHUNK];
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    if (!info || mbedtls_md_setup(&md, info, 0) != 0) { mbedtls_md_free(&md); return ESP_FAIL; }
    mbedtls_md_starts(&md);

    uint32_t total = (uint32_t)in->size(), done = 0;
    for (;;) {
        int n = in->read(buf, CHUNK);
        if (n <= 0) break;
        mbedtls_md_update(&md, buf, (size_t)n);
        done += (uint32_t)n;
        if (cb) cb(done, total ? total : done);
    }

    uint8_t digest[32];
    mbedtls_md_finish(&md, digest);
    to_hex(digest, sizeof(digest), out_hex);
    mbedtls_md_free(&md);
    return ESP_OK;
}
