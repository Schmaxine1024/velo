# Velo

GPS Cycling companion for pebble smartwatches that tracks metrics such as
- Current speed
- Distance
- Ascent
- Ride History

Works natively on your phone and watch with no accounts required.

```
   WATCH (C)                                 PHONE (Java, Android)
 ┌────────────────────────────┐          ┌──────────────────────────────┐
 │ ui_ready / ui_ride         │          │ MainActivity / Settings      │
 │ ui_summary / ui_history    │          │ History / RideDetail         │
 │        ↕                   │          │        ↕                     │
 │ ride.c   ← the model       │          │ RideRecorder  ← the truth    │
 │ theme.c  ← colours         │          │ RideStore (SQLite)           │
 │        ↕                   │          │        ↕                     │
 │ comm.c   ← AppMessage      │          │ RideService (foreground)     │
 └────────┬───────────────────┘          │ PebbleBridge                 │
          │                              └────────┬─────────────────────┘
          │        ┌──────────────────────┐       │  broadcast Intents
          └────────┤  Pebble Android app  ├───────┘  com.getpebble.action.*
            BT     │  (the transport)     │
                   └──────────────────────┘
```

## Repository layout

| Path | What it is |
|---|---|
| `watchapp/` | The Pebble app. C, SDK 4.17, targets emery/basalt/chalk/diorite |
| `watchapp/src/c/protocol.h` | **The wire contract.** Mirrored in `Protocol.java` |
| `android/` | The companion app. Java, minSdk 26, no Play Services |
| `android/app/src/main/java/com/getpebble/` | Vendored PebbleKit (MIT), see below |

## Building

**Watch:**

```sh
cd watchapp
pebble build
pebble install --emulator emery
```

`VELO_DEMO=1 pebble build` compiles in a synthetic telemetry source, because the
emulator has no companion app and every screen otherwise renders as a stalled
ride full of zeroes. It deliberately does *not* fake the Bluetooth link state,
so `pebble emu-bt-connection` still exercises the real path.

**Phone:**

```sh
cd android
./gradlew assembleDebug
```

Needs JDK 17–21 (not 25 — AGP does not support it yet) and an Android SDK with
platform 35.

## Controls on the watch

| Screen | Button | Action |
|---|---|---|
| Ready | SELECT | Start a ride |
| Ready | UP | History |
| Ride | SELECT | Pause / resume |
| Ride | **hold** SELECT | Finish the ride |
| Ride | UP / DOWN | Change which metric is the big one |
| Ride | BACK | Leave the app — **the ride keeps recording** |

**Link state versus data staleness.** The watch learns whether the phone is
there from `connection_service`, which the firmware maintains for free — not
from telemetry having arrived. Inferring it from packet arrival would oblige the
phone to send a keepalive forever, even sitting idle with no ride in progress.
Staleness is a separate idea, and only a fault during a ride:

- `NO PHONE` — Bluetooth is down.
- `NO DATA` — the phone is connected but the companion app has gone quiet.

**The clock stops when the data does.** Distance and ascent only move on an
inbound packet, so a clock that free-ran through a dropout would drift away from
them, and the saved summary would read as an hour spent covering three
kilometres. Freezing everything together keeps the numbers mutually consistent
and honestly dated to the last contact. Nothing is lost: the phone's foreground
service never stopped recording, and one packet resyncs the watch.

**STOP is the only command that is retried forever.** Every other command is
corrected by the next telemetry packet, a lost PAUSE fixes itself a second
later. A lost STOP does not: the phone would keep recording a ride the rider has
already been shown a summary for. So the watch repeats it until the phone
confirms idle, and refuses to believe any other state meanwhile.


## Battery

A 2-hour ride costs roughly 10–20% of a typical phone, almost all of it GPS and
keeping the CPU out of doze. The Pebble link adds perhaps a percentage point:
the process is already awake and the radio link already exists.

The watch is where 1 Hz is proportionally expensive ,figure 8–15% per hour
while a ride is live, against roughly a week idle. Two things keep that down:

- Telemetry only flows during a ride. Idle costs nothing, which is what
  separating link state from staleness bought.
- Static settings (units, colours, protocol version) are sent on connect and on
  change, not packed into all 3,600 frames an hour.
- The ride screen repaints once per second, not twice — the tick skips its
  redraw when a packet has already triggered one.

## Possible future plans
- Live Route
- Heart Rate tracking
