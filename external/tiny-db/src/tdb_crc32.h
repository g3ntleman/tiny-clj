// tdb_crc32.h - Small CRC32 implementation (IEEE 802.3 polynomial).

#pragma once

#include <stddef.h>
#include <stdint.h>

uint32_t tdb_crc32_ieee(const void* data, size_t len, uint32_t seed);
