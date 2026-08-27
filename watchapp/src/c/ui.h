// Screens, and the drawing vocabulary they share.
//
// Four windows: ready, ride, summary, history. Each module owns its Window and
// exposes push/update. The update calls are no-ops when that window is not
// loaded, so main.c can broadcast "the model moved" without tracking which
// screen is on top.

#pragma once

#include <pebble.h>
#include "ride.h"
#include "theme.h"

// ---------------------------------------------------------------------------
// Palette
//
// Background and accent come from the phone; ink, muted and rule are derived
// from the background so contrast holds whatever the rider picks. See theme.h
// for why only two of the five are actually choosable.
//
// These stay spelled COL_* so the call sites read as colour constants, but
// they are function calls now -- do not cache them across a settings change.
// ---------------------------------------------------------------------------

#define COL_BG      theme_bg()
#define COL_INK     theme_ink()
#define COL_ACCENT  theme_accent()
#define COL_MUTED   theme_muted()
#define COL_RULE    theme_rule()

// The band across the top of the ready and ride screens. Taller than it was:
// it is a filled block of colour now, carrying bold type rather than a line
// of grey. Round screens lose their corners, so they need more still.
#define STATUS_H  PBL_IF_RECT_ELSE(26, 34)

// ---------------------------------------------------------------------------
// Shared drawing
// ---------------------------------------------------------------------------

// One metric: a bold label above a large value, the pair centred as a block
// inside `box`. `unit` may be NULL, and rides on the label line so the number
// keeps the visual weight.
//
// Both colours are explicit because a metric drawn on a filled accent panel
// needs the opposite ink from one drawn on the background, and the caller is
// the only thing that knows which it is painting on.
void ui_draw_metric(GContext *ctx, GRect box, const char *label,
                    const char *value, const char *unit,
                    GFont value_font, GColor label_colour, GColor value_colour);

// Fill `box` with `colour`, corners rounded by `radius` (0 for square).
// Used for the accent panels the layout is built from -- big blocks of flat
// colour rather than hairline-ruled cells.
void ui_fill(GContext *ctx, GRect box, GColor colour, int16_t radius);

// Corner radius for the floating panels. Rounded because square-cornered
// blocks read as a table, and this is a bike computer, not a spreadsheet.
#define PANEL_RADIUS  PBL_IF_RECT_ELSE(10, 12)

// Height reserved for a footer line of label-font text. Must track the label
// font: it grew, and the old hard-coded 18 started clipping descenders.
#define FOOTER_H  PBL_IF_RECT_ELSE(22, 24)

// Inset of a floating panel from the screen edge.
#define PANEL_INSET   PBL_IF_RECT_ELSE(4, 14)

// Clearance between a floating panel and the status band above it. A rounded
// card butted straight against a full-bleed band reads as a rendering
// mistake -- the eye wants to see that the card is a separate object sitting
// below the bar, not a piece that failed to line up with it.
#define PANEL_GAP     PBL_IF_RECT_ELSE(7, 9)

// GPS fix strength as three ascending bars, hollow for the bars not earned.
// Drawn in `colour`, since it sits on the accent band rather than on the
// background.
void ui_draw_fix(GContext *ctx, GPoint origin, uint8_t fix, GColor colour);

// The status bar: fix bars on the left, state word centred, phone battery on
// the right. Draws its own bottom rule. Returns the y below the rule.
//
// `in_ride` selects how much the bar says. During a ride it carries the state
// word and, if the link drops, a NO PHONE banner -- it is the only place those
// can appear. On the ready screen both are suppressed: the state word there is
// always "READY", which is not information, and the headline beneath already
// announces a missing phone in large type. Saying either twice, one line
// apart, reads as a rendering bug rather than as emphasis.
int16_t ui_draw_status(GContext *ctx, GRect bounds, const RideState *r,
                       bool in_ride);

// Fonts, resolved per platform so the screens agree with each other.
//
// All of these are full-charset faces. See ui_font_hero for why the larger
// numeric-subset fonts cannot be used, however inviting their size is.
GFont ui_font_hero(void);
GFont ui_font_value(void);
GFont ui_font_label(void);

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

void ui_ready_push(void);
void ui_ready_update(void);
void ui_ready_deinit(void);

// True when the ready screen is the one actually on top. main.c uses it to
// decide whether a ride starting on the phone should auto-advance the watch.
bool ui_ready_is_top(void);

void ui_ride_push(void);
void ui_ride_update(void);
void ui_ride_deinit(void);

// Take the ride window off the stack without popping to it first. Used when
// the ride ends somewhere other than on the ride screen itself.
void ui_ride_dismiss(void);

// `from_history` swaps the footer hint and suppresses the vibration, so
// browsing an old ride does not feel like finishing a new one.
void ui_summary_push(const RideSummary *summary, bool from_history);
void ui_summary_deinit(void);

void ui_history_push(void);
void ui_history_update(void);
void ui_history_deinit(void);
