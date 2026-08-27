# Velo — Pebble appstore listing

Copy for `pebble publish`. Plain-text description below the rule is the value
for `--description`.

- **Name:** Velo
- **Tagline:** A GPS cycling computer for your handlebars.
- **Category:** `tools` (the publish default). `health` if the store offers a
  fitness category — the live list comes from the appstore API at publish time.
- **Source URL:** https://github.com/Schmaxine1024/velo
- **Watches:** Pebble Time / Time Steel, Time Round, Pebble 2, Pebble Time 2,
  Pebble Round 2. Original Pebble and Pebble Steel (aplite) are not supported.

---

Velo turns your Pebble into a handlebar computer.

Your phone has the GPS, so it does the recording. The watch is the display and
the buttons — the part you actually ride by.

WHILE YOU RIDE
• Speed, distance and elapsed time, updated every second
• Average speed, max speed and total ascent
• Choose which metric gets the big readout — UP and DOWN cycle the hero number
• Auto-pause when you stop at a light, resume when you roll
• Past rides, on the watch

CONTROLS
• SELECT — start, pause, resume
• Hold SELECT — finish the ride
• UP — ride history
• BACK — leave the app; your ride keeps recording

NO ACCOUNTS, NO CLOUD
No login, no API keys, no Strava upload, no map tiles. Velo works with your
phone in flight mode, and your rides stay on your phone.

ONE SET OF NUMBERS
The phone owns the arithmetic — accuracy filtering, speed sanity checks, ascent
smoothing and auto-pause all happen in one place. The watch displays a value
rather than computing a second opinion, so the two devices can't disagree about
how far you went.

WHEN THE LINK DROPS
Velo tells you which of two different things went wrong: NO PHONE if Bluetooth
is down, NO DATA if the phone is connected but has gone quiet. The readouts
freeze together and stay honestly dated to the last contact, then resync the
moment a packet lands. Your phone never stopped recording.

MAKE IT YOURS
Pick a background and an accent colour; Velo derives the text and hairline
colours from your choice so you can't end up with an unreadable watch. Metric
or imperial.

REQUIRES THE VELO ANDROID APP
This watchapp is one half of Velo. You also need the Velo companion app for
Android, which does the GPS recording — get it from the project's GitHub
releases page, linked from this listing. iPhone is not supported.

---

## Release notes (v1.0.2)

Adds support for Pebble Round 2, and fixes the layout on round watches --
panels no longer run off the edge of the glass, the status indicators and
button hints are visible again, and the ride screen's lower readouts are no
longer cut off.

Fixes a bug where the highlighted distance on the ride summary lost its lower
half on Pebble Time, Pebble 2 and Pebble Time Round.

Switching between metric and imperial on the phone now reaches the watch
instead of waiting until the next ride.
