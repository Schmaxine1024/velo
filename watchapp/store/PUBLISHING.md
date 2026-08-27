# Publishing Velo to the Pebble appstore

Verified against **pebble-tool 5.0.40 / SDK 4.33.1**. Specs below were read out
of `pebble_tool/commands/publish.py` and `screenshot.py`, not guessed.

## Asset specs

| Asset | Spec | File |
|---|---|---|
| `iconLarge` | 144×144 PNG | `icon/velo_icon_large_144.png` |
| `iconSmall` | 80×80 PNG | `icon/velo_icon_small_80.png` |
| Screenshots | native per platform | `screenshots/<platform>_*.png` |
| Tour GIFs | native per platform | `gif/<platform>_tour.gif` |

Native sizes: basalt 144×168, diorite 144×168, chalk 180×180, emery 200×228,
gabbro 260×260.

Icon alternates in `icon/alt_brand-orange_*.png` — see "Colour" below.

## The filename rule that will bite you

`publish` infers the platform by splitting the basename on the **first
underscore**:

```python
parts = basename.split("_", 1)
return parts[0]           # -> "emery"
```

`emery_2_ride.png` uploads as platform `emery`. A hyphenated name has no
underscore and raises *"Could not infer platform from capture filename"*. The
older `watchapp/screenshots/` set uses hyphens and cannot be passed as-is.

## ffmpeg

`publish` defaults `--gif-all-platforms` **on** (`set_defaults(
capture_gif_all_platforms=True)`), and that path calls
`_check_gif_dependencies()`, which hard-fails without ffmpeg:

```
ToolError: Missing required tool for GIF capture: ffmpeg.
```

ffmpeg is not installed here. Either install it, or pass
`--no-gif-all-platforms` and upload the GIFs in `gif/`. Its built-in GIF also
waits for a minute rollover — a watchface idea that does nothing for Velo.

## Colour

Screenshots are **not** raw framebuffer colour. `screenshot.py` applies a
64-entry LUT simulating the physical display, including:

```python
(255, 85, 0): (230, 110, 107),
```

So Velo's `DEFAULT_ACCENT 0xFF5500` (pure orange) captures as `#E66E6B`
(salmon). That is what the watch physically looks like, and it is the tool
default — but a screenshot-derived icon inherits salmon rather than the brand
orange. `icon/alt_brand-orange_*.png` are the same icon with the LUT entry
inverted back to `#FF5500`. Pick one pair; do not mix.

## Publish

`publish` builds the pbw itself. Build **without** `VELO_DEMO` and with no
`src/pkjs` present (see Screenshots below).

```sh
cd watchapp
pebble build                      # NOT VELO_DEMO=1

pebble publish \
  --no-gif-all-platforms \
  --name "Velo" \
  --category tools \
  --source "https://github.com/Schmaxine1024/velo" \
  --icon-large  store/icon/velo_icon_large_144.png \
  --icon-small  store/icon/velo_icon_small_80.png \
  --description "$(sed -n '/^---$/,/^---$/p' store/LISTING.md | sed '1d;$d')" \
  --screenshots store/screenshots/*.png store/gif/*.gif
```

Omit `--is-published` to stage it hidden and check the web dashboard first.

`pebble login` (Firebase) is required and the account must be linked to a
developer account, or the upload fails with *"Firebase account is valid but not
linked to a developer account"*. No pebble auth files exist on this machine yet.

## Screenshots: the emulator needs a JS file on SDK 4.33.1

On 4.33.1 pypkjs only registers the phone-app connection when the project ships
JS. Velo has none (native Android companion), so
`connection_service_peek_pebble_app_connection()` returns false and **every
screen renders in the NO PHONE fault state**.

To re-capture, add a throwaway `src/pkjs/index.js`:

```js
Pebble.addEventListener('ready', function () {});
```

then `VELO_DEMO=1 pebble build`, capture, and **delete `src/pkjs` before any
release build** — otherwise the JS ships in the pbw.

`pebble emu-bt-connection` does not help: it still produces byte-identical
screenshots for `--connected yes` and `no` on this SDK.

Do not run `pebble wipe` to reset persisted state — it removes the SDK
toolchain (`qemu-pebble`) along with emulator data.

## Platform coverage

`targetPlatforms` is basalt, chalk, diorite, emery and **gabbro** (Pebble
Round 2). flint (Pebble 2 Duo) is not built for; it is 144×168 rect and
geometrically identical to diorite, so adding it should be a package.json
change plus a verification pass rather than layout work.

Note that basalt, chalk and diorite are marked `PBL_SDK_FROZEN` in the SDK's
platform table — they are legacy boards. emery, flint and gabbro are the
current ones.
