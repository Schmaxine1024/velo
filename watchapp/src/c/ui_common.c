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
#elif defined(PBL_PLATFORM_CHALK)
  // Bitham 30 needs 48px of block once the label line is counted, and chalk's
  // headline block is nowhere near that: 180px of height, less the status band
  // and a hint block that cannot sit on the rim, leaves about 100px to divide
  // between the headline and the last ride. Gothic 24 is the largest that fits
  // without the value's ink running down into the block beneath it.
  return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
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

GFont ui_font_summary_value(void) {
  // The summary packs a label and a value into a third of the screen, and the
  // selected cell draws them inside an accent chip. On the 144x168 boards and
  // chalk that cell is only 40px tall, so ui_font_value()'s Gothic 24 renders a
  // value whose ink runs past the bottom of the chip -- and because the text is
  // theme_on_accent() white on a white background, the overhang does not clip,
  // it disappears. Emery's cell is 60px and has no such problem, so it keeps
  // the heavier face.
  //
  // Full charset, not a *_NUMBERS subset: see ui_font_hero for why a font
  // without '.' turns 1.87 into 187.
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  // 200x228 and 260x260 respectively: both have rows deep enough for the
  // heavier face, and on gabbro anything smaller looks lost.
  return fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
#elif defined(PBL_PLATFORM_CHALK)
  // The tightest board there is. 180px of height, of which the status band and
  // the hint block take a third, leaves rows around 33px for a label and a
  // value -- and the bottom of a round display cannot be used to claw any of it
  // back. Gothic 14 is the largest face that fits inside the chip here.
  return fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
#else
  return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
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
// Round displays
// ---------------------------------------------------------------------------

#if defined(PBL_ROUND)
// Integer square root. No math.h: pulling in the float sqrt for one call per
// panel costs code space on a watch that has little, and the inputs here are
// small enough that Newton converges in a handful of iterations.
static int32_t isqrt32(int32_t v) {
  if (v <= 0) {
    return 0;
  }
  int32_t x = v;
  int32_t y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + v / x) / 2;
  }
  return x;
}
#endif

GRect ui_fit_round(GRect box, GRect bounds) {
#if defined(PBL_ROUND)
  // A box that is perfectly legal on a rect screen can have its corners hanging
  // off a circular one, where they are simply not drawn -- which is how a
  // rounded panel ends up rendered as a dome. PANEL_INSET cannot fix this on
  // its own, because the usable width of a round display is not constant: it
  // narrows as you move away from the vertical centre.
  //
  // The binding constraint is whichever horizontal edge of the box sits farther
  // from that centre, since that is where the chord is shortest. Fit to it and
  // the whole box is inside the glass.
  const int16_t cx = bounds.origin.x + bounds.size.w / 2;
  const int16_t cy = bounds.origin.y + bounds.size.h / 2;
  const int16_t r  = bounds.size.w / 2;

  int16_t d1 = box.origin.y - cy;
  int16_t d2 = box.origin.y + box.size.h - cy;
  if (d1 < 0) d1 = -d1;
  if (d2 < 0) d2 = -d2;
  const int16_t dy = (d1 > d2) ? d1 : d2;

  if (dy >= r) {
    return GRect(cx, box.origin.y, 0, box.size.h);
  }

  // Two pixels of margin: the chord is the mathematical edge of the circle, and
  // a box drawn exactly to it still lands on the rim the display rounds off.
  int16_t half = (int16_t)isqrt32((int32_t)r * r - (int32_t)dy * dy) - 2;
  if (half < 0) {
    half = 0;
  }

  if (box.origin.x >= cx - half && box.origin.x + box.size.w <= cx + half) {
    return box;
  }
  return GRect(cx - half, box.origin.y, half * 2, box.size.h);
#else
  // Rect screens have no corners to lose.
  (void)bounds;
  return box;
#endif
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

  // Where the band's contents sit. On a rect screen they are centred in it, as
  // they always were. On a round one they are pushed to the bottom of the band
  // and fitted to the chord there: near the top of the glass the display is far
  // narrower than it is wide, so indicators placed at a fixed inset from the
  // edge are not merely tight, they are off the screen and never drawn at all.
#if defined(PBL_ROUND)
  const int16_t row_y = y + STATUS_H - LABEL_H - 2;
#else
  const int16_t row_y = y + 1;
#endif
  const GRect row = ui_fit_round(
      GRect(bounds.origin.x, row_y, bounds.size.w, LABEL_H), bounds);
  const int16_t ipad = PBL_IF_RECT_ELSE(pad, 4);
  const int16_t bar_y = PBL_IF_RECT_ELSE(y + (STATUS_H - FIX_MAX_H) / 2,
                                         row_y + (LABEL_H - FIX_MAX_H) / 2);

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
    ui_draw_fix(ctx, GPoint(row.origin.x + ipad, bar_y), r->fix, on);

    // The battery is dropped on round whenever a status word is showing. The
    // fitted row is about 92px on chalk; "PAUSED" centred in that runs from 22
    // to 70 and a right-aligned "76%" starts around 64, so the two overlap and
    // render as "PAUSED6%". The word is the one you need mid-ride, so it wins.
    if (r->phone_batt <= 100 && PBL_IF_RECT_ELSE(true, word == NULL)) {
      char batt[8];
      snprintf(batt, sizeof(batt), "%u%%", (unsigned)r->phone_batt);
      graphics_context_set_text_color(ctx, on);
      graphics_draw_text(ctx, batt, lf,
                         GRect(row.origin.x, row_y, row.size.w - ipad, LABEL_H),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    }
  }

  if (word) {
    graphics_context_set_text_color(ctx, on);
    graphics_draw_text(ctx, word, lf,
                       GRect(row.origin.x, row_y, row.size.w, LABEL_H),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  return y + STATUS_H;
}
