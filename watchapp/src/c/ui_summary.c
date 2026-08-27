#include "ui.h"
#include "fmt.h"

// Shown when a ride ends, and when a history row is opened. Same screen both
// times -- a finished ride is a finished ride -- with only the footer hint and
// the header differing.

static Window     *s_window;
static Layer      *s_canvas;
static RideSummary s_summary;
static bool        s_have;
static bool        s_from_history;

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int16_t pad = PBL_IF_RECT_ELSE(4, 20);

  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // A ride the watch never received a single fix for. This is reachable: the
  // clock only advances while the phone is linked, so start-then-stop with the
  // companion closed produces a genuinely empty ride. Saying so plainly beats
  // rendering a grid of zeroes that looks like a recording failure.
  if (!s_have) {
    // Deliberately not "the phone was not connected": since link state and
    // data freshness became separate things, the commonest way to reach this
    // screen is a phone that is connected but whose companion app never sent
    // anything. Naming the symptom covers both causes without guessing.
    const char *head = "Nothing recorded";
    const char *body = "No data came from the phone";

    GFont hf = ui_font_value();
    GFont bf = ui_font_label();

    int16_t w = bounds.size.w - 2 * pad;
    GRect measure = GRect(0, 0, w, bounds.size.h);

    // Measured and stacked rather than placed at fixed offsets. The old
    // version put the body at a hard-coded +46 from a third of the way down,
    // which was tuned against the small label font -- once the type grew, the
    // heading wrapped to two lines and the two ran into each other.
    GSize hs = graphics_text_layout_get_content_size(
        head, hf, measure, GTextOverflowModeWordWrap, GTextAlignmentCenter);
    GSize bs = graphics_text_layout_get_content_size(
        body, bf, measure, GTextOverflowModeWordWrap, GTextAlignmentCenter);

    const int16_t gap = 12;
    int16_t total = hs.h + gap + bs.h;
    int16_t y = bounds.origin.y + (bounds.size.h - total) / 2;

    graphics_context_set_text_color(ctx, COL_ACCENT);
    graphics_draw_text(ctx, head, hf,
                       GRect(bounds.origin.x + pad, y, w, hs.h + 4),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

    graphics_context_set_text_color(ctx, COL_MUTED);
    graphics_draw_text(ctx, body, bf,
                       GRect(bounds.origin.x + pad, y + hs.h + gap, w, bs.h + 4),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  bool imp = ride_imperial();

  // ---- Header: which ride this was --------------------------------------
  char date[20];
  fmt_date(date, sizeof(date), s_summary.start_time);

  graphics_context_set_text_color(ctx, COL_INK);
  graphics_draw_text(ctx, date, ui_font_label(),
                     GRect(bounds.origin.x, bounds.origin.y + 3, bounds.size.w, FOOTER_H),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);


  // ---- Five numbers ------------------------------------------------------
  //
  // Distance and time lead because they are what you tell people. Average and
  // max pair naturally on one row. Ascent gets the full width at the bottom
  // rather than a half cell with an empty twin beside it.
  int16_t top    = bounds.origin.y + STATUS_H;
  // The footer reserve has to match the label font's line height, not the
  // 18px it used to be -- the labels went up a size and "Saved" lost its
  // descender off the bottom of the screen.
  int16_t rest   = bounds.size.h - STATUS_H - FOOTER_H;
  int16_t row_h  = rest / 3;
  int16_t col_w  = bounds.size.w / 2;

  char dist[16], dur[16], avg[16], mx[16], asc[16];
  fmt_distance(dist, sizeof(dist), s_summary.distance_m, imp);
  fmt_duration(dur, sizeof(dur), s_summary.moving_s);
  fmt_ascent(asc, sizeof(asc), s_summary.ascent_m, imp);
  fmt_speed(mx, sizeof(mx), s_summary.max_speed_cms, imp);

  uint16_t avg_cms = 0;
  if (s_summary.moving_s > 0) {
    avg_cms = (uint16_t)((s_summary.distance_m * 100u) / s_summary.moving_s);
  }
  fmt_speed(avg, sizeof(avg), avg_cms, imp);

  // Inset within its cell so the card has air around it and does not run into
  // the date above or the edge of the screen beside it.
  GRect dist_box = GRect(bounds.origin.x + PANEL_INSET, top + 3,
                         col_w - 2 * PANEL_INSET, row_h - 6);
  ui_fill(ctx, dist_box, COL_ACCENT, PANEL_RADIUS);
  ui_draw_metric(ctx, dist_box, "DIST", dist, fmt_distance_unit(imp),
                 ui_font_value(), theme_on_accent(), theme_on_accent());
  ui_draw_metric(ctx, GRect(bounds.origin.x + col_w, top, col_w, row_h),
                 "TIME", dur, NULL, ui_font_value(), COL_MUTED, COL_INK);

  ui_draw_metric(ctx, GRect(bounds.origin.x, top + row_h, col_w, row_h),
                 "AVG", avg, fmt_speed_unit(imp), ui_font_value(), COL_MUTED, COL_INK);
  ui_draw_metric(ctx, GRect(bounds.origin.x + col_w, top + row_h, col_w, row_h),
                 "MAX", mx, fmt_speed_unit(imp), ui_font_value(), COL_MUTED, COL_INK);

  ui_draw_metric(ctx, GRect(bounds.origin.x, top + 2 * row_h, bounds.size.w, row_h),
                 "ASCENT", asc, fmt_ascent_unit(imp), ui_font_value(), COL_MUTED, COL_INK);

  // ---- Footer ------------------------------------------------------------
  graphics_context_set_text_color(ctx, COL_MUTED);
  // "Saved" is a claim the watch can actually back: ride_local_stop() has just
  // written this into persistent storage. For a ride opened from the history
  // list that is old news, so the footer offers the way out instead.
  graphics_draw_text(ctx,
                     s_from_history ? "SELECT to close" : "Saved",
                     ui_font_label(),
                     GRect(bounds.origin.x + pad,
                           bounds.origin.y + bounds.size.h - FOOTER_H,
                           bounds.size.w - 2 * pad, FOOTER_H),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void select_click(ClickRecognizerRef rec, void *ctx) {
  window_stack_pop(true);
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

// Does not destroy the window -- see the note in ui_ride.c's window_unload.
// select_click pops this window from inside its own click handler, so freeing
// it here would free the recognizer mid-dispatch.
static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

void ui_summary_push(const RideSummary *summary, bool from_history) {
  // Copied, not aliased: the caller's pointer is either a stack local from
  // ride_local_stop() or an entry in the history array, and the latter shifts
  // under us the moment another ride is saved.
  s_have = (summary != NULL);
  if (s_have) {
    s_summary = *summary;
  }
  s_from_history = from_history;

  if (s_window && window_stack_contains_window(s_window)) {
    // Already showing one -- just repoint it at the new ride. Pushing a second
    // summary window would make BACK walk through a stack of them.
    if (s_canvas) layer_mark_dirty(s_canvas);
    return;
  }

  if (!s_window) {
    s_window = window_create();
    window_set_click_config_provider(s_window, click_config);
    window_set_window_handlers(s_window, (WindowHandlers) {
      .load = window_load,
      .unload = window_unload,
    });
  }

  window_set_background_color(s_window, COL_BG);
  window_stack_push(s_window, true);
}

void ui_summary_deinit(void) {
  if (s_window) {
    window_destroy(s_window);
    s_window = NULL;
  }
}
