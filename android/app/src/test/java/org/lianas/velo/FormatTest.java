package org.lianas.velo;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

/**
 * Pins {@link Format} to the watch's fmt.c.
 *
 * <p>These expectations are not hand-written. They were produced by compiling
 * watchapp/src/c/fmt.c natively against a small shim for pebble.h and dumping
 * its output for each vector, so this test compares Java against the actual C
 * that runs on the watch rather than against someone's idea of it.
 *
 * <p>The vectors are chosen around the places the two could plausibly part
 * company: truncation versus rounding (1609 m imperial is 0.99 mi, not 1.00),
 * the switch from two decimals to one at 100 units, and the truncated mph
 * conversion factor.
 *
 * <p>To regenerate, see the fmtcheck harness described in the project README.
 */
public class FormatTest {

    @Test
    public void matchesWatchImplementation() {
        assertEquals("0.00", Format.distance(0, false));
        assertEquals("0.00", Format.distance(0, true));
        assertEquals("0.00", Format.distance(5, false));
        assertEquals("0.00", Format.distance(5, true));
        assertEquals("0.99", Format.distance(999, false));
        assertEquals("0.62", Format.distance(999, true));
        assertEquals("1.00", Format.distance(1000, false));
        assertEquals("0.62", Format.distance(1000, true));
        assertEquals("1.60", Format.distance(1609, false));
        assertEquals("0.99", Format.distance(1609, true));
        assertEquals("12.34", Format.distance(12340, false));
        assertEquals("7.66", Format.distance(12340, true));
        assertEquals("99.99", Format.distance(99999, false));
        assertEquals("62.13", Format.distance(99999, true));
        assertEquals("123.4", Format.distance(123456, false));
        assertEquals("76.71", Format.distance(123456, true));
        assertEquals("500.0", Format.distance(500000, false));
        assertEquals("310.6", Format.distance(500000, true));
        assertEquals("0.0", Format.speed(0, false));
        assertEquals("0.0", Format.speed(0, true));
        assertEquals("0.0", Format.speed(1, false));
        assertEquals("0.0", Format.speed(1, true));
        assertEquals("3.6", Format.speed(100, false));
        assertEquals("2.2", Format.speed(100, true));
        assertEquals("36.1", Format.speed(1005, false));
        assertEquals("22.4", Format.speed(1005, true));
        assertEquals("36.0", Format.speed(1000, false));
        assertEquals("22.3", Format.speed(1000, true));
        assertEquals("29.9", Format.speed(833, false));
        assertEquals("18.6", Format.speed(833, true));
        assertEquals("99.9", Format.speed(2777, false));
        assertEquals("62.1", Format.speed(2777, true));
        assertEquals("2359.2", Format.speed(65535, false));
        assertEquals("1466.0", Format.speed(65535, true));
        assertEquals("0", Format.ascent(0, false));
        assertEquals("0", Format.ascent(0, true));
        assertEquals("1", Format.ascent(1, false));
        assertEquals("3", Format.ascent(1, true));
        assertEquals("100", Format.ascent(100, false));
        assertEquals("328", Format.ascent(100, true));
        assertEquals("500", Format.ascent(500, false));
        assertEquals("1640", Format.ascent(500, true));
        assertEquals("1000", Format.ascent(1000, false));
        assertEquals("3281", Format.ascent(1000, true));
        assertEquals("65535", Format.ascent(65535, false));
        assertEquals("215020", Format.ascent(65535, true));
        assertEquals("0:00", Format.duration(0L));
        assertEquals("0:59", Format.duration(59L));
        assertEquals("1:00", Format.duration(60L));
        assertEquals("9:59", Format.duration(599L));
        assertEquals("59:59", Format.duration(3599L));
        assertEquals("1:00:00", Format.duration(3600L));
        assertEquals("2:45:12", Format.duration(9912L));
        assertEquals("23:59:59", Format.duration(86399L));
    }
}
