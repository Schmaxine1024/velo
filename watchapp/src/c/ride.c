#include "ride.h"

#define PERSIST_KEY_UNITS    1
#define PERSIST_KEY_HISTORY  2
#define PERSIST_KEY_COUNT    3
#define PERSIST_KEY_SCHEMA   4

// Bump whenever RideSummary's layout or the meaning of its fields changes.
//
// A size check alone cannot catch this: swapping distance_m and moving_s, or
// switching a field's units, leaves the struct at exactly 16 bytes, so the old
// blob still reads back cleanly and gets reinterpreted as real mileage. The
// version is stored beside the data and any mismatch discards the history --
// losing fifteen summaries the phone still holds in full is much cheaper than
// showing invented ones.
#define HISTORY_SCHEMA  1

static RideState   s_state;
static bool        s_imperial;
static RideSummary s_history[HISTORY_MAX];
static uint8_t     s_history_count;

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

static void history_load(void) {
  s_history_count = 0;
  memset(s_history, 0, sizeof(s_history));

  if (!persist_exists(PERSIST_KEY_HISTORY) || !persist_exists(PERSIST_KEY_COUNT)) {
    return;
  }

  // No schema key at all means a build that predates the guard; treat it the
  // same as a mismatch rather than trusting bytes of unknown provenance.
  if (!persist_exists(PERSIST_KEY_SCHEMA) ||
      persist_read_int(PERSIST_KEY_SCHEMA) != HISTORY_SCHEMA) {
    return;
  }

  int32_t count = persist_read_int(PERSIST_KEY_COUNT);
  if (count < 0) count = 0;
  if (count > HISTORY_MAX) count = HISTORY_MAX;

  // history_save() always writes the whole fixed-size array, so a blob this
  // build wrote always reads back at exactly sizeof(s_history). Anything else
  // came from a build with a different RideSummary layout, and reinterpreting
  // those bytes would invent mileage. Drop it instead.
  int read = persist_read_data(PERSIST_KEY_HISTORY, s_history, sizeof(s_history));
  if (read != (int)sizeof(s_history)) {
    s_history_count = 0;
    memset(s_history, 0, sizeof(s_history));
    return;
  }

  s_history_count = (uint8_t)count;
}

static void history_save(void) {
  persist_write_data(PERSIST_KEY_HISTORY, s_history, sizeof(s_history));
  persist_write_int(PERSIST_KEY_COUNT, s_history_count);
  persist_write_int(PERSIST_KEY_SCHEMA, HISTORY_SCHEMA);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ride_init(void) {
  memset(&s_state, 0, sizeof(s_state));
  s_state.state         = STATE_IDLE;
  s_state.phone_batt    = 255;   // unknown until the phone says otherwise
  s_state.fix           = FIX_NONE;
  s_state.silence_s     = STALE_TIMEOUT_S;
  s_state.phone_version = 0;     // 0 = the phone has not identified itself yet

  // Seeded from the firmware rather than assumed false: the app is usually
  // launched with the phone already connected, and starting at "not linked"
  // would flash NO PHONE on the ready screen for the first second.
  s_state.linked = connection_service_peek_pebble_app_connection();

  s_imperial = persist_exists(PERSIST_KEY_UNITS)
      ? (persist_read_int(PERSIST_KEY_UNITS) == UNITS_IMPERIAL)
      : false;

  history_load();
}

RideState *ride_get(void) {
  return &s_state;
}

bool ride_data_stale(void) {
  return s_state.state != STATE_IDLE && s_state.silence_s >= STALE_TIMEOUT_S;
}

void ride_set_link(bool connected) {
  if (connected == s_state.linked) {
    return;
  }
  s_state.linked = connected;

  if (!connected) {
    // Blank the two readings that become active lies the moment they stop
    // being refreshed. A frozen "31.4" km/h claims a speed we are no longer
    // measuring; a frozen three-bar fix indicator claims GPS is healthy, which
    // is the one thing the watch has just lost all evidence for.
    s_state.speed_cms = 0;
    s_state.fix       = FIX_NONE;
  }
}

bool ride_tick(void) {
  bool was_stale = ride_data_stale();
  bool changed = false;

  // Age the staleness counter. Saturate rather than wrap -- this is a uint8,
  // and a long ride with the phone off would otherwise roll back around to
  // "fresh" every 255 seconds.
  if (s_state.silence_s < 255) {
    s_state.silence_s++;
  }

  if (!was_stale && ride_data_stale()) {
    s_state.speed_cms = 0;
    s_state.fix       = FIX_NONE;
    changed = true;
  }

  // Tick the local clock so the moving readout advances every second instead
  // of stepping whenever a packet happens to land. The phone's KEY_T_MOVING
  // overwrites this the moment it arrives, so drift never accumulates and
  // auto-pause stays the phone's decision alone.
  //
  // Requires fresh data: see the note on ride_tick() in ride.h for why the
  // clock stops rather than free-running through a stall.
  if (s_state.state == STATE_RECORDING && !ride_data_stale()) {
    s_state.moving_s++;
    changed = true;
  }

  return changed;
}

void ride_telemetry_received(void) {
  s_state.silence_s = 0;
}

// ---------------------------------------------------------------------------
// Local transitions
// ---------------------------------------------------------------------------

// Clear the per-ride numbers, leaving everything that describes the phone and
// the link alone -- those outlive any individual ride.
//
// Written as an explicit field list rather than memset-then-restore. The
// restore form reads as if it clears everything, so a field added to RideState
// later is silently zeroed on every start and stop; here, a new field is simply
// not touched unless someone decides it should be.
static void clear_ride_fields(void) {
  s_state.distance_m    = 0;
  s_state.speed_cms     = 0;
  s_state.max_speed_cms = 0;
  s_state.ascent_m      = 0;
  s_state.moving_s      = 0;
  s_state.start_time    = 0;
  s_state.ride_id       = 0;   // the phone assigns the next one
}

void ride_local_start(void) {
  clear_ride_fields();
  s_state.state = STATE_RECORDING;

  // Optimistic local stamp so the ride has a date even if the phone never
  // answers; KEY_T_START replaces it with the real one on the next packet.
  s_state.start_time = (uint32_t)time(NULL);
}

void ride_local_pause(void) {
  if (s_state.state == STATE_RECORDING) {
    s_state.state     = STATE_PAUSED;
    s_state.speed_cms = 0;
  }
}

void ride_local_resume(void) {
  if (s_state.state == STATE_PAUSED) {
    s_state.state = STATE_RECORDING;
  }
}

bool ride_local_stop(RideSummary *out) {
  RideSummary s = {
    .start_time    = s_state.start_time,
    .distance_m    = s_state.distance_m,
    .moving_s      = s_state.moving_s,
    .ascent_m      = s_state.ascent_m,
    .max_speed_cms = s_state.max_speed_cms,
  };

  // A ride that went nowhere in no time was a misclick. Reset, save nothing.
  bool worth_keeping = (s.distance_m > 0 || s.moving_s >= RIDE_MIN_KEEP_S);

  if (worth_keeping) {
    // start_time normally arrives from the phone in KEY_T_START, which is the
    // only party that knows when the ride really began -- it matters when the
    // watchapp was launched mid-ride and never saw the START at all.
    //
    // Falling back to now-minus-moving would be wrong by every auto-paused
    // second, which on a long ride with stops is enough to land the date on
    // the wrong side of midnight, and the date is all the history row shows.
    // So an unknown start is stamped with now: late, but never the wrong day.
    if (s.start_time == 0) {
      s.start_time = (uint32_t)time(NULL);
    }

    // Newest first. Shift the entries we intend to keep down one slot -- when
    // the list is already full that is all but the last, which falls off the
    // end -- then write the new ride into the front and grow the count.
    uint8_t keep = s_history_count;
    if (keep > HISTORY_MAX - 1) {
      keep = HISTORY_MAX - 1;
    }
    for (int i = keep; i > 0; i--) {
      s_history[i] = s_history[i - 1];
    }
    s_history[0] = s;

    if (s_history_count < HISTORY_MAX) {
      s_history_count++;
    }
    history_save();
  }

  if (out) {
    *out = s;
  }

  clear_ride_fields();
  s_state.state = STATE_IDLE;
  s_state.fix   = FIX_NONE;   // no ride, nothing asking for a fix

  return worth_keeping;
}

// ---------------------------------------------------------------------------
// Settings and history access
// ---------------------------------------------------------------------------

bool ride_imperial(void) {
  return s_imperial;
}

void ride_set_units(uint8_t units) {
  bool imperial = (units == UNITS_IMPERIAL);
  if (imperial != s_imperial) {
    s_imperial = imperial;
    persist_write_int(PERSIST_KEY_UNITS, units);
  }
}

uint8_t ride_history_count(void) {
  return s_history_count;
}

const RideSummary *ride_history_at(uint8_t index) {
  if (index >= s_history_count) {
    return NULL;
  }
  return &s_history[index];
}

void ride_history_clear(void) {
  s_history_count = 0;
  memset(s_history, 0, sizeof(s_history));
  history_save();
}
