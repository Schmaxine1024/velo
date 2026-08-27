// Number -> string, in whichever units the rider picked.
//
// Everything here is integer arithmetic. The watch has no hardware FPU, so a
// float divide is a call into libgcc's soft-float; doing that six times per
// second in a redraw loop is measurable battery. Fixed-point costs nothing and
// the readouts only ever show one or two decimal places anyway.
//
// The value and its unit are formatted separately because the ride screen
// draws them in different fonts -- a big bold number with a small grey label
// under it reads far better at a glance than one run-on string.

#pragma once

#include <pebble.h>

// "12.34" below 100, "123.4" at or above it, so the field never outgrows its
// cell. Metres in, kilometres or miles out.
void fmt_distance(char *buf, size_t n, uint32_t metres, bool imperial);
const char *fmt_distance_unit(bool imperial);

// "24.3". Centimetres per second in, km/h or mph out.
void fmt_speed(char *buf, size_t n, uint16_t cms, bool imperial);
const char *fmt_speed_unit(bool imperial);

// "1234". Metres in, metres or feet out -- ascent is never shown fractionally.
void fmt_ascent(char *buf, size_t n, uint16_t metres, bool imperial);
const char *fmt_ascent_unit(bool imperial);

// "45:12" under an hour, "2:45:12" over it. Seconds in.
void fmt_duration(char *buf, size_t n, uint32_t seconds);

// "Tue 26 Aug" for the history list. Unix time in.
void fmt_date(char *buf, size_t n, uint32_t unix_time);
