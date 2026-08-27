#include "ui.h"
#include "comm.h"
#include "fmt.h"

// The idle screen. Its job is to answer one question before you clip in --
// will a ride I start right now actually be recorded? -- and then get out of
// the way. Everything else on it is secondary.

static Window *s_window;
static Layer  *s_canvas;

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const RideState *r = ride_get();

  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // in_ride = false: no state word, and the headline below owns the link state.
  int16_t top = ui_draw_status(ctx, bounds, r, false);
  int16_t rest = bounds.size.h - (top - bounds.origin.y);
  const int16_t pad = PBL_IF_RECT_ELSE(4, 20);

  // ---- Readiness, stated in words -------------------------------------
  //
  // The status bar already carries the fix bars, but three small bars are a
  // poor thing to squint at through sunglasses when the question is binary.
  const char *headline;
  GColor headline_col = COL_ACCENT;

  if (r->phone_version != 0 && r->phone_version != PROTOCOL_VERSION) {
    // Version trouble outranks GPS: starting a ride against a companion that
    // speaks a different protocol will misreport rather than simply fail.
    headline = "APP MISMATCH";
  } else if (!r->linked) {
    headline = "NO PHONE";
  } else if (r->fix == FIX_NONE) {
    headline = "NO GPS";
  } else if (r->fix == FIX_POOR) {
    headline = "WEAK GPS";
  } else {
    headline = "READY";
    headline_col = COL_INK;
  }

  int16_t head_h = rest * 34 / 100;
  ui_draw_metric(ctx, GRect(bounds.origin.x, top, bounds.size.w, head_h),
                 "VELO", headline, NULL, ui_font_value(), headline_col);

  graphics_context_set_stroke_color(ctx, COL_RULE);
  graphics_draw_line(ctx, GPoint(bounds.origin.x, top + head_h),
                     GPoint(bounds.origin.x + bounds.size.w, top + head_h));

  // ---- Last ride -------------------------------------------------------
  int16_t last_y = top + head_h;
  int16_t last_h = rest * 40 / 100;
  const RideSummary *last = ride_history_at(0);

  if (last) {
    char dist[16], date[20], value[24], label[32];
    fmt_distance(dist, sizeof(dist), last->distance_m, ride_imperial());
    fmt_date(date, sizeof(date), last->start_time);

    // The unit goes on the value here, not on the label as it does in the
    // metric grid. This label is already a phrase -- "LAST Tue 25 Aug KM"
    // reads as a garbled sentence, where "42.73 KM" reads as a distance.
    snprintf(value, sizeof(value), "%s %s", dist,
             fmt_distance_unit(ride_imperial()));
    snprintf(label, sizeof(label), "LAST %s", date);

    ui_draw_metric(ctx, GRect(bounds.origin.x, last_y, bounds.size.w, last_h),
                   label, value, NULL, ui_font_value(), COL_INK);
  } else {
    graphics_context_set_text_color(ctx, COL_MUTED);
    graphics_draw_text(ctx, "No rides yet", ui_font_label(),
                       GRect(bounds.origin.x + pad, last_y + last_h / 2 - 10,
                             bounds.size.w - 2 * pad, 20),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
  }

  // ---- Hints -----------------------------------------------------------
  int16_t hint_y = bounds.origin.y + bounds.size.h - 20;
  graphics_context_set_text_color(ctx, COL_MUTED);
  graphics_draw_text(ctx,
                     ride_history_count() > 0 ? "SELECT ride   UP history"
                                              : "SELECT to ride",
                     ui_font_label(),
                     GRect(bounds.origin.x + pad, hint_y,
                           bounds.size.w - 2 * pad, 18),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

static void select_click(ClickRecognizerRef rec, void *ctx) {
  // Started unconditionally, even with no fix and no phone. The alternative --
  // refusing to start until GPS is happy -- means standing at the roadside
  // waiting for a bar to light, and the phone will backfill the track from the
  // moment it does get a fix anyway.
  ride_local_start();
  comm_send_cmd(CMD_START, 0);
  vibes_short_pulse();
  ui_ride_push();
}

static void up_click(ClickRecognizerRef rec, void *ctx) {
  if (ride_history_count() > 0) {
    ui_history_push();
  }
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

void ui_ready_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_background_color(s_window, COL_BG);
    window_set_click_config_provider(s_window, click_config);
    window_set_window_handlers(s_window, (WindowHandlers) {
      .load = window_load,
      .unload = window_unload,
    });
  }
  window_stack_push(s_window, true);
}

void ui_ready_update(void) {
  if (s_canvas) {
    layer_mark_dirty(s_canvas);
  }
}

bool ui_ready_is_top(void) {
  return s_window && window_stack_get_top_window() == s_window;
}

// The ready window is the root of the stack and, unlike the others, is not
// destroyed in its own unload handler -- it gets unloaded every time another
// screen covers it. So it is torn down here, at app exit, instead.
void ui_ready_deinit(void) {
  if (s_window) {
    window_destroy(s_window);
    s_window = NULL;
  }
}
