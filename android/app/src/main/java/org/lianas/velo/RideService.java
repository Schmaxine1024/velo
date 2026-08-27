package org.lianas.velo;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.BatteryManager;
import android.os.Binder;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;

import androidx.core.app.ActivityCompat;
import androidx.core.app.NotificationCompat;

/**
 * Owns the ride: GPS in, numbers out, watch kept informed.
 *
 * <p>Lifecycle is deliberately two-mode.
 *
 * <ul>
 *   <li><b>Bound and idle.</b> While the activity is on screen it binds here,
 *       which keeps the process alive so the {@link PebbleBridge} receiver is
 *       registered and the watch can start a ride. Nothing is polled: no
 *       location updates, no timer, no telemetry. The watch learns the link is
 *       up from its own {@code connection_service}, so silence costs nothing.
 *   <li><b>Foreground and recording.</b> Once a ride starts this promotes
 *       itself to a foreground service so logging survives the activity going
 *       away and the screen turning off, which is the entire reason this is a
 *       native app rather than PebbleKit JS.
 * </ul>
 */
public class RideService extends Service implements PebbleBridge.CommandListener {

    private static final String TAG = "VeloService";
    private static final String CHANNEL_ID = "velo_ride";
    private static final int NOTIFICATION_ID = 1;

    public static final String ACTION_STOP = "org.lianas.velo.STOP";

    /** GPS request interval. 1 Hz is what makes a track worth keeping. */
    private static final long GPS_INTERVAL_MS = 1000;

    /** How often the ride row is rewritten, so process death loses little. */
    private static final int PERSIST_EVERY_S = 10;

    public interface RideListener {
        void onRideUpdated();
    }

    public class LocalBinder extends Binder {
        public RideService getService() {
            return RideService.this;
        }
    }

    private final IBinder binder = new LocalBinder();
    private final Handler handler = new Handler(Looper.getMainLooper());

    /**
     * Database writes run here, not on the main thread.
     *
     * <p>A track point is inserted for every accepted fix and the ride row is
     * rewritten every ten seconds — a disk write once a second, for hours, on
     * the thread that also draws the UI and services the Pebble receiver. It
     * would usually be fast enough and occasionally would not be, which is the
     * worst kind of "usually".
     */
    private HandlerThread ioThread;
    private Handler ioHandler;

    private RideRecorder recorder;
    private RideStore store;
    private PebbleBridge bridge;
    private Settings settings;
    private LocationManager locationManager;
    private PowerManager.WakeLock wakeLock;

    private RideListener listener;
    private long rideId;
    private int secondsSincePersist;
    private boolean recording;

    // ---- Service lifecycle -----------------------------------------------

    @Override
    public void onCreate() {
        super.onCreate();
        recorder = new RideRecorder();
        store = new RideStore(this);
        settings = new Settings(this);
        recorder.setAutoPauseEnabled(settings.isAutoPause());
        locationManager = (LocationManager) getSystemService(Context.LOCATION_SERVICE);

        ioThread = new HandlerThread("velo-io");
        ioThread.start();
        ioHandler = new Handler(ioThread.getLooper());

        bridge = new PebbleBridge(this, this);
        bridge.start();

        // A ride row is created the moment recording starts, so a crash before
        // the first fix would otherwise leave an empty ride in the list.
        ioHandler.post(store::pruneEmptyRides);

        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            stopRide();
        }
        // START_STICKY would have Android restart this with a null intent after
        // a kill, resurrecting a service with no ride and no binding. The ride
        // is already durable in SQLite, so there is nothing to resurrect.
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }

    @Override
    public void onDestroy() {
        stopTicker();
        stopLocationUpdates();
        releaseWakeLock();
        bridge.stop();

        // Close the store from the io thread, behind whatever writes are still
        // queued, then let the looper drain. Closing here on the main thread
        // could pull the database out from under a pending insert.
        ioHandler.post(store::close);
        ioThread.quitSafely();

        super.onDestroy();
    }

    public void setListener(RideListener l) {
        listener = l;
    }

    // ---- Ride control ----------------------------------------------------

    public boolean isRecording() {
        return recording;
    }

    public RideRecorder getRecorder() {
        return recorder;
    }

    public boolean isWatchConnected() {
        return bridge.isWatchConnected();
    }

    public void startRide() {
        if (recording) {
            return;
        }
        if (!hasLocationPermission()) {
            Log.w(TAG, "refusing to start without location permission");
            return;
        }

        long now = System.currentTimeMillis();
        rideId = store.createRide(now);
        recorder.setAutoPauseEnabled(settings.isAutoPause());
        recorder.start(rideId, now);
        recording = true;
        secondsSincePersist = 0;

        // Become a started service, not merely a bound one. Without this the
        // whole thing is destroyed the moment the activity unbinds -- which is
        // exactly what happens when the rider pockets the phone -- and
        // startForeground alone does not prevent that.
        startService(new Intent(this, RideService.class));

        startForegroundNotification();
        acquireWakeLock();
        startLocationUpdates();
        startTicker();

        // The watch may not be showing Velo when a ride starts from the phone.
        bridge.launchWatchapp();
        bridge.markConfigDirty();
        pushTelemetry();
        notifyListener();
    }

    public void stopRide() {
        if (!recording) {
            // Still answer the watch: it repeats CMD_STOP until it sees idle,
            // and a stop for an already-stopped ride has to be a no-op that
            // still reports idle rather than silence.
            bridge.sendIdleState();
            return;
        }

        persistRide();
        recorder.stop();
        recording = false;

        stopTicker();
        stopLocationUpdates();
        releaseWakeLock();

        bridge.sendIdleState();

        // No version guard: STOP_FOREGROUND_REMOVE landed in API 24 and minSdk
        // is 26, so the legacy branch was unreachable.
        stopForeground(Service.STOP_FOREGROUND_REMOVE);

        notifyListener();

        // Undo the startService from startRide. If the activity is still bound
        // the service survives on that binding alone -- which is what keeps the
        // Pebble receiver registered so the watch can start the next ride. If
        // nothing is bound, the phone is pocketed and there is nothing left to
        // serve, so going away is correct.
        stopSelf();
    }

    public void pauseRide() {
        recorder.pause();
        pushTelemetry();
        updateNotification();
        notifyListener();
    }

    public void resumeRide() {
        recorder.resume();
        pushTelemetry();
        updateNotification();
        notifyListener();
    }

    /** Re-send settings to the watch after they change in the phone UI. */
    public void onSettingsChanged() {
        recorder.setAutoPauseEnabled(settings.isAutoPause());
        bridge.markConfigDirty();
        // Idle rides send nothing on a timer, so push one frame now or the
        // watch would not see a colour change until the next ride started.
        pushTelemetry();

        // That frame goes nowhere if the watchapp is not running: an AppMessage
        // needs a live app on the other end, and it is not retried. Bringing it
        // up makes the change land, and does so without a second push -- the
        // watchapp sends CMD_SYNC as it starts, and this service, which is
        // alive precisely because a settings screen is bound to it, answers
        // with the settings attached.
        //
        // Not mid-ride: there the watchapp is already running, and relaunching
        // it would throw away the screen the rider is looking at.
        if (!recording) {
            bridge.launchWatchapp();
        }
    }

    // ---- Commands from the watch -----------------------------------------

    @Override
    public void onStartRequested() {
        handler.post(this::startRide);
    }

    @Override
    public void onPauseRequested(long id) {
        handler.post(() -> {
            if (matchesCurrentRide(id)) pauseRide();
        });
    }

    @Override
    public void onResumeRequested(long id) {
        handler.post(() -> {
            if (matchesCurrentRide(id)) resumeRide();
        });
    }

    @Override
    public void onStopRequested(long id, long watchMovingSeconds) {
        handler.post(() -> {
            // A stop for a ride we are no longer running is answered rather
            // than ignored -- that is how the watch's repeat loop terminates.
            if (!recording || matchesCurrentRide(id)) {
                stopRide();
            } else {
                Log.w(TAG, "stop for ride " + id + " while running " + rideId);
                bridge.sendIdleState();
            }
        });
    }

    @Override
    public void onSyncRequested() {
        handler.post(this::pushTelemetry);
    }

    /**
     * Commands quote the ride they refer to. A mismatch means the watch is
     * talking about a ride this process no longer has -- typically because the
     * service was killed and restarted mid-ride -- and acting on it would
     * apply the command to the wrong ride.
     */
    private boolean matchesCurrentRide(long id) {
        return id == 0 || id == rideId;
    }

    // ---- Location --------------------------------------------------------

    // onStatusChanged and friends are deprecated, and got default implementations
    // in API 30 -- but this app supports API 26, where they are still abstract.
    // Dropping them would compile against SDK 35 and then throw AbstractMethodError
    // on an Android 8 phone, so they are kept and the warning is suppressed.
    @SuppressWarnings("deprecation")
    private final LocationListener locationListener = new LocationListener() {
        @Override
        public void onLocationChanged(Location location) {
            boolean keep = recorder.onLocation(location);
            if (keep && rideId != 0) {
                // Snapshot the values before handing them to another thread:
                // the framework is free to recycle the Location object.
                final long id = rideId;
                final double lat = location.getLatitude();
                final double lon = location.getLongitude();
                final double alt = location.hasAltitude() ? location.getAltitude() : 0;
                final long t = location.getTime();
                final float sp = location.hasSpeed() ? location.getSpeed() : 0f;
                ioHandler.post(() -> store.addPoint(id, lat, lon, alt, t, sp));
            }
            notifyListener();
        }

        // Required on older API levels; deliberately empty.
        @Override public void onStatusChanged(String p, int s, Bundle e) {}
        @Override public void onProviderEnabled(String provider) {}
        @Override public void onProviderDisabled(String provider) {}
    };

    private void startLocationUpdates() {
        if (!hasLocationPermission()) {
            return;
        }
        try {
            // GPS_PROVIDER directly rather than the fused provider: fused means
            // Play Services, and this app deliberately has no dependency on it.
            // For a cycling track the raw GNSS stream is what you want anyway.
            locationManager.requestLocationUpdates(
                    LocationManager.GPS_PROVIDER, GPS_INTERVAL_MS, 0f, locationListener);
        } catch (SecurityException | IllegalArgumentException e) {
            Log.e(TAG, "cannot start location updates", e);
        }
    }

    private void stopLocationUpdates() {
        try {
            locationManager.removeUpdates(locationListener);
        } catch (SecurityException ignored) {
        }
    }

    private boolean hasLocationPermission() {
        return ActivityCompat.checkSelfPermission(this,
                Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
    }

    // ---- The one-second loop ---------------------------------------------

    private final Runnable ticker = new Runnable() {
        @Override
        public void run() {
            recorder.tick();
            pushTelemetry();
            updateNotification();
            notifyListener();

            if (++secondsSincePersist >= PERSIST_EVERY_S) {
                secondsSincePersist = 0;
                persistRide();
            }

            handler.postDelayed(this, 1000);
        }
    };

    private void startTicker() {
        handler.removeCallbacks(ticker);
        handler.postDelayed(ticker, 1000);
    }

    private void stopTicker() {
        handler.removeCallbacks(ticker);
    }

    private void pushTelemetry() {
        bridge.sendTelemetry(recorder, batteryPercent(), settings);
    }

    private void persistRide() {
        if (rideId == 0) {
            return;
        }
        // Read the recorder on this thread, write on the io thread: the
        // recorder is not synchronised and belongs to the main thread.
        final long id = rideId;
        final long moving = recorder.getMovingSeconds();
        final int dist = recorder.getDistanceM();
        final int ascent = recorder.getAscentM();
        final int maxSpeed = recorder.getMaxSpeedCms();
        ioHandler.post(() -> store.updateRide(id, moving, dist, ascent, maxSpeed));
    }

    private void notifyListener() {
        RideListener l = listener;
        if (l != null) {
            l.onRideUpdated();
        }
    }

    private int batteryPercent() {
        BatteryManager bm = (BatteryManager) getSystemService(Context.BATTERY_SERVICE);
        if (bm == null) return 255;
        int pct = bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY);
        return (pct < 0 || pct > 100) ? 255 : pct;
    }

    // ---- Wake lock -------------------------------------------------------

    private void acquireWakeLock() {
        if (wakeLock != null) return;
        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        if (pm == null) return;
        // Partial: the CPU stays up so fixes keep arriving, the screen does not.
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "velo:ride");
        wakeLock.acquire();
    }

    private void releaseWakeLock() {
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
        wakeLock = null;
    }

    // ---- Notification ----------------------------------------------------

    private void createNotificationChannel() {
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm == null) return;
        NotificationChannel ch = new NotificationChannel(CHANNEL_ID, "Ride recording",
                NotificationManager.IMPORTANCE_LOW);
        ch.setDescription("Shown while a ride is being recorded");
        ch.setShowBadge(false);
        nm.createNotificationChannel(ch);
    }

    private Notification buildNotification() {
        Intent open = new Intent(this, MainActivity.class)
                .setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent openPi = PendingIntent.getActivity(this, 0, open,
                PendingIntent.FLAG_IMMUTABLE);

        Intent stop = new Intent(this, RideService.class).setAction(ACTION_STOP);
        PendingIntent stopPi = PendingIntent.getService(this, 1, stop,
                PendingIntent.FLAG_IMMUTABLE);

        boolean imperial = settings.isImperial();
        String text = Format.distance(recorder.getDistanceM(), imperial)
                + " " + Format.distanceUnit(imperial)
                + "   " + Format.duration(recorder.getMovingSeconds());

        return new NotificationCompat.Builder(this, CHANNEL_ID)
                .setContentTitle(recorder.getState() == Protocol.STATE_PAUSED
                        ? "Ride paused" : "Recording ride")
                .setContentText(text)
                .setSmallIcon(android.R.drawable.ic_menu_compass)
                .setContentIntent(openPi)
                .addAction(0, "Stop", stopPi)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .build();
    }

    private void startForegroundNotification() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, buildNotification(),
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION);
        } else {
            startForeground(NOTIFICATION_ID, buildNotification());
        }
    }

    private void updateNotification() {
        if (!recording) return;
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm != null) {
            nm.notify(NOTIFICATION_ID, buildNotification());
        }
    }
}
