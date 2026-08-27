#include "theme.h"

#define PERSIST_KEY_COL_BG      10
#define PERSIST_KEY_COL_ACCENT  11

// White background, orange accent -- the default the phone also ships with, so
// a watch that has never been configured looks like one that has.
#define DEFAULT_BG      0xFFFFFF
#define DEFAULT_ACCENT  0xFF5500

static uint32_t s_bg     = DEFAULT_BG;
static uint32_t s_accent = DEFAULT_ACCENT;
static bool     s_dark;   // is the background dark enough to need light ink

// ---------------------------------------------------------------------------
// Derivation
// ---------------------------------------------------------------------------

// Rec. 601 luma. Green dominates because the eye does: a saturated blue at
// full value is far darker to look at than a full-value yellow, and treating
// them the same is how you end up with white text on yellow.
static uint16_t luma(uint32_t rgb) {
  uint32_t r = (rgb >> 16) & 0xFF;
  uint32_t g = (rgb >> 8) & 0xFF;
  uint32_t b = rgb & 0xFF;
  return (uint16_t)((r * 299 + g * 587 + b * 114) / 1000);
}

// The crossover point between black and white ink. Slightly above mid-grey
// because black text on a mid-tone reads better than white text does -- the
// tie should break toward dark ink.
#define INK_THRESHOLD  145

static void recompute(void) {
  s_dark = luma(s_bg) < INK_THRESHOLD;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

GColor theme_bg(void) {
#if defined(PBL_COLOR)
  return GColorFromHEX(s_bg);
#else
  return GColorWhite;
#endif
}

GColor theme_ink(void) {
#if defined(PBL_COLOR)
  return s_dark ? GColorWhite : GColorBlack;
#else
  return GColorBlack;
#endif
}

GColor theme_accent(void) {
#if defined(PBL_COLOR)
  // An accent too close to the background is worse than no accent: the hero
  // number, which is the one thing on the screen you read at speed, would
  // vanish. Fall back to plain ink when the contrast is not there.
  uint16_t la = luma(s_accent);
  uint16_t lb = luma(s_bg);
  uint16_t gap = (la > lb) ? (la - lb) : (lb - la);
  if (gap < 60) {
    return theme_ink();
  }
  return GColorFromHEX(s_accent);
#else
  return GColorBlack;
#endif
}

GColor theme_on_accent(void) {
#if defined(PBL_COLOR)
  // Note this reads s_accent directly rather than calling theme_accent(): if
  // the accent was rejected for poor contrast, theme_accent() returns ink, and
  // the fill behind this text is then ink too -- so the answer must be the
  // ink's opposite, which is exactly what deriving from the background gives.
  uint16_t la = luma(s_accent);
  uint16_t lb = luma(s_bg);
  uint16_t gap = (la > lb) ? (la - lb) : (lb - la);
  if (gap < 60) {
    return theme_bg();
  }
  return (la < INK_THRESHOLD) ? GColorWhite : GColorBlack;
#else
  return GColorWhite;
#endif
}

GColor theme_muted(void) {
#if defined(PBL_COLOR)
  return s_dark ? GColorLightGray : GColorDarkGray;
#else
  return GColorBlack;
#endif
}

GColor theme_rule(void) {
#if defined(PBL_COLOR)
  return s_dark ? GColorDarkGray : GColorLightGray;
#else
  return GColorBlack;
#endif
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void theme_init(void) {
  s_bg = persist_exists(PERSIST_KEY_COL_BG)
      ? (uint32_t)persist_read_int(PERSIST_KEY_COL_BG) : DEFAULT_BG;
  s_accent = persist_exists(PERSIST_KEY_COL_ACCENT)
      ? (uint32_t)persist_read_int(PERSIST_KEY_COL_ACCENT) : DEFAULT_ACCENT;
  recompute();
}

void theme_set_bg(uint32_t rgb) {
  rgb &= 0xFFFFFF;
  if (rgb == s_bg) {
    return;   // the phone resends settings on every connect; do not thrash flash
  }
  s_bg = rgb;
  persist_write_int(PERSIST_KEY_COL_BG, (int32_t)rgb);
  recompute();
}

void theme_set_accent(uint32_t rgb) {
  rgb &= 0xFFFFFF;
  if (rgb == s_accent) {
    return;
  }
  s_accent = rgb;
  persist_write_int(PERSIST_KEY_COL_ACCENT, (int32_t)rgb);
}
