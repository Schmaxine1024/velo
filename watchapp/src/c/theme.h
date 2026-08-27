// Colours, set from the phone and persisted on the watch.
//
// Only two are chosen by the rider: the background and the accent. Everything
// else -- ink, muted label grey, hairline rules -- is derived from the
// background's luminance.
//
// That derivation is the whole point. Letting someone pick a dark background
// and keeping black text would produce an unreadable watchapp, and asking them
// to pick five coordinated colours to avoid that is a worse deal than picking
// one and having the rest follow. The rider chooses the mood; the watch keeps
// the contrast.
//
// Diorite has no colour at all. Rather than let GColorFromRGB collapse an
// arbitrary pair to two indistinguishable greys, it is pinned to black on
// white and the settings simply have no effect there.

#pragma once

#include <pebble.h>

void theme_init(void);

// 24-bit 0xRRGGBB, as sent by the phone. Persisted, so the look survives the
// phone being out of range at launch.
void theme_set_bg(uint32_t rgb);
void theme_set_accent(uint32_t rgb);

GColor theme_bg(void);
GColor theme_ink(void);       // black or white, whichever the background allows
GColor theme_accent(void);    // the rider's accent, forced to ink if unreadable
GColor theme_muted(void);     // labels: ink, stepped toward the background
GColor theme_rule(void);      // hairlines: the faintest still-visible step

// Ink to use *on top of* the accent, for the one place the accent is a fill
// rather than a stroke: the selected row of the history menu. Derived from the
// accent's own luminance, because a rider who picks yellow would otherwise get
// white text on yellow.
GColor theme_on_accent(void);
