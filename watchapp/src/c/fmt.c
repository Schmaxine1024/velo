#include "fmt.h"

// Conversion constants, all expressed so the multiply happens before the
// divide and the intermediate stays inside the type named in the comment.

// Millimetres in a mile. Named for the unit it is actually in: the obvious
// misreading is "metres per mile", and `metres / MM_PER_MILE` would then
// quietly return 0 for every ride shorter than 1609 km.
#define MM_PER_MILE      1609344ULL

void fmt_distance(char *buf, size_t n, uint32_t metres, bool imperial) {
  // Work in hundredths of the display unit, then split. The imperial path
  // needs 64-bit for the intermediate: 500 km in metres times 100000 is about
  // 5e10, which laps a uint32 several times over.
  uint32_t hundredths;
  if (imperial) {
    hundredths = (uint32_t)(((uint64_t)metres * 100000ULL) / MM_PER_MILE);
  } else {
    hundredths = metres / 10;
  }

  uint32_t whole = hundredths / 100;
  if (whole >= 100) {
    // Three integer digits already; one decimal is all that still fits.
    snprintf(buf, n, "%u.%u", (unsigned)whole, (unsigned)((hundredths % 100) / 10));
  } else {
    snprintf(buf, n, "%u.%02u", (unsigned)whole, (unsigned)(hundredths % 100));
  }
}

const char *fmt_distance_unit(bool imperial) {
  return imperial ? "MI" : "KM";
}

void fmt_speed(char *buf, size_t n, uint16_t cms, bool imperial) {
  // Tenths of the display unit.
  //   km/h = cm/s * 0.036          -> * 36 / 1000, scaled by 10 for tenths
  //   mph  = cm/s * 0.0223694      -> * 2237 / 10000, likewise
  // The mph factor is truncated at four digits; the error is 0.003%, which is
  // three orders of magnitude below GPS speed noise.
  uint32_t tenths = imperial
      ? ((uint32_t)cms * 2237u) / 10000u
      : ((uint32_t)cms * 36u) / 100u;

  snprintf(buf, n, "%u.%u", (unsigned)(tenths / 10), (unsigned)(tenths % 10));
}

const char *fmt_speed_unit(bool imperial) {
  return imperial ? "MPH" : "KM/H";
}

void fmt_ascent(char *buf, size_t n, uint16_t metres, bool imperial) {
  uint32_t v = imperial ? ((uint32_t)metres * 3281u) / 1000u : metres;
  snprintf(buf, n, "%u", (unsigned)v);
}

const char *fmt_ascent_unit(bool imperial) {
  return imperial ? "FT" : "M";
}

void fmt_duration(char *buf, size_t n, uint32_t seconds) {
  uint32_t h = seconds / 3600;
  uint32_t m = (seconds % 3600) / 60;
  uint32_t s = seconds % 60;

  // Hours are dropped entirely below the hour mark rather than shown as a
  // leading "0:", so the common case reads as a stopwatch and the digits can
  // be drawn a size larger.
  if (h > 0) {
    snprintf(buf, n, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
  } else {
    snprintf(buf, n, "%u:%02u", (unsigned)m, (unsigned)s);
  }
}

void fmt_date(char *buf, size_t n, uint32_t unix_time) {
  time_t t = (time_t)unix_time;
  struct tm *lt = localtime(&t);
  if (lt == NULL) {
    snprintf(buf, n, "--");
    return;
  }
  // Unlike snprintf, strftime does not truncate: if the result will not fit it
  // returns 0 and leaves the buffer's contents unspecified -- not even
  // guaranteed NUL-terminated. A history row would then render stack noise.
  if (strftime(buf, n, "%a %d %b", lt) == 0) {
    snprintf(buf, n, "--");
  }
}
