#include "datetime_utc.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool parse_uint_n(const char *s, int n, int *out) {
  int v = 0;
  for (int i = 0; i < n; i++) {
    char c = s[i];
    if (c < '0' || c > '9') {
      return false;
    }
    v = v * 10 + (c - '0');
  }
  *out = v;
  return true;
}

int32_t tinyclj_days_from_civil_utc(int year, int month, int day) {
  // Howard Hinnant algorithm; returns days since 1970-01-01.
  int y = (month <= 2) ? (year - 1) : year;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned mp = (unsigned)((month > 2) ? (month - 3) : (month + 9));
  unsigned doy = (unsigned)((153 * mp + 2) / 5 + (unsigned)day - 1);
  unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  int64_t z = (int64_t)era * 146097 + (int64_t)doe;
  return (int32_t)(z - 719468);
}

void tinyclj_civil_from_days_utc(int32_t unix_days, int *out_year, int *out_month, int *out_day) {
  // Howard Hinnant algorithm; inverse of days-from-civil.
  int64_t z = (int64_t)unix_days + 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  int64_t doe = z - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = yoe + era * 400;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  int64_t d = doy - (153 * mp + 2) / 5 + 1;
  int64_t m = mp + (mp < 10 ? 3 : -9);
  y += (m <= 2);

  *out_year = (int)y;
  *out_month = (int)m;
  *out_day = (int)d;
}

const char *tinyclj_parse_iso8601_utc_instant(const char *iso, int32_t *out_days, uint32_t *out_ms) {
  if (!iso) {
    return "#inst expects a non-null string";
  }

  size_t len = strlen(iso);
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, millis = 0;

  bool has_millis = false;
  if (len == 20) {
    // YYYY-MM-DDTHH:MM:SSZ
    has_millis = false;
  } else if (len == 24) {
    // YYYY-MM-DDTHH:MM:SS.mmmZ
    has_millis = true;
  } else {
    return "Invalid #inst format (expected ISO-8601 UTC)";
  }

  if (!parse_uint_n(iso + 0, 4, &year) ||
      iso[4] != '-' ||
      !parse_uint_n(iso + 5, 2, &month) ||
      iso[7] != '-' ||
      !parse_uint_n(iso + 8, 2, &day) ||
      iso[10] != 'T' ||
      !parse_uint_n(iso + 11, 2, &hour) ||
      iso[13] != ':' ||
      !parse_uint_n(iso + 14, 2, &minute) ||
      iso[16] != ':' ||
      !parse_uint_n(iso + 17, 2, &second)) {
    return "Invalid #inst format (bad date/time fields)";
  }

  if (has_millis) {
    if (iso[19] != '.' || !parse_uint_n(iso + 20, 3, &millis)) {
      return "Invalid #inst format (bad milliseconds)";
    }
    if (iso[23] != 'Z') {
      return "Invalid #inst format (UTC-only; expected trailing Z)";
    }
  } else {
    if (iso[19] != 'Z') {
      return "Invalid #inst format (UTC-only; expected trailing Z)";
    }
  }

  if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 || second < 0 || second > 59 || millis < 0 || millis > 999) {
    return "Invalid #inst format (out-of-range fields)";
  }

  int32_t days = tinyclj_days_from_civil_utc(year, month, day);
  uint32_t ms = (uint32_t)(hour * 3600000 + minute * 60000 + second * 1000 + millis);

  if (out_days) {
    *out_days = days;
  }
  if (out_ms) {
    *out_ms = ms;
  }

  return NULL;
}

size_t tinyclj_format_inst_literal_iso8601_utc(char *buf, size_t buf_size, int32_t days, uint32_t ms) {
  int year = 0, month = 0, day = 0;
  tinyclj_civil_from_days_utc(days, &year, &month, &day);

  uint32_t tmp = ms;
  uint32_t hour = tmp / 3600000u;
  tmp %= 3600000u;
  uint32_t minute = tmp / 60000u;
  tmp %= 60000u;
  uint32_t second = tmp / 1000u;
  uint32_t millis = tmp % 1000u;

  return (size_t)snprintf(
      buf,
      buf_size,
      "#inst \"%04d-%02d-%02dT%02u:%02u:%02u.%03uZ\"",
      year,
      month,
      day,
      (unsigned)hour,
      (unsigned)minute,
      (unsigned)second,
      (unsigned)millis);
}
