#include "ui.h"
#include "comm.h"
#include "fmt.h"

// The live ride screen: a status bar, one hero metric, and a grid of
// secondaries. UP/DOWN move a different metric into the hero slot, so the
// number you care about on this particular ride is the one rendered large.

typedef enum {
  M_SPEED = 0,
  M_DIST,
  M_TIME,
  M_AVG,
  M_ASCENT,
} Metric;

#define METRIC_COUNT  5

// On a rectangular screen only these three are worth the hero slot; average
// and ascent are reference numbers you glance at, not ones you ride to, and
// the grid below shows them permanently.
//
// Round screens cannot carry that grid -- it has two cells, not four -- so
// there the hero cycles all five instead. Otherwise ascent and average would
// be unreachable on chalk: never in the grid, never promotable to hero.
#define HERO_COUNT  PBL_IF_RECT_ELSE(3, METRIC_COUNT)

// Round screens cannot carry a 2x2 grid -- the corner cells fall off the
// curve -- so they show a single column of two.
#define GRID_COLS  PBL_IF_RECT_ELSE(2, 1)
#define GRID_CELLS PBL_IF_RECT_ELSE(4, 2)

static Window  *s_window;
static Layer   *s_canvas;
static uint8_t  s_hero;

// ---------------------------------------------------------------------------
// Metric rendering
// ---------------------------------------------------------------------------

static void render_metric(Metric m, const RideState *r,
                          const char **label, char *value, size_t vn,
                          const char **unit) {
  bool imp = ride_imperial();

  switch (m) {
    case M_SPEED:
      *label = "SPEED";
      *unit  = fmt_speed_unit(imp);
      fmt_speed(value, vn, r->speed_cms, imp);
      break;

    case M_DIST:
      *label = "DIST";
      *unit  = fmt_distance_unit(imp);
      fmt_distance(value, vn, r->distance_m, imp);
      break;

    case M_TIME:
      *label = "TIME";
      *unit  = NULL;
      fmt_duration(value, vn, r->moving_s);
      break;

    case M_AVG: {
      *label = "AVG";
      *unit  = fmt_speed_unit(imp);
      // Average over moving time, not wall time, so stopping at a café does
      // not slowly eat the number you spent the morning earning.
      //
      // distance_m * 100 stays inside uint32 up to 42,949 km, which is rather
      // more than a Pebble battery will see in one ride.
      uint16_t avg_cms = 0;
      if (r->moving_s > 0) {
        avg_cms = (uint16_t)((r->distance_m * 100u) / r->moving_s);
      }
      fmt_speed(value, vn, avg_cms, imp);
      break;
    }

    case M_ASCENT:
      *label = "ASCENT";
      *unit  = fmt_ascent_unit(imp);
      fmt_ascent(value, vn, r->ascent_m, imp);
      break;
  }
}

// Which metrics fill the grid: the first GRID_CELLS of the full list that are
// not currently the hero. Keeping the hero out avoids showing the same number
// twice at two different sizes.
//
// On a rectangle that lands on exactly the old behaviour -- the two remaining
// of speed/dist/time, then average and ascent.
static void grid_metrics(Metric out[GRID_CELLS]) {
  int n = 0;
  for (int m = 0; m < METRIC_COUNT && n < GRID_CELLS; m++) {
    if (m != s_hero) {
      out[n++] = (Metric)m;
    }
  }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const RideState *r = ride_get();

  graphics_context_set_fill_color(ctx, COL_BG);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  int16_t top = ui_draw_status(ctx, bounds, r, true);

  int16_t rest    = bounds.size.h - (top - bounds.origin.y);
  int16_t hero_h  = rest * 38 / 100;
  int16_t grid_h  = rest - hero_h;
  int16_t rows    = GRID_CELLS / GRID_COLS;
  int16_t row_h   = grid_h / rows;
  int16_t col_w   = bounds.size.w / GRID_COLS;

  const char *label, *unit;
  char value[16];

  render_metric((Metric)s_hero, r, &label, value, sizeof(value), &unit);
  ui_draw_metric(ctx, GRect(bounds.origin.x, top, bounds.size.w, hero_h),
                 label, value, unit, ui_font_hero(), COL_ACCENT);

  graphics_context_set_stroke_color(ctx, COL_RULE);
  graphics_draw_line(ctx, GPoint(bounds.origin.x, top + hero_h),
                     GPoint(bounds.origin.x + bounds.size.w, top + hero_h));

  Metric cells[GRID_CELLS];
  grid_metrics(cells);

  for (int i = 0; i < GRID_CELLS; i++) {
    int16_t cx = bounds.origin.x + (i % GRID_COLS) * col_w;
    int16_t cy = top + hero_h + (i / GRID_COLS) * row_h;

    render_metric(cells[i], r, &label, value, sizeof(value), &unit);
    ui_draw_metric(ctx, GRect(cx, cy, col_w, row_h),
                   label, value, unit, ui_font_value(), COL_INK);
  }

  // Grid rules, drawn after the cells so they sit on top of any overshoot.
  graphics_context_set_stroke_color(ctx, COL_RULE);
  for (int c = 1; c < GRID_COLS; c++) {
    graphics_draw_line(ctx, GPoint(bounds.origin.x + c * col_w, top + hero_h),
                       GPoint(bounds.origin.x + c * col_w, bounds.origin.y + bounds.size.h));
  }
  for (int rw = 1; rw < rows; rw++) {
    graphics_draw_line(ctx, GPoint(bounds.origin.x, top + hero_h + rw * row_h),
                       GPoint(bounds.origin.x + bounds.size.w, top + hero_h + rw * row_h));
  }
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

static void select_click(ClickRecognizerRef rec, void *ctx) {
  RideState *r = ride_get();
  if (r->state == STATE_RECORDING) {
    ride_local_pause();
    comm_send_cmd(CMD_PAUSE, 0);
    vibes_short_pulse();
  } else if (r->state == STATE_PAUSED) {
    ride_local_resume();
    comm_send_cmd(CMD_RESUME, 0);
    vibes_short_pulse();
  }
  layer_mark_dirty(s_canvas);
}

static void cycle_hero(int delta) {
  s_hero = (uint8_t)((s_hero + HERO_COUNT + delta) % HERO_COUNT);
  layer_mark_dirty(s_canvas);
}

static void up_click(ClickRecognizerRef rec, void *ctx)   { cycle_hero(-1); }
static void down_click(ClickRecognizerRef rec, void *ctx) { cycle_hero(+1); }

// Finishing lives on a long SELECT, not on the back button.
//
// Holding back is a firmware-level "quit the app" gesture that a watchapp does
// not get to override -- subscribing to it looks like it works right up until
// the app closes underneath you. So back keeps its ordinary meaning here: it
// leaves the watchapp, and the ride carries on, because the phone's foreground
// service is what is actually recording. Re-opening Velo resyncs and lands
// straight back on this screen.
static void select_long_click(ClickRecognizerRef rec, void *ctx) {
  RideSummary summary;
  bool kept = ride_local_stop(&summary);
  comm_send_cmd(CMD_STOP, summary.moving_s);
  vibes_double_pulse();

  // Replace this window rather than stacking on it: the ride is over, so
  // backing out of the summary should reach the ready screen, not a frozen
  // ride screen showing numbers that no longer update.
  window_stack_remove(s_window, false);
  ui_summary_push(kept ? &summary : NULL, false);
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, select_long_click, NULL);
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
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

// Note what this does NOT do: destroy the window.
//
// unload runs synchronously from inside window_stack_remove/pop, and those are
// called from this window's own click handler. Freeing the Window there frees
// the ClickRecognizer the firmware is still dispatching through -- a
// use-after-free that happens to survive in testing. The window is cached
// instead and torn down in ui_ride_deinit() at app exit.
static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

void ui_ride_push(void) {
  if (s_window && window_stack_contains_window(s_window)) {
    return;   // already up; pushing twice would stack two ride screens
  }

  if (!s_window) {
    s_window = window_create();
    window_set_click_config_provider(s_window, click_config);
    window_set_window_handlers(s_window, (WindowHandlers) {
      .load = window_load,
      .unload = window_unload,
    });
  }

  // Reset the hero on every fresh push, not at creation: the window now
  // outlives the ride, so a cached one would still be showing the last ride's
  // chosen metric.
  s_hero = M_SPEED;
  window_set_background_color(s_window, COL_BG);
  window_stack_push(s_window, true);
}

void ui_ride_deinit(void) {
  if (s_window) {
    window_destroy(s_window);
    s_window = NULL;
  }
}

void ui_ride_update(void) {
  if (s_canvas) {
    layer_mark_dirty(s_canvas);
  }
}

void ui_ride_dismiss(void) {
  if (s_window) {
    // Removing rather than popping: the ride window may not be on top (the
    // rider could be browsing history when the phone ends the ride), and
    // popping would tear off whatever is above it instead.
    window_stack_remove(s_window, false);
  }
}
