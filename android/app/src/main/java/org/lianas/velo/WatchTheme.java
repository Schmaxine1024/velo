package org.lianas.velo;

import android.graphics.Color;

/**
 * The watch's colour derivation, reproduced on the phone.
 *
 * <p>This is a deliberate duplicate of {@code watchapp/src/c/theme.c}. Keep the
 * two in sync: if the constants here drift, the settings preview stops
 * predicting what the watch will actually do, which is worse than having no
 * preview at all.
 *
 * <p>The rider picks a background and an accent; everything else follows from
 * the background's luminance. That is what makes colour customization safe —
 * there is no combination of choices that produces unreadable text.
 */
public final class WatchTheme {

    private WatchTheme() {}

    /** Pebble's greys, which the watch will snap to regardless. */
    private static final int LIGHT_GRAY = 0xAAAAAA;
    private static final int DARK_GRAY  = 0x555555;

    private static final int INK_THRESHOLD = 145;
    private static final int MIN_ACCENT_CONTRAST = 60;

    /** Rec. 601 luma — green dominates because the eye does. */
    public static int luma(int rgb) {
        int r = (rgb >> 16) & 0xFF;
        int g = (rgb >> 8) & 0xFF;
        int b = rgb & 0xFF;
        return (r * 299 + g * 587 + b * 114) / 1000;
    }

    public static boolean isDark(int bg) {
        return luma(bg) < INK_THRESHOLD;
    }

    public static int ink(int bg) {
        return isDark(bg) ? 0xFFFFFF : 0x000000;
    }

    public static int muted(int bg) {
        return isDark(bg) ? LIGHT_GRAY : DARK_GRAY;
    }

    public static int rule(int bg) {
        return isDark(bg) ? DARK_GRAY : LIGHT_GRAY;
    }

    /**
     * The accent as it will actually render — which is not always the accent
     * that was chosen. An accent too close to the background would make the
     * hero number, the one thing read at speed, disappear; the watch falls back
     * to plain ink in that case and so must this.
     */
    public static int effectiveAccent(int bg, int accent) {
        if (Math.abs(luma(accent) - luma(bg)) < MIN_ACCENT_CONTRAST) {
            return ink(bg);
        }
        return accent;
    }

    /** True when the chosen accent will be rejected, so the UI can say so. */
    public static boolean accentRejected(int bg, int accent) {
        return effectiveAccent(bg, accent) != accent;
    }

    public static int opaque(int rgb) {
        return Color.rgb((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }

    /**
     * The colours the watch can actually show.
     *
     * <p>Pebble's display is two bits per channel, so every component is one of
     * 0x00, 0x55, 0xAA, 0xFF. Offering anything else would let the rider pick a
     * shade that silently snaps to a different one on the watch.
     */
    public static final int[] BACKGROUNDS = {
            0xFFFFFF, 0x000000, 0x555555, 0xAAAAAA,
            0x000055, 0x0000AA, 0x005555, 0x005500,
            0x550000, 0x550055, 0xFFFFAA, 0xAAFFFF,
    };

    public static final int[] ACCENTS = {
            0xFF5500, 0xFF0000, 0xFFAA00, 0xFFFF00,
            0x00FF00, 0x00FFAA, 0x00AAFF, 0x5555FF,
            0xFF00FF, 0xFFFFFF, 0x000000, 0xAAAAAA,
    };
}
