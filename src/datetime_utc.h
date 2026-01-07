#pragma once

#include <stdint.h>
#include <stddef.h>

// UTC-only date algorithms + ISO-8601 instant parsing/formatting.
// These are used by native code paths like the reader/parser and pr-str.

int32_t tinyclj_days_from_civil_utc(int year, int month, int day);
void tinyclj_civil_from_days_utc(int32_t unix_days, int *out_year, int *out_month, int *out_day);

// Parses ISO-8601 UTC instants:
//   YYYY-MM-DDTHH:MM:SSZ
//   YYYY-MM-DDTHH:MM:SS.mmmZ
// Returns NULL on success, otherwise a static error message.
const char *tinyclj_parse_iso8601_utc_instant(const char *iso, int32_t *out_days, uint32_t *out_ms);

// Formats an instant as a tagged literal:
//   #inst "YYYY-MM-DDTHH:MM:SS.mmmZ"
// Returns the number of characters written (snprintf-style).
size_t tinyclj_format_inst_literal_iso8601_utc(char *buf, size_t buf_size, int32_t days, uint32_t ms);
