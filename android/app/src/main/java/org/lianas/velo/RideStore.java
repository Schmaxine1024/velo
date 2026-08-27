package org.lianas.velo;

import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;

import java.util.ArrayList;
import java.util.List;

/**
 * Ride and track-point storage.
 *
 * <p>Raw {@link SQLiteOpenHelper} rather than Room: the schema is two tables
 * that will not change shape, and the whole point of this app is to have no
 * dependencies it does not need. Room would add an annotation processor and a
 * runtime to save perhaps forty lines here.
 *
 * <p>The phone holds the complete history and every track point. The watch
 * keeps only the last fifteen summaries, so that it stays useful with the phone
 * in a jersey pocket — this is the authoritative copy.
 */
public class RideStore extends SQLiteOpenHelper {

    private static final String DB = "velo.db";
    private static final int VERSION = 1;

    public static final class Ride {
        public long id;
        public long startTimeMs;
        public long movingSeconds;
        public int distanceM;
        public int ascentM;
        public int maxSpeedCms;

        public int avgSpeedCms() {
            if (movingSeconds <= 0) return 0;
            return (int) ((distanceM * 100L) / movingSeconds);
        }
    }

    public static final class Point {
        public double lat;
        public double lon;
        public double altitude;
        public long timeMs;
        public float speedMps;
    }

    public RideStore(Context context) {
        super(context.getApplicationContext(), DB, null, VERSION);
    }

    @Override
    public void onCreate(SQLiteDatabase db) {
        db.execSQL("CREATE TABLE rides ("
                + "id INTEGER PRIMARY KEY,"
                + "start_time INTEGER NOT NULL,"
                + "moving_s INTEGER NOT NULL DEFAULT 0,"
                + "distance_m INTEGER NOT NULL DEFAULT 0,"
                + "ascent_m INTEGER NOT NULL DEFAULT 0,"
                + "max_speed_cms INTEGER NOT NULL DEFAULT 0)");

        db.execSQL("CREATE TABLE points ("
                + "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                + "ride_id INTEGER NOT NULL,"
                + "lat REAL NOT NULL,"
                + "lon REAL NOT NULL,"
                + "alt REAL,"
                + "time INTEGER NOT NULL,"
                + "speed REAL)");

        // Every read of points is "all points for one ride, in time order".
        // Without this index that is a full scan of what becomes the largest
        // table in the app -- a few thousand rows per ride.
        db.execSQL("CREATE INDEX idx_points_ride ON points(ride_id, time)");
    }

    @Override
    public void onUpgrade(SQLiteDatabase db, int oldVersion, int newVersion) {
        // Version 1 is the first release; there is nothing to migrate from yet.
        // When that changes, migrate — do not drop. This table is the only copy
        // of the rider's history that exists.
    }

    // ---- Rides -----------------------------------------------------------

    /**
     * Create a ride row and return its id.
     *
     * <p>The id doubles as the protocol's ride id, which is why it is minted
     * here at the moment recording starts rather than assigned on completion.
     */
    public long createRide(long startTimeMs) {
        ContentValues v = new ContentValues();
        v.put("start_time", startTimeMs);
        return getWritableDatabase().insert("rides", null, v);
    }

    public void updateRide(long id, long movingSeconds, int distanceM,
                           int ascentM, int maxSpeedCms) {
        ContentValues v = new ContentValues();
        v.put("moving_s", movingSeconds);
        v.put("distance_m", distanceM);
        v.put("ascent_m", ascentM);
        v.put("max_speed_cms", maxSpeedCms);
        getWritableDatabase().update("rides", v, "id=?",
                new String[]{ String.valueOf(id) });
    }

    public void deleteRide(long id) {
        SQLiteDatabase db = getWritableDatabase();
        db.beginTransaction();
        try {
            db.delete("points", "ride_id=?", new String[]{ String.valueOf(id) });
            db.delete("rides", "id=?", new String[]{ String.valueOf(id) });
            db.setTransactionSuccessful();
        } finally {
            db.endTransaction();
        }
    }

    public List<Ride> listRides() {
        List<Ride> out = new ArrayList<>();
        try (Cursor c = getReadableDatabase().rawQuery(
                "SELECT id,start_time,moving_s,distance_m,ascent_m,max_speed_cms"
                        + " FROM rides ORDER BY start_time DESC", null)) {
            while (c.moveToNext()) {
                out.add(readRide(c));
            }
        }
        return out;
    }

    public Ride getRide(long id) {
        try (Cursor c = getReadableDatabase().rawQuery(
                "SELECT id,start_time,moving_s,distance_m,ascent_m,max_speed_cms"
                        + " FROM rides WHERE id=?",
                new String[]{ String.valueOf(id) })) {
            return c.moveToFirst() ? readRide(c) : null;
        }
    }

    private static Ride readRide(Cursor c) {
        Ride r = new Ride();
        r.id            = c.getLong(0);
        r.startTimeMs   = c.getLong(1);
        r.movingSeconds = c.getLong(2);
        r.distanceM     = c.getInt(3);
        r.ascentM       = c.getInt(4);
        r.maxSpeedCms   = c.getInt(5);
        return r;
    }

    /**
     * Rides with no distance and under ten seconds, which are misclicks.
     *
     * <p>Called on service start: a ride row is created the instant recording
     * begins, so a process death between "start" and the first fix would
     * otherwise leave an empty ride in the list forever.
     */
    public void pruneEmptyRides() {
        SQLiteDatabase db = getWritableDatabase();
        db.beginTransaction();
        try {
            db.delete("rides", "distance_m=0 AND moving_s<10", null);
            // Points outlive their ride otherwise. Each pruned ride can carry a
            // few seconds of fixes, and points is by far the largest table --
            // rows nothing will ever read again, accumulating for the life of
            // the install.
            db.delete("points",
                    "ride_id NOT IN (SELECT id FROM rides)", null);
            db.setTransactionSuccessful();
        } finally {
            db.endTransaction();
        }
    }

    // ---- Points ----------------------------------------------------------

    public void addPoint(long rideId, double lat, double lon, double alt,
                         long timeMs, float speedMps) {
        ContentValues v = new ContentValues();
        v.put("ride_id", rideId);
        v.put("lat", lat);
        v.put("lon", lon);
        v.put("alt", alt);
        v.put("time", timeMs);
        v.put("speed", speedMps);
        getWritableDatabase().insert("points", null, v);
    }

    public List<Point> listPoints(long rideId) {
        List<Point> out = new ArrayList<>();
        try (Cursor c = getReadableDatabase().rawQuery(
                "SELECT lat,lon,alt,time,speed FROM points WHERE ride_id=?"
                        + " ORDER BY time ASC",
                new String[]{ String.valueOf(rideId) })) {
            while (c.moveToNext()) {
                Point p = new Point();
                p.lat      = c.getDouble(0);
                p.lon      = c.getDouble(1);
                p.altitude = c.isNull(2) ? 0 : c.getDouble(2);
                p.timeMs   = c.getLong(3);
                p.speedMps = c.isNull(4) ? 0 : c.getFloat(4);
                out.add(p);
            }
        }
        return out;
    }
}
