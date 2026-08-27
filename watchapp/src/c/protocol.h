// The wire contract between the watch and the Android companion.
//
// This file is the single source of truth for it. The Android side has a
// mirror of these numbers in Protocol.java -- if you change a key here, change
// it there too, and bump PROTOCOL_VERSION so a stale pairing is detectable
// rather than silently wrong.
//
// Keys are explicit small integers, NOT the auto-assigned 10000+ range the
// Pebble SDK hands out for a `messageKeys` array. A native Android companion
// writes raw integer keys into the dictionary, so the numbers have to be
// stable and knowable; package.json therefore declares messageKeys as an
// object with these exact values.
//
// Division of labour: the phone owns every number (it has the fixes and can
// filter them properly), the watch owns the buttons and the screen. The watch
// runs a local one-second timer purely so the elapsed readout ticks smoothly
// between updates -- it resyncs to the phone's figure whenever one arrives, so
// auto-pause decisions stay in one place instead of being fought over.

#pragma once

#define PROTOCOL_VERSION 1

// ---------------------------------------------------------------------------
// Phone -> watch. Telemetry, pushed about once a second while a ride is live
// and once on connect so a freshly opened watchapp lands in the right state.
// ---------------------------------------------------------------------------

#define KEY_T_DISTANCE   1   // uint32, metres this ride
#define KEY_T_SPEED      2   // uint16, current speed in cm/s
#define KEY_T_ASCENT     3   // uint16, cumulative ascent in metres
#define KEY_T_FIX        4   // uint8,  FIX_* below
#define KEY_T_STATE      5   // uint8,  STATE_* below -- the phone's view
#define KEY_T_MAXSPEED   6   // uint16, session max speed in cm/s
#define KEY_T_PHONEBATT  7   // uint8,  phone battery percent
#define KEY_T_UNITS      8   // uint8,  UNITS_* below
#define KEY_T_MOVING     9   // uint32, moving seconds (authoritative)
#define KEY_T_VERSION    10  // uint8,  PROTOCOL_VERSION of the phone
#define KEY_T_START      11  // uint32, unix time the ride actually started

// Appearance, chosen in the Android app and persisted on the watch so it
// survives the phone being out of range. 24-bit 0xRRGGBB; the watch snaps them
// to its 64-colour palette and derives the remaining greys itself.
#define KEY_T_COL_BG     12  // uint32, background
#define KEY_T_COL_ACCENT 13  // uint32, the hero-number accent

// Which ride is being discussed.
//
// Assigned by the phone when a ride starts and echoed back on every command,
// so neither side can act on the other's stale idea of "the current ride" --
// the case that motivates it is the phone's service being killed and restarted
// mid-ride, after which an un-tagged STOP would land on whatever ride the
// phone now holds. 0 means "no ride".
#define KEY_T_RIDE_ID    14  // uint32

// ---------------------------------------------------------------------------
// Watch -> phone. Commands. KEY_C_CMD is always present; KEY_C_MOVING rides
// along with STOP so the phone can cross-check its own figure.
//
// The key is named for moving time, not elapsed time, because moving time is
// the only duration the watch actually holds -- the phone overwrites it every
// second via KEY_T_MOVING, and auto-paused seconds are excluded from both. A
// key called ELAPSED carrying moving seconds would guarantee the cross-check
// disagreed by the whole stopped-at-traffic-lights total, which in town is a
// quarter of the ride.
// ---------------------------------------------------------------------------

#define KEY_C_CMD        20  // uint8, CMD_* below
#define KEY_C_MOVING     21  // uint32, the watch's moving seconds
#define KEY_C_RIDE_ID    22  // uint32, the ride this command refers to

// The phone must apply commands idempotently, keyed on KEY_C_RIDE_ID: a STOP
// for a ride that has already stopped is a no-op that still reports IDLE, not
// an error. The watch repeats STOP until it sees that IDLE, because STOP is
// the one command the phone cannot restate on its own -- every other command
// is corrected by the next telemetry packet, but a dropped STOP leaves the
// phone recording a ride the rider has already been shown a summary for.

#define CMD_START        1
#define CMD_PAUSE        2
#define CMD_RESUME       3
#define CMD_STOP         4
#define CMD_SYNC         5   // "tell me where we are" -- sent on app launch
#define CMD_LAP          6

// ---------------------------------------------------------------------------
// Shared enumerations.
// ---------------------------------------------------------------------------

#define STATE_IDLE       0
#define STATE_RECORDING  1
#define STATE_PAUSED     2

// Fix quality is bucketed rather than sent as raw metres of accuracy: the
// watch only ever renders it as a three-bar indicator, so bucketing on the
// phone keeps the presentation decision next to the data that informs it.
#define FIX_NONE         0
#define FIX_POOR         1
#define FIX_OK           2
#define FIX_GOOD         3

#define UNITS_METRIC     0
#define UNITS_IMPERIAL   1
