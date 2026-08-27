// The model: current ride state, plus the on-watch history of finished ones.
//
// Nothing in here talks to AppMessage or to a Layer. comm.c pushes telemetry
// in, the UI reads it out, and this file is the only place that knows how a
// ride is stored. That seam is what lets the ride screen keep ticking when the
// phone link drops -- the model simply stops being refreshed, and the UI reads
// a `linked` flag to say so rather than going blank.

#pragma once

#include <pebble.h>
#include "protocol.h"

// How many seconds of silence during a ride before the numbers are considered
// stale. Telemetry arrives about once a second while recording, so this is
// roughly eight missed packets -- long enough to ride through a pocket-shaped
// Bluetooth null, short enough that a genuine stall shows up before the next
// junction.
//
// This is NOT how the watch decides whether the phone is there. That comes
// from connection_service, which the firmware maintains for free. Inferring it
// from packet arrival instead would oblige the phone to transmit a keepalive
// forever, even sitting idle on the ready screen with no ride in progress --
// a battery cost baked into the protocol for no information gained.
#define STALE_TIMEOUT_S  8

// A finished ride, as the watch remembers it. Sixteen bytes, chosen so a
// useful number of them fit in one persist slot (see HISTORY_MAX).
typedef struct {
  uint32_t start_time;      // unix seconds, local clock at the moment of START
  uint32_t distance_m;
  uint32_t moving_s;
  uint16_t ascent_m;
  uint16_t max_speed_cms;
} RideSummary;

// A persist value tops out at 256 bytes. Fifteen summaries is 240, leaving
// margin rather than sitting exactly on the ceiling. The phone keeps the full
// history and the whole GPS track; this is the glanceable tail of it, and it
// exists so the watch is still useful with the phone in a jersey pocket.
#define HISTORY_MAX  15

// Below this, with no distance recorded, a ride is treated as a misclick.
#define RIDE_MIN_KEEP_S  10

typedef struct {
  uint8_t  state;           // STATE_IDLE / STATE_RECORDING / STATE_PAUSED
  uint32_t distance_m;
  uint16_t speed_cms;
  uint16_t max_speed_cms;
  uint16_t ascent_m;
  uint8_t  fix;             // FIX_*
  uint8_t  phone_batt;      // percent, 255 = unknown
  uint32_t moving_s;        // authoritative from phone, ticked locally between
  uint32_t start_time;      // unix seconds, from the phone via KEY_T_START
  uint32_t ride_id;         // the phone's id for this ride, 0 = none
  bool     linked;          // is the phone app connected (from connection_service)
  uint8_t  silence_s;       // seconds since the last telemetry packet
  uint8_t  phone_version;   // phone's PROTOCOL_VERSION, 0 = not yet heard
} RideState;

// True when a ride is live but its numbers have stopped arriving. Distinct
// from !linked: the phone can be perfectly connected and simply not sending,
// which is the normal state when idle and a fault only during a ride.
bool ride_data_stale(void);

// Bluetooth link state, pushed in from connection_service.
void ride_set_link(bool connected);

void ride_init(void);

// The live model. Callers read it; only ride.c and comm.c write it.
RideState *ride_get(void);

// Called once a second by the app's tick timer. Advances the local moving
// count while recording *and linked*, and ages the link watchdog. Returns true
// if anything the UI cares about changed, so the caller can skip a redraw when
// nothing did.
//
// The clock deliberately stops once the data goes stale. Distance, ascent and
// max speed only ever move on an inbound packet, so a clock that kept running
// through a dropout would drift away from them and the summary written at STOP
// would read as an hour spent covering the three kilometres we last heard
// about. Freezing every number together keeps them mutually consistent and
// honestly dated to the last contact -- and costs nothing real, because the
// phone's foreground service never stopped recording, so the moment packets
// resume KEY_T_MOVING resyncs us to the truth.
bool ride_tick(void);

// Telemetry landed. Resets the link watchdog and marks us linked.
void ride_telemetry_received(void);

// State transitions requested locally (button presses). These update the model
// optimistically so the screen responds on the same frame as the click; the
// phone's own STATE in the next telemetry packet is what ultimately wins.
void ride_local_start(void);
void ride_local_pause(void);
void ride_local_resume(void);

// Snapshot the live ride into `out` and push it onto the history, then reset
// the model to idle. `out` is filled either way.
//
// Returns false, and stores nothing, for a ride that covered no distance and
// lasted under RIDE_MIN_KEEP_S -- an accidental start-stop, which should not
// litter a fifteen-slot list. A ride that covered any distance at all is kept
// regardless of how brief it was.
bool ride_local_stop(RideSummary *out);

// Units are a phone-side setting mirrored here so the watch renders correctly
// while the link is down. Persisted.
bool ride_imperial(void);
void ride_set_units(uint8_t units);

// History, newest first.
uint8_t ride_history_count(void);
const RideSummary *ride_history_at(uint8_t index);
void ride_history_clear(void);
