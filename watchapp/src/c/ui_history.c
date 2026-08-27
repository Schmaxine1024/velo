#include "ui.h"
#include "fmt.h"

// The last fifteen rides, newest first, plus a way to forget them.
//
// A MenuLayer rather than hand-drawn rows: it brings scrolling, selection and
// the round-screen row centring for free, and this is the one screen where the
// system look is the right look.

#define SECTION_RIDES  0
#define SECTION_CLEAR  1
#define SECTION_COUNT  2

static Window    *s_window;
static MenuLayer *s_menu;

static uint16_t num_sections(MenuLayer *menu, void *ctx) {
  return SECTION_COUNT;
}

static uint16_t num_rows(MenuLayer *menu, uint16_t section, void *ctx) {
  return (section == SECTION_RIDES) ? ride_history_count() : 1;
}

static int16_t header_height(MenuLayer *menu, uint16_t section, void *ctx) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *c) {
  menu_cell_basic_header_draw(ctx, cell,
      (section == SECTION_RIDES) ? "Rides" : "Manage");
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *c) {
  if (idx->section == SECTION_CLEAR) {
    menu_cell_basic_draw(ctx, cell, "Clear history", "Hold SELECT", NULL);
    return;
  }

  const RideSummary *s = ride_history_at((uint8_t)idx->row);
  if (!s) {
    return;
  }

  bool imp = ride_imperial();
  // subtitle holds "<dist> <unit>   <duration>": two 15-char fields plus a
  // 4-char unit and separators. Sized so the compiler can prove no truncation
  // rather than sized to the strings these actually produce.
  char title[24], subtitle[48], dist[16], dur[16];

  fmt_date(title, sizeof(title), s->start_time);
  fmt_distance(dist, sizeof(dist), s->distance_m, imp);
  fmt_duration(dur, sizeof(dur), s->moving_s);
  snprintf(subtitle, sizeof(subtitle), "%s %s   %s",
           dist, fmt_distance_unit(imp), dur);

  menu_cell_basic_draw(ctx, cell, title, subtitle, NULL);
}

static void select_click(MenuLayer *menu, MenuIndex *idx, void *c) {
  if (idx->section == SECTION_RIDES) {
    const RideSummary *s = ride_history_at((uint8_t)idx->row);
    if (s) {
      ui_summary_push(s, true);
    }
  }
  // The clear row deliberately ignores a short press -- see select_long_click.
}

static void select_long_click(MenuLayer *menu, MenuIndex *idx, void *c) {
  if (idx->section != SECTION_CLEAR) {
    return;
  }
  // Long press is the confirmation. A modal yes/no would be more explicit, but
  // this is fifteen summaries the phone still holds in full, so the cost of
  // getting it wrong is a screen that refills on the next ride.
  ride_history_clear();
  vibes_short_pulse();
  menu_layer_reload_data(s_menu);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_menu = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
    .get_num_sections  = num_sections,
    .get_num_rows      = num_rows,
    .get_header_height = header_height,
    .draw_header       = draw_header,
    .draw_row          = draw_row,
    .select_click      = select_click,
    .select_long_click = select_long_click,
  });
  menu_layer_set_click_config_onto_window(s_menu, window);
#if defined(PBL_COLOR)
  // The menu is the one screen drawn by system code, so it has to be told the
  // theme explicitly rather than inheriting it from an update_proc.
  menu_layer_set_normal_colors(s_menu, COL_BG, COL_INK);
  menu_layer_set_highlight_colors(s_menu, COL_ACCENT, theme_on_accent());
#endif
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

// Does not destroy the window -- see the note in ui_ride.c's window_unload.
static void window_unload(Window *window) {
  menu_layer_destroy(s_menu);
  s_menu = NULL;
}

void ui_history_push(void) {
  if (s_window && window_stack_contains_window(s_window)) {
    return;
  }

  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers) {
      .load = window_load,
      .unload = window_unload,
    });
  }

  window_set_background_color(s_window, COL_BG);
  window_stack_push(s_window, true);
}

// The MenuLayer is drawn by system code, so unlike every other screen it holds
// its own copy of the palette. A theme change pushed from the phone while the
// list is open would otherwise repaint everything except this.
void ui_history_update(void) {
  if (!s_menu) {
    return;
  }
#if defined(PBL_COLOR)
  menu_layer_set_normal_colors(s_menu, COL_BG, COL_INK);
  menu_layer_set_highlight_colors(s_menu, COL_ACCENT, theme_on_accent());
#endif
  layer_mark_dirty(menu_layer_get_layer(s_menu));
}

void ui_history_deinit(void) {
  if (s_window) {
    window_destroy(s_window);
    s_window = NULL;
  }
}
