package org.lianas.velo;

import java.io.IOException;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.TimeZone;

/**
 * Writes a ride as GPX 1.1.
 *
 * <p>Export is to a file the rider picks, via the system document picker — not
 * an upload. There is no Strava or Garmin integration by design, but a GPX file
 * imports into any of them, so nothing is locked in here.
 */
public final class GpxWriter {

    private GpxWriter() {}

    public static void write(OutputStream out, RideStore.Ride ride,
                             List<RideStore.Point> points) throws IOException {
        // GPX timestamps are ISO 8601 in UTC. Writing local time with a Z
        // suffix is a common bug that silently shifts every track by the
        // offset, so the timezone is set explicitly rather than assumed.
        SimpleDateFormat iso =
                new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.US);
        iso.setTimeZone(TimeZone.getTimeZone("UTC"));

        Writer w = new OutputStreamWriter(out, StandardCharsets.UTF_8);

        w.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        w.write("<gpx version=\"1.1\" creator=\"Velo\"\n");
        w.write("     xmlns=\"http://www.topografix.com/GPX/1/1\"\n");
        w.write("     xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n");
        w.write("     xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1"
                + " http://www.topografix.com/GPX/1/1/gpx.xsd\">\n");

        w.write("  <metadata>\n");
        w.write("    <name>" + escape(name(ride)) + "</name>\n");
        w.write("    <time>" + iso.format(new Date(ride.startTimeMs)) + "</time>\n");
        w.write("  </metadata>\n");

        w.write("  <trk>\n");
        w.write("    <name>" + escape(name(ride)) + "</name>\n");
        w.write("    <type>cycling</type>\n");
        w.write("    <trkseg>\n");

        for (RideStore.Point p : points) {
            w.write(String.format(Locale.US,
                    "      <trkpt lat=\"%.7f\" lon=\"%.7f\">\n", p.lat, p.lon));
            w.write(String.format(Locale.US,
                    "        <ele>%.1f</ele>\n", p.altitude));
            w.write("        <time>" + iso.format(new Date(p.timeMs)) + "</time>\n");
            w.write("      </trkpt>\n");
        }

        w.write("    </trkseg>\n");
        w.write("  </trk>\n");
        w.write("</gpx>\n");
        w.flush();
    }

    public static String name(RideStore.Ride ride) {
        return "Velo ride " + new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US)
                .format(new Date(ride.startTimeMs));
    }

    public static String filename(RideStore.Ride ride) {
        return "velo-" + new SimpleDateFormat("yyyyMMdd-HHmm", Locale.US)
                .format(new Date(ride.startTimeMs)) + ".gpx";
    }

    /** Minimal XML escaping. Ride names are generated, but not by the parser. */
    private static String escape(String s) {
        return s.replace("&", "&amp;")
                .replace("<", "&lt;")
                .replace(">", "&gt;")
                .replace("\"", "&quot;");
    }
}
