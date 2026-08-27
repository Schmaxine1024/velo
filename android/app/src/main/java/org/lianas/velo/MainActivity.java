package org.lianas.velo;

import android.Manifest;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.IBinder;
import android.view.View;
import android.widget.TextView;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.snackbar.Snackbar;

import java.util.ArrayList;
import java.util.List;

/**
 * The live ride screen on the phone.
 *
 * <p>Binds to {@link RideService} rather than owning any recording state. That
 * binding is doing double duty: it feeds this screen, and it keeps the service
 * process alive while the app is visible so the Pebble receiver is registered
 * and the watch can start a ride.
 */
public class MainActivity extends AppCompatActivity implements RideService.RideListener {

    private RideService service;
    private boolean bound;

    private Settings settings;

    private TextView statusWatch, statusGps;
    private TextView valueDistance, valueTime, valueSpeed, valueAvg, valueAscent;
    private TextView labelDistance, labelSpeed, labelAvg, labelAscent;
    private MaterialButton btnPrimary, btnPause;

    private ActivityResultLauncher<String[]> permissionLauncher;

    private final ServiceConnection connection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            service = ((RideService.LocalBinder) binder).getService();
            service.setListener(MainActivity.this);
            bound = true;
            refresh();
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            // Only fires on an unexpected process death, not on unbind.
            service = null;
            bound = false;
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        ViewInsets.applySystemBars(findViewById(R.id.root));

        settings = new Settings(this);

        statusWatch   = findViewById(R.id.status_watch);
        statusGps     = findViewById(R.id.status_gps);
        valueDistance = findViewById(R.id.value_distance);
        valueTime     = findViewById(R.id.value_time);
        valueSpeed    = findViewById(R.id.value_speed);
        valueAvg      = findViewById(R.id.value_avg);
        valueAscent   = findViewById(R.id.value_ascent);
        labelDistance = findViewById(R.id.label_distance);
        labelSpeed    = findViewById(R.id.label_speed);
        labelAvg      = findViewById(R.id.label_avg);
        labelAscent   = findViewById(R.id.label_ascent);
        btnPrimary    = findViewById(R.id.btn_primary);
        btnPause      = findViewById(R.id.btn_pause);

        btnPrimary.setOnClickListener(v -> onPrimaryClicked());
        btnPause.setOnClickListener(v -> onPauseClicked());

        findViewById(R.id.btn_history).setOnClickListener(v ->
                startActivity(new Intent(this, HistoryActivity.class)));
        findViewById(R.id.btn_settings).setOnClickListener(v ->
                startActivity(new Intent(this, SettingsActivity.class)));

        permissionLauncher = registerForActivityResult(
                new ActivityResultContracts.RequestMultiplePermissions(), result -> {
                    if (hasLocationPermission()) {
                        startRide();
                    } else {
                        Snackbar.make(btnPrimary, R.string.permission_needed,
                                Snackbar.LENGTH_LONG).show();
                    }
                });
    }

    @Override
    protected void onStart() {
        super.onStart();
        // Bind only. Emphatically NOT startForegroundService: while idle the
        // service does not call startForeground, and Android kills a service
        // that fails to within five seconds. The service promotes itself when
        // a ride actually begins, and calls startService on itself then so it
        // outlives this binding.
        Intent intent = new Intent(this, RideService.class);
        bindService(intent, connection, Context.BIND_AUTO_CREATE);
    }

    @Override
    protected void onStop() {
        super.onStop();
        if (bound) {
            service.setListener(null);
            unbindService(connection);
            bound = false;
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
    }

    // ---- Controls --------------------------------------------------------

    private void onPrimaryClicked() {
        if (service != null && service.isRecording()) {
            service.stopRide();
        } else {
            if (!hasLocationPermission()) {
                requestPermissions();
                return;
            }
            startRide();
        }
        refresh();
    }

    private void onPauseClicked() {
        if (service == null) return;
        if (service.getRecorder().getState() == Protocol.STATE_PAUSED) {
            service.resumeRide();
        } else {
            service.pauseRide();
        }
        refresh();
    }

    private void startRide() {
        if (service != null) {
            service.startRide();
            refresh();
        }
    }

    private boolean hasLocationPermission() {
        return ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION)
                == PackageManager.PERMISSION_GRANTED;
    }

    private void requestPermissions() {
        List<String> wanted = new ArrayList<>();
        wanted.add(Manifest.permission.ACCESS_FINE_LOCATION);
        wanted.add(Manifest.permission.ACCESS_COARSE_LOCATION);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            // Not strictly required to record, but the foreground notification
            // is the only way to stop a ride from outside the app.
            wanted.add(Manifest.permission.POST_NOTIFICATIONS);
        }
        permissionLauncher.launch(wanted.toArray(new String[0]));
    }

    // ---- Rendering -------------------------------------------------------

    @Override
    public void onRideUpdated() {
        runOnUiThread(this::refresh);
    }

    private void refresh() {
        boolean imperial = settings.isImperial();

        labelDistance.setText(getString(R.string.label_distance)
                + "  (" + Format.distanceUnit(imperial) + ")");
        labelSpeed.setText(getString(R.string.label_speed)
                + "  (" + Format.speedUnit(imperial) + ")");
        labelAvg.setText(getString(R.string.label_avg)
                + "  (" + Format.speedUnit(imperial) + ")");
        labelAscent.setText(getString(R.string.label_ascent)
                + "  (" + Format.ascentUnit(imperial) + ")");

        if (service == null) {
            return;
        }

        RideRecorder r = service.getRecorder();

        valueDistance.setText(Format.distance(r.getDistanceM(), imperial));
        valueTime.setText(Format.duration(r.getMovingSeconds()));
        valueSpeed.setText(Format.speed(r.getSpeedCms(), imperial));
        valueAscent.setText(Format.ascent(r.getAscentM(), imperial));

        int avgCms = r.getMovingSeconds() > 0
                ? (int) ((r.getDistanceM() * 100L) / r.getMovingSeconds()) : 0;
        valueAvg.setText(Format.speed(avgCms, imperial));

        statusWatch.setText(service.isWatchConnected()
                ? R.string.watch_connected : R.string.watch_disconnected);
        statusGps.setText(gpsLabel(r.getFix()));

        boolean recording = service.isRecording();
        btnPrimary.setText(recording ? R.string.stop_ride : R.string.start_ride);
        btnPause.setVisibility(recording ? View.VISIBLE : View.GONE);
        btnPause.setText(r.getState() == Protocol.STATE_PAUSED
                ? R.string.resume : R.string.pause);
    }

    private int gpsLabel(int fix) {
        switch (fix) {
            case Protocol.FIX_GOOD: return R.string.gps_good;
            case Protocol.FIX_OK:   return R.string.gps_ok;
            case Protocol.FIX_POOR: return R.string.gps_poor;
            default:                return R.string.gps_none;
        }
    }
}
