package org.lianas.velo;

import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;

import java.io.OutputStream;
import java.text.DateFormat;
import java.util.Collections;
import java.util.Date;
import java.util.List;

/** One finished ride: its shape, its numbers, and the two things you can do to it. */
public class RideDetailActivity extends AppCompatActivity {

    public static final String EXTRA_RIDE_ID = "ride_id";

    private RideStore store;
    private Settings settings;
    private RideStore.Ride ride;

    /** Empty until the loader finishes, never null — the export path reads it. */
    private List<RideStore.Point> points = Collections.emptyList();

    private ActivityResultLauncher<String> createDocument;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_ride_detail);

        store = new RideStore(this);
        settings = new Settings(this);

        long id = getIntent().getLongExtra(EXTRA_RIDE_ID, 0);
        ride = store.getRide(id);
        if (ride == null) {
            finish();
            return;
        }

        setTitle(DateFormat.getDateInstance(DateFormat.MEDIUM)
                .format(new Date(ride.startTimeMs)));

        // The system picker: the file lands wherever the rider chooses, and the
        // app needs no storage permission to put it there.
        createDocument = registerForActivityResult(
                new ActivityResultContracts.CreateDocument("application/gpx+xml"),
                this::writeGpxTo);

        View export = findViewById(R.id.btn_export);
        export.setOnClickListener(v -> createDocument.launch(GpxWriter.filename(ride)));
        // Disabled until the track is in memory, or a quick tap would write a
        // GPX file containing no track at all.
        export.setEnabled(false);

        findViewById(R.id.btn_delete).setOnClickListener(v -> confirmDelete());

        loadPoints(id);
    }

    /**
     * Track points are read off the main thread.
     *
     * <p>A three-hour ride at 1 Hz is on the order of ten thousand rows. Reading
     * those synchronously in onCreate is a visible stall opening the screen, and
     * on a long tour it is an ANR.
     */
    private void loadPoints(long id) {
        new Thread(() -> {
            // A store of its own, opened and closed on this thread. Sharing the
            // activity's would mean onDestroy could close the database out from
            // under this read if the rider backs out while it is still running.
            RideStore local = new RideStore(getApplicationContext());
            final List<RideStore.Point> loaded;
            try {
                loaded = local.listPoints(id);
            } finally {
                local.close();
            }
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) {
                    return;
                }
                points = loaded;
                onPointsLoaded();
            });
        }, "velo-detail-load").start();
    }

    private void onPointsLoaded() {
        TrackView track = findViewById(R.id.track);
        if (points.size() >= 2) {
            track.setPoints(points);
        } else {
            // A ride recorded indoors, or one where every fix was rejected as
            // noise, has nothing to draw. Say so rather than showing a blank
            // rectangle that looks like a rendering failure.
            track.setVisibility(View.GONE);
            findViewById(R.id.no_track).setVisibility(View.VISIBLE);
        }

        ((TextView) findViewById(R.id.stats)).setText(buildStats());
        findViewById(R.id.btn_export).setEnabled(true);
    }

    @Override
    protected void onDestroy() {
        store.close();
        super.onDestroy();
    }

    private String buildStats() {
        boolean imp = settings.isImperial();
        return "Distance    " + Format.distance(ride.distanceM, imp)
                + " " + Format.distanceUnit(imp)
                + "\nMoving time  " + Format.duration(ride.movingSeconds)
                + "\nAverage      " + Format.speed(ride.avgSpeedCms(), imp)
                + " " + Format.speedUnit(imp)
                + "\nMax speed    " + Format.speed(ride.maxSpeedCms, imp)
                + " " + Format.speedUnit(imp)
                + "\nAscent       " + Format.ascent(ride.ascentM, imp)
                + " " + Format.ascentUnit(imp)
                + "\nTrack points " + points.size();
    }

    private void writeGpxTo(Uri uri) {
        if (uri == null) {
            return;   // rider backed out of the picker
        }
        try (OutputStream out = getContentResolver().openOutputStream(uri)) {
            if (out == null) {
                throw new IllegalStateException("no output stream");
            }
            GpxWriter.write(out, ride, points);
            Toast.makeText(this, "Exported", Toast.LENGTH_SHORT).show();
        } catch (Exception e) {
            Toast.makeText(this, "Export failed: " + e.getMessage(),
                    Toast.LENGTH_LONG).show();
        }
    }

    private void confirmDelete() {
        // Unlike the watch's history, this is the only copy of the track, so it
        // gets a real confirmation rather than a long press.
        new AlertDialog.Builder(this)
                .setTitle(R.string.delete_ride)
                .setMessage("This deletes the ride and its track. It cannot be undone.")
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(R.string.delete_ride, (d, w) -> {
                    store.deleteRide(ride.id);
                    finish();
                })
                .show();
    }
}
