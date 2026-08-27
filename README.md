# Velo

A GPS cycling computer in two halves: a Pebble watchapp you ride by, and a
native Android app that does the actual recording.

No accounts, no API keys, no Strava, no Google Play Services, no map tiles.
Everything works with the phone in flight mode.

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

## The one design decision everything else follows from

No Pebble has GPS, so the phone was always going to be the sensor. Velo goes
further and makes it the sole **authority**:

- **The phone owns every number.** It has the fixes and can filter them
  properly — accuracy gating, speed sanity, ascent hysteresis. It also decides
  auto-pause.
- **The watch owns the buttons and the screen.** It renders, it takes presses,
  and it keeps a local one-second clock only so the elapsed readout ticks
  smoothly between packets.

There is exactly one copy of the arithmetic, so the two devices cannot drift on
distance or ascent. The watch is displaying a value, not computing a second
opinion.

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

Finishing is a long SELECT, not a long BACK, because holding back is a
firmware-level "quit the app" gesture a watchapp does not get to override.
Subscribing to it looks like it works right up until the app closes underneath
you.

## Things worth knowing

**Why PebbleKit is vendored.** It was published to jcenter, which no longer
exists. The source is MIT-licensed and lives under
`android/app/src/main/java/com/getpebble/android/kit/`. Vendoring also meant the
exact intent actions could be read rather than guessed.

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
corrected by the next telemetry packet — a lost PAUSE fixes itself a second
later. A lost STOP does not: the phone would keep recording a ride the rider has
already been shown a summary for. So the watch repeats it until the phone
confirms idle, and refuses to believe any other state meanwhile.

**Commands quote a ride id.** Assigned by the phone when a ride starts and
echoed back, so a service that was killed and restarted mid-ride cannot have a
stale STOP applied to the wrong ride.

**Colour customization cannot produce an unreadable watch.** The rider picks a
background and an accent; ink, label grey and hairline colour are all derived
from the background's luminance. An accent too close to the background is
rejected in favour of plain ink, and the phone's settings screen says so rather
than substituting silently. `WatchTheme.java` duplicates `theme.c`'s derivation
so the preview predicts the watch — keep the two in sync.

## The icon

A bicycle, authored as 1-bit pixel art on a 25×25 lattice — the Pebble
menu-icon size. That constraint is the aesthetic: `tools/icon/gen.py` draws it
with Bresenham lines and a midpoint circle, so the result sits *on* the grid
rather than being a smooth vector resampled onto it.

```sh
python3 tools/icon/gen.py
```

emits both targets from the one grid:

- `watchapp/resources/images/menu_icon.png` — 25×25, black on transparent
- `android/…/drawable/ic_launcher_foreground.xml` — the same pixels as vector
  rects, horizontal runs merged (54 subpaths rather than ~103 single pixels)

So the phone icon is not a lookalike; it is the same drawing. The generator is
the source of truth and both outputs are checked in, since they are build
inputs.

Two things the drawing has to fight at this size. The frame triangle is drawn
wider than a real bicycle's, because an anatomically tight one puts the seat
tube, down tube and top tube on adjacent pixels where they fill into a solid
blob. And the saddle and bars sit a row clear of the top tube on single-pixel
posts — flush against it they merge into one heavy slab and stop reading as
separate parts.

## Keeping the two implementations honest

Two pieces of logic exist in both languages, and both are load-bearing.

**`Format.java` mirrors `fmt.c`, integer truncation and all.** The obvious Java
version — convert to double, `String.format("%.2f")` — is *not* equivalent,
because the C truncates where `String.format` rounds half-up. At 1609 m the
watch reads 0.99 mi and the phone 1.00 mi, for the same instant of the same
ride. `tools/fmtcheck` compiles the real `fmt.c` natively against a shim for
`pebble.h`, runs both sides over the same vectors and diffs them:

```sh
tools/fmtcheck/run.sh          # regenerates c-output.txt from the real fmt.c
cd android && ./gradlew test   # asserts Java matches it
```

`FormatTest.java` is generated from that output rather than hand-written, so it
compares against the C that actually runs on the watch.

**`WatchTheme.java` mirrors `theme.c`.** The settings screen previews what the
watch will look like, including the parts the watch *derives* — so if the
constants drift, the preview becomes a confident lie. `WatchThemeTest` pins the
luminance threshold, the grey inversion, the accent-rejection rule, and that
every offered swatch is on Pebble's two-bits-per-channel palette.

**`RideRecorder` is shaped for testing, on purpose.** The two worst bugs it has
had — a noise floor that rejected any riding under about 30 km/h, and an
auto-pause that could never release — both lived in code no test could reach,
because the logic sat inside a method taking an `android.location.Location`,
which cannot be constructed in a JVM test. So the per-fix decision now lives in
`onSample(...)` over primitives, with the filter predicate split out as the pure
static `isTravel(...)`. `RideRecorderTest` drives the real intake path rather
than poking fields: a test that set the speed directly would have passed against
the broken code. Both regressions were mutation-checked — reintroduce either bug
and the suite fails.

## What has and has not been run

The watchapp has been driven on the emery emulator: ready, live ride, hero
cycling, pause, finish, summary, history, both unit systems, a custom dark
theme applied over a real AppMessage, and the stale-data path. Screenshots are
in `watchapp/screenshots/`.

The `NO PHONE` path is **not** verified on-device: `pebble emu-bt-connection`
produces malformed QEMU packets on this SDK version and never reaches the
firmware, so only the `NO DATA` branch could be exercised.

The Android app compiles clean and its unit tests pass, but it has **not been
run on a device or emulator** — the Android emulator segfaults in its renderer
on this host under every backend tried (`swiftshader_indirect`, `off`,
`guest`). Everything below the UI is therefore unproven at runtime: the
foreground-service lifecycle, the GPS filtering against real fixes, and the
PebbleKit round trip in particular.

## Battery

A 2-hour ride costs roughly 10–20% of a typical phone, almost all of it GPS and
keeping the CPU out of doze. The Pebble link adds perhaps a percentage point:
the process is already awake and the radio link already exists.

The watch is where 1 Hz is proportionally expensive — figure 8–15% per hour
while a ride is live, against roughly a week idle. Two things keep that down:

- Telemetry only flows during a ride. Idle costs nothing, which is what
  separating link state from staleness bought.
- Static settings (units, colours, protocol version) are sent on connect and on
  change, not packed into all 3,600 frames an hour.
- The ride screen repaints once per second, not twice — the tick skips its
  redraw when a packet has already triggered one.

## Not done

- iOS. PebbleKit iOS exists but needs Xcode; this is an Android companion.
- Live route/navigation on the watch.
- Heart rate, cadence, or any external sensor.
