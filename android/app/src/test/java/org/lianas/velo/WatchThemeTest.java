package org.lianas.velo;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/**
 * Pins {@link WatchTheme} to the watch's theme.c.
 *
 * <p>The settings screen previews what the watch will look like, so if this
 * derivation drifts from the C the preview becomes a confident lie. These are
 * the constants and boundaries that theme.c actually uses.
 *
 * <p>Only the pure functions are exercised: {@code opaque()} calls into
 * android.graphics.Color, which is not available to a plain JVM test.
 */
public class WatchThemeTest {

    @Test
    public void lumaUsesRec601Weights() {
        // Green dominates because the eye does. A full-value blue is much
        // darker to look at than a full-value yellow, and theme.c relies on
        // that to decide ink colour.
        assertEquals(0, WatchTheme.luma(0x000000));
        // The weights sum to exactly 1000, so white lands on 255 with no
        // rounding loss: 255 * (299+587+114) / 1000.
        assertEquals(255, WatchTheme.luma(0xFFFFFF));
        assertEquals(76, WatchTheme.luma(0xFF0000));
        assertEquals(149, WatchTheme.luma(0x00FF00));
        assertEquals(29, WatchTheme.luma(0x0000FF));
    }

    @Test
    public void inkFlipsAtTheThreshold() {
        // theme.c uses 145, set slightly above mid-grey because black text on a
        // mid-tone reads better than white does -- the tie breaks toward dark.
        assertEquals(0x000000, WatchTheme.ink(0xFFFFFF));
        assertEquals(0xFFFFFF, WatchTheme.ink(0x000000));
        assertEquals(0xFFFFFF, WatchTheme.ink(0x000055));
        assertEquals(0x000000, WatchTheme.ink(0xFFFFAA));
        assertEquals(0x000000, WatchTheme.ink(0xAAAAAA));
        assertEquals(0xFFFFFF, WatchTheme.ink(0x555555));
    }

    @Test
    public void greysInvertWithTheBackground() {
        assertEquals(0x555555, WatchTheme.muted(0xFFFFFF));
        assertEquals(0xAAAAAA, WatchTheme.muted(0x000000));
        assertEquals(0xAAAAAA, WatchTheme.rule(0xFFFFFF));
        assertEquals(0x555555, WatchTheme.rule(0x000000));
    }

    @Test
    public void lowContrastAccentFallsBackToInk() {
        // The failure this prevents: the hero number -- the one thing read at
        // speed -- rendered in a colour indistinguishable from the background.
        assertTrue(WatchTheme.accentRejected(0xFFFFFF, 0xFFFFAA));
        assertEquals(0x000000, WatchTheme.effectiveAccent(0xFFFFFF, 0xFFFFAA));

        assertTrue(WatchTheme.accentRejected(0x000000, 0x000055));
        assertEquals(0xFFFFFF, WatchTheme.effectiveAccent(0x000000, 0x000055));
    }

    @Test
    public void goodContrastAccentSurvivesUnchanged() {
        assertFalse(WatchTheme.accentRejected(0xFFFFFF, 0xFF5500));
        assertEquals(0xFF5500, WatchTheme.effectiveAccent(0xFFFFFF, 0xFF5500));

        assertFalse(WatchTheme.accentRejected(0x000000, 0x00AAFF));
        assertEquals(0x00AAFF, WatchTheme.effectiveAccent(0x000000, 0x00AAFF));
    }

    @Test
    public void everyOfferedSwatchIsOnThePebblePalette() {
        // Pebble's display is two bits per channel. Offering a shade outside
        // {0x00,0x55,0xAA,0xFF} would let the rider pick a colour that silently
        // snaps to a different one on the watch.
        for (int rgb : WatchTheme.BACKGROUNDS) assertOnPalette(rgb);
        for (int rgb : WatchTheme.ACCENTS) assertOnPalette(rgb);
    }

    private static void assertOnPalette(int rgb) {
        for (int shift : new int[]{16, 8, 0}) {
            int c = (rgb >> shift) & 0xFF;
            assertTrue("0x" + Integer.toHexString(rgb) + " is off-palette",
                    c == 0x00 || c == 0x55 || c == 0xAA || c == 0xFF);
        }
    }

    @Test
    public void defaultsMatchTheWatchDefaults() {
        // theme.c ships white-on-orange, so an unconfigured watch and an
        // unconfigured phone must agree without exchanging anything.
        assertEquals(0xFFFFFF, Settings.DEFAULT_BG);
        assertEquals(0xFF5500, Settings.DEFAULT_ACCENT);
    }
}
