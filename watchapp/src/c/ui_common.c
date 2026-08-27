#include "ui.h"
#include "fmt.h"

// ---------------------------------------------------------------------------
// Fonts
//
// Emery is 200x228 and can carry a 42px hero; the 144x168 platforms cannot,
// so they drop to Bitham 30 Black rather than to a numbers-only face -- every
// hero value here has a decimal point in it, and the LECO/Bitham *_NUMBERS
// fonts have no glyph for one. A missing '.' turns 24.3 into 243.
// ---------------------------------------------------------------------------

GFont ui_font_hero(void) {
#if defined(PBL_PLATFORM_EMERY)
  return fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
#else
  return fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
#endif
}

GFont ui_font_value(void) {
#if defined(PBL_PLATFORM_EMERY)
  return fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
#else
  return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
#endif
}

GFont ui_font_label(void) {
  return fonts_get_system_font(FONT_KEY_GOTHIC_14);
}

// ---------------------------------------------------------------------------
// Metric block
// ---------------------------------------------------------------------------

#define LABEL_H  16

void ui_draw_metric(GContext *ctx, GRect box, const char *label,
                    const char *value, const char *unit,
                    GFont value_font, GColor value_colour) {
  // The unit rides on the label line rather than beside the number. Putting it
  // next to the value would mean measuring the value first and hand-packing
  // two runs of different fonts on one baseline; "DIST KM" over a big 12.34
  // says the same thing, keeps the number optically centred in its cell, and
  // survives the value changing width as the ride goes on.
  char head[24];
  if (unit && unit[0]) {
    snprintf(head, sizeof(head), "%s %s", label, unit);
  } else {
    snprintf(head, sizeof(head), "%s", label);
  }

  GSize vs = graphics_text_layout_get_content_size(
      value, value_font, GRect(0, 0, box.size.w, box.size.h),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter);

  int16_t total = LABEL_H + vs.h;
  int16_t top = box.origin.y + (box.size.h - total) / 2;
  if (top < box.origin.y) {
    top = box.origin.y;
  }

  graphics_context_set_text_color(ctx, COL_MUTED);
  graphics_draw_text(ctx, head, ui_font_label(),
                     GRect(box.origin.x, top, box.size.w, LABEL_H),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Pebble fonts carry a few pixels of internal leading above the cap height,
  // so the value is pulled up slightly to close the gap the label leaves.
  graphics_context_set_text_color(ctx, value_colour);
  graphics_draw_text(ctx, value, value_font,
                     GRect(box.origin.x, top + LABEL_H - 4, box.size.w, vs.h + 6),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

#define FIX_BAR_W    3
#define FIX_BAR_GAP  2
#define FIX_MAX_H    10

void ui_draw_fix(GContext *ctx, GPoint origin, uint8_t fix) {
  for (int i = 0; i < 3; i++) {
    int16_t h = 4 + i * 3;
    GRect bar = GRect(origin.x + i * (FIX_BAR_W + FIX_BAR_GAP),
                      origin.y + (FIX_MAX_H - h),
                      FIX_BAR_W, h);
    if (fix > i) {
      graphics_context_set_fill_color(ctx, COL_INK);
      graphics_fill_rect(ctx, bar, 0, GCornerNone);
    } else {
      // Hollow, not absent: three outlines make it obvious the indicator has
      // three levels and none are lit, where drawing nothing reads as a bug.
      graphics_context_set_stroke_color(ctx, COL_RULE);
      graphics_draw_rect(ctx, bar);
    }
  }
}

int16_t ui_draw_status(GContext *ctx, GRect bounds, const RideState *r,
                       bool in_ride) {
  const int16_t y = bounds.origin.y;
  const int16_t pad = PBL_IF_RECT_ELSE(4, 22);   // round screens lose the corners
  GFont lf = ui_font_label();

  // Two different faults, told apart because the fixes differ: NO PHONE means
  // Bluetooth is down and the watch is out of the loop entirely; NO DATA means
  // the phone is connected but has stopped sending, which points at the
  // companion app rather than at the radio.
  const char *fault = NULL;
  if (in_ride) {
    if (!r->linked)             fault = "NO PHONE";
    else if (ride_data_stale()) fault = "NO DATA";
  }

  if (fault) {
    // One unambiguous message beats a row of indicators describing numbers we
    // are no longer receiving.
    graphics_context_set_text_color(ctx, COL_ACCENT);
    graphics_draw_text(ctx, fault, lf,
                       GRect(bounds.origin.x, y + 2, bounds.size.w, LABEL_H),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  } else {
    ui_draw_fix(ctx, GPoint(bounds.origin.x + pad, y + 5), r->fix);

    if (in_ride) {
      const char *word = (r->state == STATE_PAUSED) ? "PAUSED" : "REC";
      graphics_context_set_text_color(ctx,
          r->state == STATE_PAUSED ? COL_ACCENT : COL_INK);
      graphics_draw_text(ctx, word, lf,
                         GRect(bounds.origin.x, y + 2, bounds.size.w, LABEL_H),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }

    if (r->phone_batt <= 100) {
      char batt[8];
      snprintf(batt, sizeof(batt), "%u%%", (unsigned)r->phone_batt);
      graphics_context_set_text_color(ctx, COL_MUTED);
      graphics_draw_text(ctx, batt, lf,
                         GRect(bounds.origin.x, y + 2, bounds.size.w - pad, LABEL_H),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    }
  }

  int16_t rule_y = y + STATUS_H - 1;
  graphics_context_set_stroke_color(ctx, COL_RULE);
  graphics_draw_line(ctx, GPoint(bounds.origin.x, rule_y),
                     GPoint(bounds.origin.x + bounds.size.w, rule_y));

  return y + STATUS_H;
}
