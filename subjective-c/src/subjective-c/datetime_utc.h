#pragma once

#include <stdint.h>
#include <stddef.h>

// UTC-only date algorithms + ISO-8601 instant parsing/formatting.
// These are used by native code paths like the reader/parser and pr-str.

/** @brief Convert civil date to days since epoch
 * @param year Year (e.g. 1970)
 * @param month Month (1-12)
 * @param day Day (1-31)
 * @return Days since Unix epoch
 */
int32_t clj_days_from_civil_utc(int year, int month, int day);

/** @brief Convert days since epoch to civil date
 * @param unix_days Days since Unix epoch
 * @param out_year Output for year
 * @param out_month Output for month (1-12)
 * @param out_day Output for day (1-31)
 */
void tinyclj_civil_from_days_utc(int32_t unix_days, int *out_year, int *out_month, int *out_day);

/** @brief Parse ISO-8601 UTC instant string
 * @param iso ISO-8601 string (YYYY-MM-DDTHH:MM:SSZ or with .mmmZ)
 * @param out_days Output for days since epoch
 * @param out_ms Output for milliseconds within day
 * @return NULL on success, error message on failure
 */
const char *tinyclj_parse_iso8601_utc_instant(const char *iso, int32_t *out_days, uint32_t *out_ms);

/** @brief Format instant as tagged literal
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @param days Days since epoch
 * @param ms Milliseconds within day
 * @return Number of characters written
 */
size_t tinyclj_format_inst_literal_iso8601_utc(char *buf, size_t buf_size, int32_t days, uint32_t ms);
