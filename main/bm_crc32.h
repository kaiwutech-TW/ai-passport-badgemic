// main/bm_crc32.h - CRC-32/ISO-HDLC (zlib crc32), incremental. Pure C.
#pragma once
#include <stddef.h>
#include <stdint.h>

// Start with crc = 0; feed chunks in order; the result equals zlib crc32().
uint32_t bm_crc32_update(uint32_t crc, const uint8_t *data, size_t len);
