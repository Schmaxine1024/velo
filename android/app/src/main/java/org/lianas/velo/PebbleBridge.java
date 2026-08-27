package org.lianas.velo;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.IntentFilter;
import android.util.Log;

import androidx.core.content.ContextCompat;

import com.getpebble.android.kit.Constants;
import com.getpebble.android.kit.PebbleKit;
import com.getpebble.android.kit.util.PebbleDictionary;

/**
 * Everything that talks to the watch.
 *
 * <p>Transport is PebbleKit's broadcast-intent API: this app broadcasts to the
 * Pebble Android app, which relays over Bluetooth. PebbleKit itself is vendored
 * (see {@code com.getpebble.android.kit}) rather than pulled from jcenter,
 * which no longer exists.
 */
public class PebbleBridge {

    private static final String TAG = "VeloBridge";

    public interface CommandListener {
        void onStartRequested();
        void onPauseRequested(long rideId);
        void onResumeRequested(long rideId);
        void onStopRequested(long rideId, long watchMovingSeconds);
        void onSyncRequested();
    }

    private final Context context;
    private final CommandListener listener;
    private BroadcastReceiver receiver;

    /**
     * Whether the static settings still need sending.
     *
     * <p>Units, both theme colours and the protocol version change perhaps
     * twice in the life of an install, but they were originally packed into
     * every telemetry frame — a third of the payload, 3,600 times an hour,
     * carrying no news. They are now sent on connect and on change only.
     */
    private boolean configDirty = true;

    public PebbleBridge(Context context, CommandListener listener) {
        this.context = context.getApplicationContext();
        this.listener = listener;
    }

    // ---- Lifecycle -------------------------------------------------------

    public void start() {
        receiver = new PebbleKit.PebbleDataReceiver(Protocol.WATCHAPP_UUID) {
            @Override
            public void receiveData(Context ctx, int transactionId, PebbleDictionary data) {
                // The watch's AppMessage will time out if this is not
                // acknowledged, so ack first and interpret afterwards.
                PebbleKit.sendAckToPebble(ctx, transactionId);
                handleCommand(data);
            }
        };

        // Registered here rather than via PebbleKit.registerReceivedDataHandler.
        //
        // That helper dates from 2016 and calls registerReceiver(receiver,
        // filter) with no export flag. Since Android 14 that is a hard
        // SecurityException for any receiver not listening exclusively to
        // system broadcasts, so the helper takes the whole app down the instant
        // the service is created. The fix belongs here, in our code, rather
        // than as a patch to the vendored source.
        //
        // EXPORTED, not NOT_EXPORTED: the broadcast we are waiting for comes
        // from the Pebble app, a different UID. NOT_EXPORTED would register
        // cleanly and then silently never receive anything, which is a worse
        // failure than the crash.
        ContextCompat.registerReceiver(context, receiver,
                new IntentFilter(Constants.INTENT_APP_RECEIVE),
                ContextCompat.RECEIVER_EXPORTED);
    }

    public void stop() {
        if (receiver != null) {
            try {
                context.unregisterReceiver(receiver);
            } catch (IllegalArgumentException ignored) {
                // Already gone; unregistering twice is not worth crashing over.
            }
            receiver = null;
        }
    }

    public boolean isWatchConnected() {
        try {
            return PebbleKit.isWatchConnected(context);
        } catch (Exception e) {
            // isWatchConnected queries a ContentProvider in the Pebble app. If
            // that app is missing or locked down, treat it as "not connected"
            // rather than taking the service down with it.
            return false;
        }
    }

    public void launchWatchapp() {
        try {
            PebbleKit.startAppOnPebble(context, Protocol.WATCHAPP_UUID);
        } catch (Exception e) {
            Log.w(TAG, "could not launch watchapp", e);
        }
    }

    /** Call when units or colours change, so the next frame carries them. */
    public void markConfigDirty() {
        configDirty = true;
    }

    // ---- Inbound ---------------------------------------------------------

    private void handleCommand(PebbleDictionary data) {
        try {
            dispatchCommand(data);
        } catch (Exception e) {
            // getUnsignedIntegerAsLong throws if a key arrives with a type it
            // did not expect. This runs inside a BroadcastReceiver, so letting
            // it escape takes the whole app down -- mid-ride -- over one
            // malformed packet from a mismatched watchapp build.
            Log.w(TAG, "ignoring malformed command", e);
        }
    }

    private void dispatchCommand(PebbleDictionary data) {
        Long cmd = data.getUnsignedIntegerAsLong(Protocol.C_CMD);
        if (cmd == null) {
            return;
        }

        Long rideId = data.getUnsignedIntegerAsLong(Protocol.C_RIDE_ID);
        long id = rideId == null ? 0 : rideId;

        switch (cmd.intValue()) {
            case Protocol.CMD_START:
                listener.onStartRequested();
                break;
            case Protocol.CMD_PAUSE:
                listener.onPauseRequested(id);
                break;
            case Protocol.CMD_RESUME:
                listener.onResumeRequested(id);
                break;
            case Protocol.CMD_STOP: {
                Long moving = data.getUnsignedIntegerAsLong(Protocol.C_MOVING);
                listener.onStopRequested(id, moving == null ? 0 : moving);
                break;
            }
            case Protocol.CMD_SYNC:
                // A sync makes the watch's whole world stale, including the
                // settings, so resend those too rather than only the numbers.
                configDirty = true;
                listener.onSyncRequested();
                break;
            default:
                Log.w(TAG, "unknown command " + cmd);
                break;
        }
    }

    // ---- Outbound --------------------------------------------------------

    /**
     * Send one telemetry frame.
     *
     * <p>Static settings ride along only when dirty. Note this is fire and
     * forget: there is no retry, because the next frame is a second away and
     * carries strictly fresher numbers. A resent stale frame would be worse
     * than a dropped one.
     */
    public void sendTelemetry(RideRecorder r, int batteryPercent, Settings settings) {
        PebbleDictionary d = new PebbleDictionary();

        d.addUint32(Protocol.T_DISTANCE, r.getDistanceM());
        d.addUint16(Protocol.T_SPEED, (short) clampU16(r.getSpeedCms()));
        d.addUint16(Protocol.T_ASCENT, (short) clampU16(r.getAscentM()));
        d.addUint16(Protocol.T_MAXSPEED, (short) clampU16(r.getMaxSpeedCms()));
        d.addUint32(Protocol.T_MOVING, (int) r.getMovingSeconds());
        d.addUint8(Protocol.T_FIX, (byte) r.getFix());
        d.addUint8(Protocol.T_STATE, (byte) r.getState());
        d.addUint8(Protocol.T_PHONEBATT, (byte) batteryPercent);
        d.addUint32(Protocol.T_RIDE_ID, (int) r.getRideId());
        d.addUint32(Protocol.T_START, (int) (r.getStartTimeMs() / 1000L));

        if (configDirty) {
            d.addUint8(Protocol.T_UNITS, (byte) settings.getUnits());
            d.addUint8(Protocol.T_VERSION, (byte) Protocol.VERSION);
            d.addUint32(Protocol.T_COL_BG, settings.getBackgroundColor() & 0xFFFFFF);
            d.addUint32(Protocol.T_COL_ACCENT, settings.getAccentColor() & 0xFFFFFF);
            configDirty = false;
        }

        send(d);
    }

    /**
     * Tell the watch a ride has ended, without a full telemetry frame.
     *
     * <p>The watch repeats CMD_STOP until it sees STATE_IDLE, so this is the
     * message that ends that loop. It is sent even when the phone had already
     * stopped — a STOP for an already-stopped ride is a no-op that still has to
     * report idle, or the watch would keep asking forever.
     */
    public void sendIdleState() {
        PebbleDictionary d = new PebbleDictionary();
        d.addUint8(Protocol.T_STATE, (byte) Protocol.STATE_IDLE);
        d.addUint32(Protocol.T_RIDE_ID, 0);
        send(d);
    }

    private void send(PebbleDictionary d) {
        try {
            PebbleKit.sendDataToPebble(context, Protocol.WATCHAPP_UUID, d);
        } catch (Exception e) {
            Log.w(TAG, "send failed", e);
        }
    }

    /**
     * Saturate to a value that survives the trip as a 16-bit field.
     *
     * <p>Capped at 32767, not 65535, because {@code addUint16} takes a
     * {@code short}: anything above 32767 casts to a negative number, is stored
     * as negative, and is serialised into the intent's JSON as negative. It
     * probably round-trips through two's complement — but "probably" is not a
     * wire format, and the cap costs nothing real. 32767 cm/s is 1179 km/h, and
     * 32767 m of ascent is four times the height gain of a Tour mountain stage.
     */
    private static int clampU16(int v) {
        if (v < 0) return 0;
        return Math.min(v, Short.MAX_VALUE);
    }
}
