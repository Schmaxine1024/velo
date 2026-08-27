package org.lianas.velo;

import java.util.UUID;

/**
 * The wire contract with the watch.
 *
 * <p>This is a mirror of {@code watchapp/src/c/protocol.h}. If you change a key
 * here, change it there too and bump {@link #VERSION} — a stale pairing should
 * be detectable rather than silently wrong.
 *
 * <p>Keys are explicit small integers rather than the SDK's auto-assigned
 * 10000+ range, because this side writes raw integer keys into the dictionary
 * and needs them to be stable and knowable.
 *
 * <p>Division of labour: this app owns every number — it has the fixes and can
 * filter them properly — and the watch owns the buttons and the screen.
 */
public final class Protocol {

    private Protocol() {}

    /** Must match the watchapp's UUID in package.json. */
    public static final UUID WATCHAPP_UUID =
            UUID.fromString("2aa3462c-9d71-4dae-b4c2-bc9a99d032d6");

    public static final int VERSION = 1;

    // ---- Phone -> watch (telemetry) ------------------------------------

    public static final int T_DISTANCE   = 1;   // uint32, metres this ride
    public static final int T_SPEED      = 2;   // uint16, cm/s
    public static final int T_ASCENT     = 3;   // uint16, metres
    public static final int T_FIX        = 4;   // uint8,  FIX_*
    public static final int T_STATE      = 5;   // uint8,  STATE_*
    public static final int T_MAXSPEED   = 6;   // uint16, cm/s
    public static final int T_PHONEBATT  = 7;   // uint8,  percent
    public static final int T_UNITS      = 8;   // uint8,  UNITS_*
    public static final int T_MOVING     = 9;   // uint32, moving seconds
    public static final int T_VERSION    = 10;  // uint8
    public static final int T_START      = 11;  // uint32, unix start time
    public static final int T_COL_BG     = 12;  // uint32, 0xRRGGBB
    public static final int T_COL_ACCENT = 13;  // uint32, 0xRRGGBB
    public static final int T_RIDE_ID    = 14;  // uint32, 0 = no ride

    // ---- Watch -> phone (commands) --------------------------------------

    public static final int C_CMD     = 20;  // uint8, CMD_*
    public static final int C_MOVING  = 21;  // uint32, the watch's moving seconds
    public static final int C_RIDE_ID = 22;  // uint32, the ride this refers to

    public static final int CMD_START  = 1;
    public static final int CMD_PAUSE  = 2;
    public static final int CMD_RESUME = 3;
    public static final int CMD_STOP   = 4;
    public static final int CMD_SYNC   = 5;
    public static final int CMD_LAP    = 6;

    // ---- Shared enumerations --------------------------------------------

    public static final int STATE_IDLE      = 0;
    public static final int STATE_RECORDING = 1;
    public static final int STATE_PAUSED    = 2;

    public static final int FIX_NONE = 0;
    public static final int FIX_POOR = 1;
    public static final int FIX_OK   = 2;
    public static final int FIX_GOOD = 3;

    public static final int UNITS_METRIC   = 0;
    public static final int UNITS_IMPERIAL = 1;

    /**
     * Bucket a horizontal accuracy in metres into the three-bar indicator the
     * watch draws.
     *
     * <p>Bucketing happens here, not on the watch, so the presentation decision
     * sits next to the data that informs it — the watch never renders raw
     * accuracy, so sending it would only invite the two sides to disagree about
     * what "good" means.
     */
    public static int fixFromAccuracy(float metres, boolean hasFix) {
        if (!hasFix) return FIX_NONE;
        if (metres <= 10f) return FIX_GOOD;
        if (metres <= 25f) return FIX_OK;
        return FIX_POOR;
    }
}
