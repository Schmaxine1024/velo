#include <pebble.h>
#include "ride.h"
#include "comm.h"
#include "ui.h"

// Wiring. main.c owns the once-a-second tick, the Bluetooth link handler and
// the inbound-message handler, and does nothing else -- the model lives in
// ride.c, the transport in comm.c and every pixel in the ui_* files.

// Set when an inbound packet has already repainted this second, so the tick
// does not repaint again on top of it. See handle_tick.
static bool s_painted_this_second;

// Repaint whichever screens are loaded. Each of these no-ops when its window
// is not up, so this stays honest without main.c tracking the window stack.
static void ui_refresh(void) {
  ui_ready_update();
  ui_ride_update();
  ui_history_update();
}

// ---------------------------------------------------------------------------
// The phone spoke
// ---------------------------------------------------------------------------

static void handle_comm(bool phone_ended_ride) {
  RideState *r = ride_get();

  if (phone_ended_ride) {
    // Stop was pressed in the Android app. comm.c deliberately leaves the
    // model alone so the numbers survive to be snapshotted here.
    //
    // This is self-limiting: ride_local_stop() moves the model to IDLE, so the
    // next packet sees prev_state == IDLE and does not raise the event again.
    // That is what stops a summary window being pushed once per second.
    RideSummary summary;
    bool kept = ride_local_stop(&summary);

    ui_ride_dismiss();
    vibes_double_pulse();
    ui_summary_push(kept ? &summary : NULL, false);

  } else if (r->state != STATE_IDLE && ui_ready_is_top()) {
    // A ride is live but we are sitting on the ready screen -- either the
    // rider pressed start in the app, or the watchapp was opened mid-ride and
    // the launch SYNC has just been answered. Only auto-advance from the ready
    // screen: doing it unconditionally would shove the ride screen on top of a
    // summary the rider is still reading.
    ui_ride_push();
  }

  ui_refresh();
  s_painted_this_second = true;
}

// ---------------------------------------------------------------------------
// Demo telemetry
//
// The emulator has no companion app, so without this every screen renders as
// a permanently unlinked ride full of zeroes and the layouts cannot be checked
// at all. Compiled in only under `VELO_DEMO=1 pebble build`.
//
// It writes to the model through the same fields comm.c writes, deliberately:
// anything that renders correctly here renders correctly on real telemetry.
// ---------------------------------------------------------------------------

#ifdef VELO_DEMO
static void demo_seed_history(void) {
  // Only ever seed an empty history. Persist survives a reinstall, so seeding
  // unconditionally adds two more rides on every launch and the list fills up
  // with duplicates of the same two days.
  if (ride_history_count() > 0) {
    return;
  }

  // Two finished rides so the ready screen's "last ride" block and the history
  // list both have something to draw.
  RideSummary a = { .start_time = (uint32_t)time(NULL) - 86400,
                    .distance_m = 42730, .moving_s = 5820,
                    .ascent_m = 631, .max_speed_cms = 1390 };
  RideSummary b = { .start_time = (uint32_t)time(NULL) - 3 * 86400,
                    .distance_m = 18400, .moving_s = 2515,
                    .ascent_m = 214, .max_speed_cms = 1120 };

  RideState *r = ride_get();
  *r = (RideState){ .state = STATE_RECORDING };
  r->distance_m = b.distance_m; r->moving_s = b.moving_s;
  r->ascent_m = b.ascent_m; r->max_speed_cms = b.max_speed_cms;
  r->start_time = b.start_time;
  ride_local_stop(NULL);

  *r = (RideState){ .state = STATE_RECORDING };
  r->distance_m = a.distance_m; r->moving_s = a.moving_s;
  r->ascent_m = a.ascent_m; r->max_speed_cms = a.max_speed_cms;
  r->start_time = a.start_time;
  ride_local_stop(NULL);
}

static void demo_tick(void) {
  RideState *r = ride_get();

  // Stand in for fresh telemetry. Deliberately does NOT force r->linked: that
  // now comes from connection_service, and overriding it here would make the
  // demo build unable to show the NO PHONE state at all -- masking exactly the
  // path `pebble emu-bt-connection` exists to exercise.
  r->silence_s     = 0;
  r->fix           = FIX_GOOD;
  r->phone_batt    = 76;
  r->phone_version = PROTOCOL_VERSION;

  if (r->state == STATE_RECORDING) {
    // Wobble around 28 km/h so the hero value changes width as it runs.
    r->speed_cms = 700 + (uint16_t)((r->moving_s % 9) * 45);
    r->distance_m += r->speed_cms / 100;
    if (r->speed_cms > r->max_speed_cms) {
      r->max_speed_cms = r->speed_cms;
    }
    r->ascent_m = (uint16_t)(r->distance_m / 40);
  }
}
#endif

// ---------------------------------------------------------------------------
// One second passed
// ---------------------------------------------------------------------------

static void handle_tick(struct tm *tick_time, TimeUnits units_changed) {
#ifdef VELO_DEMO
  demo_tick();
  // Then announce it exactly as a real packet would. Updating the model without
  // this would leave the screen stale, because the ready screen only repaints
  // on a model change or an inbound message -- and demo_tick is neither.
  // Routing through handle_comm keeps the demo honest about the real path.
  handle_comm(false);
#endif

  ride_tick();

  // Repaint here only if the packet handler has not already done it this
  // second. Telemetry arrives at the same 1 Hz as this tick, so repainting in
  // both places meant a full-screen redraw twice a second -- ten text runs
  // rendered for one second's worth of change, on a coin cell.
  //
  // When packets stop, this becomes the only thing repainting, which is
  // exactly when the NO DATA banner needs to appear.
  if (!s_painted_this_second) {
    ui_refresh();
  }
  s_painted_this_second = false;
}

// ---------------------------------------------------------------------------
// Bluetooth link
// ---------------------------------------------------------------------------

static void handle_connection(bool connected) {
  // Free link state, straight from the firmware. The alternative -- inferring
  // it from telemetry arrival -- would require the phone to transmit a
  // keepalive forever just so the watch could conclude nothing had changed.
  APP_LOG(APP_LOG_LEVEL_DEBUG, "link %s", connected ? "up" : "down");
  ride_set_link(connected);
  ui_refresh();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

static void init(void) {
  // Before any window exists: the ready screen paints itself from the theme
  // the moment it loads, so the persisted colours have to be in place first.
  theme_init();
  ride_init();

#ifdef VELO_DEMO
  demo_seed_history();
#endif

  ui_ready_push();

  // comm_init sends the launch SYNC, so the UI must already exist to render
  // whatever comes back.
  comm_init(handle_comm);

  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = handle_connection,
  });

  tick_timer_service_subscribe(SECOND_UNIT, handle_tick);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  connection_service_unsubscribe();
  comm_deinit();

  // Every screen caches its Window rather than destroying it on unload (see
  // ui_ride.c), so this is the only place any of them are freed.
  ui_ride_deinit();
  ui_summary_deinit();
  ui_history_deinit();
  ui_ready_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
