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
  // Bitham 42 Bold, and NOT the 49px Roboto Bold Subset, tempting as those
  // extra seven pixels are.
  //
  // "Subset" means digits and a little punctuation -- no decimal point. It
  // renders 23.7 as "237", silently, which is not a cosmetic problem: it is a
  // speed readout off by a factor of ten. The same trap applies to the LECO
  // and Bitham *_NUMBERS faces. Any font used here must carry '.' and ':',
  // which in practice means a full one.
#if defined(PBL_PLATFORM_EMERY)
  return fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
#else
  return fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
#endif
}

GFont ui_font_value(void) {
  // Bitham Black rather than Gothic Bold: heavier strokes survive being read
  // at arm's length on a bouncing handlebar, which is the whole job.
#if defined(PBL_PLATFORM_EMERY)
  return fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
#else
  return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
#endif
}

GFont ui_font_label(void) {
  // Bold, and a size up from the old Gothic 14. A label you have to squint at
  // is not a label, it is decoration.
#if defined(PBL_PLATFORM_EMERY)
  return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
#else
  return fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
#endif
}

// ---------------------------------------------------------------------------
// Metric block
// ---------------------------------------------------------------------------

#define LABEL_H  PBL_IF_RECT_ELSE(20, 18)

void ui_draw_metric(GContext *ctx, GRect box, const char *label,
                    const char *value, const char *unit,
                    GFont value_font, GColor label_colour, GColor value_colour) {
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

  graphics_context_set_text_color(ctx, label_colour);
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

void ui_fill(GContext *ctx, GRect box, GColor colour, int16_t radius) {
  graphics_context_set_fill_color(ctx, colour);
  graphics_fill_rect(ctx, box, radius,
                     radius > 0 ? GCornersAll : GCornerNone);
}

#define FIX_BAR_W    PBL_IF_RECT_ELSE(4, 3)
#define FIX_BAR_GAP  2
#define FIX_MAX_H    12

void ui_draw_fix(GContext *ctx, GPoint origin, uint8_t fix, GColor colour) {
  for (int i = 0; i < 3; i++) {
    int16_t h = 5 + i * 4;
    GRect bar = GRect(origin.x + i * (FIX_BAR_W + FIX_BAR_GAP),
                      origin.y + (FIX_MAX_H - h),
                      FIX_BAR_W, h);
    if (fix > i) {
      graphics_context_set_fill_color(ctx, colour);
      graphics_fill_rect(ctx, bar, 0, GCornerNone);
    } else {
      // Hollow, not absent: three outlines make it obvious the indicator has
      // three levels and none are lit, where drawing nothing reads as a bug.
      graphics_context_set_stroke_color(ctx, colour);
      graphics_draw_rect(ctx, bar);
    }
  }
}

int16_t ui_draw_status(GContext *ctx, GRect bounds, const RideState *r,
                       bool in_ride) {
  const int16_t y = bounds.origin.y;
  const int16_t pad = PBL_IF_RECT_ELSE(5, 24);   // round screens lose the corners
  GFont lf = ui_font_label();

  // A solid band of accent, not a line of grey text over a hairline rule. The
  // status of the ride is the thing you check at a glance without focusing,
  // and a block of colour is readable from further away than any typeface.
  GRect band = GRect(bounds.origin.x, y, bounds.size.w, STATUS_H);
  ui_fill(ctx, band, COL_ACCENT, 0);

  GColor on = theme_on_accent();

  // Two different faults, told apart because the fixes differ: NO PHONE means
  // Bluetooth is down and the watch is out of the loop entirely; NO DATA means
  // the phone is connected but has stopped sending, which points at the
  // companion app rather than at the radio.
  const char *word = NULL;
  if (in_ride) {
    if (!r->linked)             word = "NO PHONE";
    else if (ride_data_stale()) word = "NO DATA";
    else if (r->state == STATE_PAUSED) word = "PAUSED";
    else                        word = "REC";
  }

  // A fault replaces the indicators outright. Showing GPS bars beside the
  // words NO DATA would be describing numbers we are no longer receiving.
  bool fault = in_ride && (!r->linked || ride_data_stale());

  if (!fault) {
    ui_draw_fix(ctx, GPoint(bounds.origin.x + pad, y + (STATUS_H - FIX_MAX_H) / 2),
                r->fix, on);

    if (r->phone_batt <= 100) {
      char batt[8];
      snprintf(batt, sizeof(batt), "%u%%", (unsigned)r->phone_batt);
      graphics_context_set_text_color(ctx, on);
      graphics_draw_text(ctx, batt, lf,
                         GRect(bounds.origin.x, y + 1, bounds.size.w - pad, LABEL_H),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    }
  }

  if (word) {
    graphics_context_set_text_color(ctx, on);
    graphics_draw_text(ctx, word, lf,
                       GRect(bounds.origin.x, y + 1, bounds.size.w, LABEL_H),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  return y + STATUS_H;
}
