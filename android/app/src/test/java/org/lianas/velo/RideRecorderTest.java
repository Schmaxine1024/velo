package org.lianas.velo;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/**
 * Regression tests for the two worst bugs this class has had.
 *
 * <p>Both shipped, both were found by reading rather than by running, and both
 * were in logic no test could reach — which is why {@code isTravel} is a pure
 * static and {@code onSample} takes primitives instead of a {@link
 * android.location.Location}, a class that cannot be constructed in a JVM test.
 *
 * <p>{@link #feedSpeed} deliberately goes through {@code onSample} rather than
 * poking a field. The auto-resume bug was precisely that the real intake path
 * stopped updating speed once paused; a test that set the field directly would
 * have passed against the broken code and proved nothing.
 */
public class RideRecorderTest {

    /**
     * Deliver one fix at the given speed through the ordinary intake path.
     *
     * <p>haveReference is false, so no step is measured — this exercises the
     * speed-tracking and state-machine behaviour without also asserting
     * anything about distance.
     */
    private static void feedSpeed(RideRecorder r, float mps) {
        r.onSample(5f, true, true, mps, false, 0, 0f, 0, false);
    }

    // ---- The noise floor used to reject ordinary cycling ------------------

    @Test
    public void acceptsNormalCyclingAtMediocreAccuracy() {
        // The regression: an accuracy-scaled floor of max(2, accuracy * 0.5)
        // meant a 20 m fix demanded a 10 m step, so at 1 Hz nothing below
        // 36 km/h counted as movement at all. A ride recorded almost no
        // distance and then auto-paused itself, because a rejected step also
        // read as zero speed.
        //
        // 5 m/s is 18 km/h: unremarkable riding, and it must count.
        assertTrue(RideRecorder.isTravel(5.0f, 1.0, true, 5.0f));

        // Slower still — a steep climb at 8 km/h.
        assertTrue(RideRecorder.isTravel(2.2f, 1.0, true, 2.2f));
    }

    @Test
    public void rejectsStationaryJitter() {
        // Parked, but the reported position is wandering by five metres. The
        // receiver knows it is not moving, and that is the signal we trust
        // rather than trying to infer it from position noise.
        assertFalse(RideRecorder.isTravel(5.0f, 1.0, true, 0.1f));
    }

    @Test
    public void rejectsSubMetreWander() {
        assertFalse(RideRecorder.isTravel(1.4f, 1.0, true, 1.4f));
    }

    @Test
    public void rejectsTeleports() {
        // 200 m in one second is a re-acquisition artefact, not a descent.
        assertFalse(RideRecorder.isTravel(200f, 1.0, true, 200f));
    }

    @Test
    public void rejectsOutOfOrderFixes() {
        assertFalse(RideRecorder.isTravel(10f, 0.0, true, 10f));
        assertFalse(RideRecorder.isTravel(10f, -1.0, true, 10f));
    }

    @Test
    public void withoutDopplerFallsBackToStepDistanceAlone() {
        // Receivers that report no speed still get their steps counted; only
        // the stationary test is unavailable.
        assertTrue(RideRecorder.isTravel(5.0f, 1.0, false, 0f));
        assertFalse(RideRecorder.isTravel(0.5f, 1.0, false, 0f));
    }

    // ---- Auto-pause used to be a one-way trap ----------------------------

    @Test
    public void autoPauseEngagesWhenStopped() {
        RideRecorder r = new RideRecorder();
        r.setAutoPauseEnabled(true);
        r.start(1, 0);

        feedSpeed(r, 0f);
        for (int i = 0; i < 10; i++) {
            r.tick();
        }

        assertEquals(Protocol.STATE_PAUSED, r.getState());
    }

    @Test
    public void autoPauseReleasesWhenMovingAgain() {
        // The regression: onLocation returned before updating speed whenever
        // the state was not RECORDING, and pause() zeroed the speed. So once
        // auto-paused, the speed could never rise again and the ride stayed
        // stopped until the rider noticed and intervened.
        RideRecorder r = new RideRecorder();
        r.setAutoPauseEnabled(true);
        r.start(1, 0);

        feedSpeed(r, 0f);
        for (int i = 0; i < 10; i++) {
            r.tick();
        }
        assertEquals(Protocol.STATE_PAUSED, r.getState());

        feedSpeed(r, 6f);   // rolling away from the lights
        r.tick();

        assertEquals(Protocol.STATE_RECORDING, r.getState());
    }

    @Test
    public void manualPauseIsNotUndoneByMovement() {
        // Rolling forward at a junction must not restart a clock the rider
        // stopped on purpose.
        RideRecorder r = new RideRecorder();
        r.setAutoPauseEnabled(true);
        r.start(1, 0);
        r.pause();

        feedSpeed(r, 6f);
        for (int i = 0; i < 10; i++) {
            r.tick();
        }

        assertEquals(Protocol.STATE_PAUSED, r.getState());
    }

    @Test
    public void speedReadsZeroWhilePausedButKeepsTrackingUnderneath() {
        RideRecorder r = new RideRecorder();
        r.setAutoPauseEnabled(true);
        r.start(1, 0);
        r.pause();

        feedSpeed(r, 6f);
        // Nothing to show beside a stopped clock...
        assertEquals(0, r.getSpeedCms());

        // ...but the reading is still live, which is what auto-resume needs.
        r.resume();
        assertEquals(600, r.getSpeedCms());
    }

    @Test
    public void movingTimeOnlyAdvancesWhileRecording() {
        RideRecorder r = new RideRecorder();
        r.setAutoPauseEnabled(false);
        r.start(1, 0);

        r.tick();
        r.tick();
        assertEquals(2, r.getMovingSeconds());

        r.pause();
        r.tick();
        r.tick();
        assertEquals(2, r.getMovingSeconds());
    }

    @Test
    public void disablingAutoPauseReleasesAPauseItImposed() {
        RideRecorder r = new RideRecorder();
        r.setAutoPauseEnabled(true);
        r.start(1, 0);

        feedSpeed(r, 0f);
        for (int i = 0; i < 10; i++) {
            r.tick();
        }
        assertEquals(Protocol.STATE_PAUSED, r.getState());

        // Turning the feature off must not leave the ride stopped for a reason
        // that no longer exists.
        r.setAutoPauseEnabled(false);
        assertEquals(Protocol.STATE_RECORDING, r.getState());
    }
}
