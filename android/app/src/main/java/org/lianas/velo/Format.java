package org.lianas.velo;

import java.util.Locale;

/**
 * Number to string, in the rider's chosen units.
 *
 * <p>This is a deliberate line-by-line mirror of {@code watchapp/src/c/fmt.c},
 * <b>including its integer arithmetic</b>, and that matters more than it looks.
 *
 * <p>The obvious Java implementation — convert to double, hand it to
 * {@code String.format("%.2f")} — is not equivalent, because the C truncates
 * where {@code String.format} rounds half-up. At 1609 m the watch would read
 * 0.99 mi and the phone 1.00 mi for the same instant of the same ride. Small,
 * but it is precisely the drift the single-authority design exists to prevent,
 * and the sort of thing a rider notices and cannot explain.
 *
 * <p>So the fixed-point contortions are reproduced here even though this side
 * has perfectly good hardware floating point. The unit tests pin the two
 * implementations together; if you change one, change both.
 */
public final class Format {

    private Format() {}

    /** Millimetres in a mile — the same constant, in the same units, as fmt.c. */
    private static final long MM_PER_MILE = 1609344L;

    /** "12.34" below 100, "123.4" at or above, truncated not rounded. */
    public static String distance(int metres, boolean imperial) {
        long hundredths = imperial
                ? (metres * 100000L) / MM_PER_MILE
                : metres / 10L;

        long whole = hundredths / 100;
        if (whole >= 100) {
            // Three integer digits already; one decimal is all that still fits.
            return String.format(Locale.US, "%d.%d", whole, (hundredths % 100) / 10);
        }
        return String.format(Locale.US, "%d.%02d", whole, hundredths % 100);
    }

    public static String distanceUnit(boolean imperial) {
        return imperial ? "mi" : "km";
    }

    /**
     * "24.3". Centimetres per second in.
     *
     * <p>The mph factor is truncated at four digits exactly as on the watch;
     * the error is 0.003%, three orders of magnitude below GPS speed noise, but
     * it has to be the <em>same</em> error on both screens.
     */
    public static String speed(int cms, boolean imperial) {
        long tenths = imperial
                ? (cms * 2237L) / 10000L
                : (cms * 36L) / 100L;
        return String.format(Locale.US, "%d.%d", tenths / 10, tenths % 10);
    }

    public static String speedUnit(boolean imperial) {
        return imperial ? "mph" : "km/h";
    }

    public static String ascent(int metres, boolean imperial) {
        long v = imperial ? (metres * 3281L) / 1000L : metres;
        return String.valueOf(v);
    }

    public static String ascentUnit(boolean imperial) {
        return imperial ? "ft" : "m";
    }

    /** "45:12" under an hour, "2:45:12" over it. */
    public static String duration(long seconds) {
        long h = seconds / 3600;
        long m = (seconds % 3600) / 60;
        long s = seconds % 60;
        return h > 0
                ? String.format(Locale.US, "%d:%02d:%02d", h, m, s)
                : String.format(Locale.US, "%d:%02d", m, s);
    }
}
