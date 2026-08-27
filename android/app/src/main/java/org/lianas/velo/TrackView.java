package org.lianas.velo;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.AttributeSet;
import android.view.View;

import java.util.List;

/**
 * Draws a ride's track as a bare polyline.
 *
 * <p>No map tiles, deliberately. Tiles would mean a provider, an API key and a
 * network round trip to look at a ride you already own — and this app is meant
 * to work with no accounts and no connection. The shape of a route is usually
 * enough to recognise it.
 */
public class TrackView extends View {

    private static final float PADDING_DP = 12f;

    private final Paint linePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint startPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint endPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();

    private List<RideStore.Point> points;

    public TrackView(Context c, AttributeSet a) {
        super(c, a);

        linePaint.setStyle(Paint.Style.STROKE);
        linePaint.setStrokeWidth(dp(3f));
        linePaint.setStrokeCap(Paint.Cap.ROUND);
        linePaint.setStrokeJoin(Paint.Join.ROUND);
        linePaint.setColor(getContext().getColor(R.color.velo_orange));

        startPaint.setColor(Color.parseColor("#2E7D32"));
        endPaint.setColor(Color.parseColor("#C62828"));
    }

    public void setPoints(List<RideStore.Point> pts) {
        points = pts;
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (points == null || points.size() < 2) {
            return;
        }

        double minLat = Double.MAX_VALUE, maxLat = -Double.MAX_VALUE;
        double minLon = Double.MAX_VALUE, maxLon = -Double.MAX_VALUE;
        for (RideStore.Point p : points) {
            minLat = Math.min(minLat, p.lat);
            maxLat = Math.max(maxLat, p.lat);
            minLon = Math.min(minLon, p.lon);
            maxLon = Math.max(maxLon, p.lon);
        }

        // Equirectangular projection with a cosine correction at the track's
        // mean latitude. Over a single ride the error against a proper
        // projection is far below a pixel, and it needs no library.
        //
        // Without the cosine term a route at 55N would be stretched almost
        // twice as wide as it is tall, which makes it unrecognisable.
        double meanLat = Math.toRadians((minLat + maxLat) / 2);
        double lonScale = Math.cos(meanLat);

        double spanX = (maxLon - minLon) * lonScale;
        double spanY = maxLat - minLat;

        float pad = dp(PADDING_DP);
        float availW = getWidth() - 2 * pad;
        float availH = getHeight() - 2 * pad;
        if (availW <= 0 || availH <= 0) {
            return;
        }

        // A ride that is a straight line, or a stationary smudge, has zero span
        // on one axis; guard the divide rather than emitting NaN coordinates.
        double scale;
        if (spanX <= 0 && spanY <= 0) {
            return;
        } else if (spanX <= 0) {
            scale = availH / spanY;
        } else if (spanY <= 0) {
            scale = availW / spanX;
        } else {
            scale = Math.min(availW / spanX, availH / spanY);
        }

        // Centre whatever is left over after preserving the aspect ratio.
        float offsetX = (float) (pad + (availW - spanX * scale) / 2);
        float offsetY = (float) (pad + (availH - spanY * scale) / 2);

        path.reset();
        float firstX = 0, firstY = 0, lastX = 0, lastY = 0;

        for (int i = 0; i < points.size(); i++) {
            RideStore.Point p = points.get(i);
            float x = (float) (offsetX + (p.lon - minLon) * lonScale * scale);
            // Screen y grows downward; latitude grows north. Flip it, or every
            // ride is drawn upside down.
            float y = (float) (offsetY + (maxLat - p.lat) * scale);

            if (i == 0) {
                path.moveTo(x, y);
                firstX = x;
                firstY = y;
            } else {
                path.lineTo(x, y);
            }
            lastX = x;
            lastY = y;
        }

        canvas.drawPath(path, linePaint);
        canvas.drawCircle(firstX, firstY, dp(5f), startPaint);
        canvas.drawCircle(lastX, lastY, dp(5f), endPaint);
    }

    private float dp(float v) {
        return v * getResources().getDisplayMetrics().density;
    }
}
