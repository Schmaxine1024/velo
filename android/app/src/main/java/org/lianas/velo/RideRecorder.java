package org.lianas.velo;

import android.location.Location;

import androidx.annotation.VisibleForTesting;

/**
 * Turns a stream of GPS fixes into the numbers a ride is made of.
 *
 * <p>This is the only place in either app where those numbers are decided.
 * Keeping it free of services, contexts and Bluetooth is deliberate: it makes
 * the arithmetic reviewable on its own, and it means the watch is displaying a
 * value rather than computing a second opinion that could drift.
 *
 * <p>Everything here is defensive about the input, because raw GPS is not the
 * clean track people imagine. A phone sitting still on a wall will happily
 * report a kilometre of travel over an hour if you sum every reported position
 * change, and altitude is noisier still.
 */
public class RideRecorder {

    /** Fixes worse than this are discarded outright rather than filtered. */
    private static final float MAX_ACCURACY_M = 50f;

    /** No cyclist covers this much ground between two 1 Hz fixes. */
    private static final float MAX_STEP_SPEED_MPS = 30f;   // 108 km/h

    /**
     * Steps shorter than this are treated as the receiver wandering.
     *
     * <p>Fixed, and deliberately small. An earlier version scaled this with the
     * reported accuracy — {@code max(2, accuracy * 0.5)} — which sounds prudent
     * and is catastrophic: at a perfectly ordinary 20 m accuracy the floor
     * becomes 10 m, and at 1 Hz that discards every step slower than 36 km/h.
     * The ride records almost no distance, and since a discarded step also
     * looked like zero speed, auto-pause then stopped the ride entirely.
     *
     * <p>Standing-still jitter is rejected by {@link #STATIONARY_MPS} instead,
     * which is a far better instrument for the job: it asks the receiver
     * whether it is moving rather than inferring it from position noise.
     */
    private static final float MIN_STEP_M = 2.0f;

    /**
     * Below this the receiver considers itself stationary, so position changes
     * are noise rather than travel. Doppler speed is near-zero when still even
     * while the reported position wanders, which is exactly the discrimination
     * a distance filter needs.
     */
    private static final float STATIONARY_MPS = 0.5f;

    /**
     * Altitude must move this far from the running anchor before any of it
     * counts. GPS altitude wanders by several metres while stationary, and
     * without a deadband a flat two-hour ride accumulates hundreds of metres
     * of entirely fictional climbing.
     */
    private static final double ASCENT_DEADBAND_M = 3.0;

    /** Smoothing on altitude before the deadband sees it. */
    private static final double ALT_SMOOTHING = 0.3;

    /** Below this, with auto-pause on, the rider is considered stopped. */
    private static final float AUTOPAUSE_SPEED_MPS = 1.0f;

    /** ...and only after this many consecutive seconds, to ride out a stall. */
    private static final int AUTOPAUSE_DELAY_S = 8;

    private int state = Protocol.STATE_IDLE;
    private long rideId;
    private long startTimeMs;

    private double distanceM;
    private double ascentM;
    /** The receiver's latest speed, maintained in every state. See onLocation. */
    private float lastFixSpeedMps;
    private float maxSpeedMps;
    private long movingSeconds;

    private Location lastAccepted;
    private double smoothedAlt;
    private double altAnchor;
    private boolean haveAlt;

    private int fix = Protocol.FIX_NONE;
    private boolean autoPauseEnabled = true;
    private boolean pausedAutomatically;
    private int slowSeconds;

    // ---- Lifecycle -------------------------------------------------------

    public void start(long rideId, long startTimeMs) {
        this.rideId = rideId;
        this.startTimeMs = startTimeMs;
        state = Protocol.STATE_RECORDING;

        distanceM = 0;
        ascentM = 0;
        lastFixSpeedMps = 0;
        maxSpeedMps = 0;
        movingSeconds = 0;
        lastAccepted = null;
        haveAlt = false;
        pausedAutomatically = false;
        slowSeconds = 0;
        // fix is deliberately left alone: the receiver's health does not reset
        // just because a new ride began.
    }

    public void pause() {
        if (state == Protocol.STATE_RECORDING) {
            state = Protocol.STATE_PAUSED;
            pausedAutomatically = false;
            // lastFixSpeedMps is deliberately NOT cleared: getSpeedCms already
            // reports zero while paused, and the real value has to keep
            // updating or auto-resume has nothing to watch.
        }
    }

    public void resume() {
        if (state == Protocol.STATE_PAUSED) {
            state = Protocol.STATE_RECORDING;
            pausedAutomatically = false;
            slowSeconds = 0;
            // Drop the anchor: the gap across a pause is not travel, and
            // joining across it would draw a straight line through the café.
            lastAccepted = null;
        }
    }

    public void stop() {
        state = Protocol.STATE_IDLE;
        lastFixSpeedMps = 0;
        rideId = 0;
    }

    // ---- Input -----------------------------------------------------------

    /**
     * Fold a fix into the ride.
     *
     * @return true if this fix should be persisted as a track point — false for
     *         fixes rejected as noise, which should not reach the GPX either.
     */
    public boolean onLocation(Location loc) {
        // This method's only job is to unwrap the Location and remember the
        // reference fix. All the decisions live in onSample, which takes
        // primitives and is therefore reachable from a plain JVM test --
        // Location cannot be constructed in one.
        boolean haveReference = lastAccepted != null;
        float step = haveReference ? lastAccepted.distanceTo(loc) : 0f;
        double dt = haveReference
                ? (loc.getTime() - lastAccepted.getTime()) / 1000.0
                : 0.0;

        boolean accepted = onSample(
                loc.hasAccuracy() ? loc.getAccuracy() : Float.MAX_VALUE,
                loc.hasAccuracy(),
                loc.hasSpeed(), loc.getSpeed(),
                loc.hasAltitude(), loc.getAltitude(),
                step, dt, haveReference);

        if (accepted) {
            lastAccepted = new Location(loc);
        }
        return accepted;
    }

    /**
     * The whole of the per-fix decision, in primitives.
     *
     * @param stepM        metres from the last accepted fix, 0 if none
     * @param dtSeconds    seconds since it, 0 if none
     * @param haveReference false for the first fix of a ride or after a resume
     */
    @VisibleForTesting
    boolean onSample(float accuracy, boolean hasAccuracy,
                     boolean hasSpeed, float speedMps,
                     boolean hasAltitude, double altitude,
                     float stepM, double dtSeconds, boolean haveReference) {

        fix = Protocol.fixFromAccuracy(accuracy, hasAccuracy);

        if (accuracy > MAX_ACCURACY_M) {
            return false;
        }

        // Track the receiver's own speed whatever the state, and before the
        // recording check. Auto-resume works by noticing movement *while
        // paused*, so a paused recorder that stops reading speed can never
        // un-pause itself -- auto-pause becomes a one-way trap.
        //
        // Doppler speed is preferred throughout: it comes from carrier phase
        // rather than from differencing two noisy positions, so it is smoother
        // and more accurate than step/dt, and it stays meaningful even when the
        // position is not moving enough to measure.
        if (hasSpeed) {
            lastFixSpeedMps = speedMps;
        }

        if (state != Protocol.STATE_RECORDING) {
            // Fix quality above is still worth having: the ready screen shows
            // it before a ride starts, which is how the rider knows whether
            // starting now is worth anything.
            return false;
        }

        updateAltitude(hasAltitude, altitude);

        if (!haveReference) {
            return true;   // first fix; nothing to measure a step against yet
        }

        if (!isTravel(stepM, dtSeconds, hasSpeed, speedMps)) {
            return false;
        }

        distanceM += stepM;

        if (!hasSpeed) {
            // No Doppler on this fix; fall back to differencing. Only reached
            // on receivers that do not report speed at all.
            lastFixSpeedMps = (float) (stepM / dtSeconds);
        }
        if (lastFixSpeedMps > maxSpeedMps && lastFixSpeedMps <= MAX_STEP_SPEED_MPS) {
            maxSpeedMps = lastFixSpeedMps;
        }
        return true;
    }

    /**
     * Does this step between two fixes represent actual travel?
     *
     * <p>Pulled out as a pure static so it can be tested without an Android
     * {@link Location}. That matters: the two worst bugs this class has had
     * both lived in exactly these four lines, and neither was reachable by a
     * test while the logic was buried inside a method taking a Location.
     *
     * @param stepM     metres between this fix and the last accepted one
     * @param dtSeconds seconds between them
     * @param hasSpeed  whether the receiver reported a Doppler speed
     * @param speedMps  that speed, meaningless if {@code hasSpeed} is false
     */
    @VisibleForTesting
    static boolean isTravel(float stepM, double dtSeconds, boolean hasSpeed, float speedMps) {
        if (dtSeconds <= 0) {
            return false;   // duplicate or out-of-order fix
        }
        // Standing still: the position may be wandering by metres, but the
        // receiver knows it is not moving.
        if (hasSpeed && speedMps < STATIONARY_MPS) {
            return false;
        }
        if (stepM < MIN_STEP_M) {
            return false;
        }
        // Teleport; almost always a re-acquisition artefact.
        return !(stepM / dtSeconds > MAX_STEP_SPEED_MPS);
    }

    private void updateAltitude(boolean hasAltitude, double alt) {
        if (!hasAltitude) {
            return;
        }

        if (!haveAlt) {
            smoothedAlt = alt;
            altAnchor = alt;
            haveAlt = true;
            return;
        }

        smoothedAlt += ALT_SMOOTHING * (alt - smoothedAlt);

        if (smoothedAlt > altAnchor + ASCENT_DEADBAND_M) {
            ascentM += smoothedAlt - altAnchor;
            altAnchor = smoothedAlt;
        } else if (smoothedAlt < altAnchor - ASCENT_DEADBAND_M) {
            // Descending only moves the anchor. Ascent is a one-way total, so
            // a rolling course does not cancel itself out to zero.
            altAnchor = smoothedAlt;
        }
    }

    /** Called once a second. Advances moving time and evaluates auto-pause. */
    public void tick() {
        if (state == Protocol.STATE_RECORDING) {
            movingSeconds++;

            if (autoPauseEnabled) {
                if (lastFixSpeedMps < AUTOPAUSE_SPEED_MPS) {
                    if (++slowSeconds >= AUTOPAUSE_DELAY_S) {
                        state = Protocol.STATE_PAUSED;
                        pausedAutomatically = true;
                    }
                } else {
                    slowSeconds = 0;
                }
            }
        } else if (state == Protocol.STATE_PAUSED && pausedAutomatically) {
            // Only un-pause what we paused. A rider who pressed pause on
            // purpose does not want rolling forward at a junction to restart
            // the clock behind their back.
            if (lastFixSpeedMps >= AUTOPAUSE_SPEED_MPS) {
                state = Protocol.STATE_RECORDING;
                pausedAutomatically = false;
                slowSeconds = 0;
                lastAccepted = null;
            }
        }
    }

    // ---- Output ----------------------------------------------------------

    public int getState()          { return state; }
    public long getRideId()        { return rideId; }
    public long getStartTimeMs()   { return startTimeMs; }
    public long getMovingSeconds() { return movingSeconds; }
    public int getFix()            { return fix; }

    public int getDistanceM()   { return (int) Math.round(distanceM); }
    public int getAscentM()     { return (int) Math.round(ascentM); }

    /**
     * Speed in cm/s, the unit the wire and the watch both use.
     *
     * <p>Reports zero unless actually recording. The underlying reading keeps
     * updating while paused so auto-resume can see movement, but showing a
     * live speed beside a stopped clock would just look broken.
     */
    public int getSpeedCms() {
        if (state != Protocol.STATE_RECORDING) {
            return 0;
        }
        return (int) Math.round(lastFixSpeedMps * 100);
    }
    public int getMaxSpeedCms() { return (int) Math.round(maxSpeedMps * 100); }

    public void setAutoPauseEnabled(boolean enabled) {
        autoPauseEnabled = enabled;
        if (!enabled && state == Protocol.STATE_PAUSED && pausedAutomatically) {
            // Turning the feature off should release a pause it had imposed,
            // otherwise the ride stays stopped for a reason that no longer
            // exists and the rider has no obvious way to undo.
            resume();
        }
    }

    public boolean isAutoPauseEnabled() { return autoPauseEnabled; }
}
