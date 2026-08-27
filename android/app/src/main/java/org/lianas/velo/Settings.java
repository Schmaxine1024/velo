package org.lianas.velo;

import android.content.Context;
import android.content.SharedPreferences;

/** User preferences. Small enough that SharedPreferences is the right answer. */
public class Settings {

    private static final String PREFS = "velo";

    private static final String KEY_UNITS      = "units";
    private static final String KEY_AUTOPAUSE  = "autopause";
    private static final String KEY_COL_BG     = "col_bg";
    private static final String KEY_COL_ACCENT = "col_accent";

    /** Matches the watch's own defaults in theme.c, so an unconfigured watch
     *  and an unconfigured phone agree without exchanging anything. */
    public static final int DEFAULT_BG     = 0xFFFFFF;
    public static final int DEFAULT_ACCENT = 0xFF5500;

    private final SharedPreferences prefs;

    public Settings(Context context) {
        prefs = context.getApplicationContext()
                .getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    public int getUnits() {
        return prefs.getInt(KEY_UNITS, Protocol.UNITS_METRIC);
    }

    public void setUnits(int units) {
        prefs.edit().putInt(KEY_UNITS, units).apply();
    }

    public boolean isImperial() {
        return getUnits() == Protocol.UNITS_IMPERIAL;
    }

    public boolean isAutoPause() {
        return prefs.getBoolean(KEY_AUTOPAUSE, true);
    }

    public void setAutoPause(boolean on) {
        prefs.edit().putBoolean(KEY_AUTOPAUSE, on).apply();
    }

    public int getBackgroundColor() {
        return prefs.getInt(KEY_COL_BG, DEFAULT_BG);
    }

    public void setBackgroundColor(int rgb) {
        prefs.edit().putInt(KEY_COL_BG, rgb & 0xFFFFFF).apply();
    }

    public int getAccentColor() {
        return prefs.getInt(KEY_COL_ACCENT, DEFAULT_ACCENT);
    }

    public void setAccentColor(int rgb) {
        prefs.edit().putInt(KEY_COL_ACCENT, rgb & 0xFFFFFF).apply();
    }
}
