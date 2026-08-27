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

// Every metric can take the hero slot, on every platform. The screen shows
// three at a time rather than five, so anything not currently visible has to
// be reachable by cycling or it may as well not exist.
#define HERO_COUNT  METRIC_COUNT

// Two secondary cells, side by side, and that is the lot.
//
// This used to be a 2x2 grid of four, which fitted but meant five numbers on
// a 200px screen in type small enough to need looking *at* rather than
// glancing at. Three big ones beat five cramped ones on a bouncing handlebar.
#define GRID_CELLS  2

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

// Which metrics fill the two cells: the next ones after the hero, wrapping.
// Keeping the hero out avoids showing the same number twice at two sizes, and
// walking forward from it means cycling the hero rotates the whole set rather
// than reshuffling it unpredictably.
static void grid_metrics(Metric out[GRID_CELLS]) {
  for (int n = 0; n < GRID_CELLS; n++) {
    out[n] = (Metric)((s_hero + 1 + n) % METRIC_COUNT);
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

  // The ride screen has no footer, so on a rect display it runs to the bottom
  // edge. On a round one that is exactly where the chord collapses to nothing,
  // and the bottom cell row fitted to it would come out zero pixels wide and
  // simply not be drawn. Give the last row somewhere to live.
  //
  // A sixth rather than an eighth, counter-intuitively, to make that row WIDER:
  // reserving more at the rim lifts the row toward the middle of the glass,
  // where the chord is longer. At an eighth chalk's cells came out 57px and
  // ellipsised "AVG KM/H" to "AVG KM..."; at a sixth they are about 65px.
  int16_t rest   = bounds.size.h - (top - bounds.origin.y)
                   - PBL_IF_RECT_ELSE(0, bounds.size.h / 6);
  int16_t hero_h = rest * 52 / 100;   // the hero gets over half the screen
  int16_t cell_h = rest - hero_h;
  int16_t col_w  = bounds.size.w / GRID_CELLS;

  const char *label, *unit;
  char value[16];

  // The hero is a filled panel, not text on the background. This is the one
  // number you read while moving, and a block of colour finds your eye before
  // you have focused on anything.
  //
  // theme_on_accent() rather than a fixed white: the accent is the rider's
  // choice, and on a pale one white text would vanish. It also keeps diorite
  // honest, where the accent collapses to black.
  GRect hero = ui_fit_round(
      GRect(bounds.origin.x + PANEL_INSET, top + PANEL_GAP,
            bounds.size.w - 2 * PANEL_INSET, hero_h - PANEL_GAP - 2), bounds);
  ui_fill(ctx, hero, COL_ACCENT, PANEL_RADIUS);

  GColor on = theme_on_accent();
  render_metric((Metric)s_hero, r, &label, value, sizeof(value), &unit);
  ui_draw_metric(ctx, hero, label, value, unit, ui_font_hero(), on, on);

  Metric cells[GRID_CELLS];
  grid_metrics(cells);

  // No divider between the cells, and no rules anywhere. Whitespace separates
  // them perfectly well at this size, and every hairline removed is one less
  // thing competing with the numbers for attention.
  // The row is fitted as a whole and then divided, rather than each cell being
  // fitted on its own: fitting them separately would pull each toward the
  // centre by a different amount and the columns would stop lining up.
  GRect grid = ui_fit_round(
      GRect(bounds.origin.x, top + hero_h, bounds.size.w, cell_h), bounds);
  col_w = grid.size.w / GRID_CELLS;

  for (int i = 0; i < GRID_CELLS; i++) {
    GRect cell = GRect(grid.origin.x + i * col_w, grid.origin.y, col_w, cell_h);
    render_metric(cells[i], r, &label, value, sizeof(value), &unit);
    ui_draw_metric(ctx, cell, label, value, unit, ui_font_value(),
                   COL_MUTED, COL_INK);
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
