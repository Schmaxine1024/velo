package org.lianas.velo;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.View;

/**
 * A miniature of the watch's ride screen, drawn with the currently chosen
 * colours.
 *
 * <p>Worth the code: colour choices on a phone screen are a poor predictor of a
 * reflective LCD, and more importantly the watch *derives* three of its five
 * colours. Showing a swatch alone would hide the interesting part — that
 * picking a dark background silently flips the text to white, and that a
 * low-contrast accent is rejected outright.
 *
 * <p>Proportions mirror emery (200x228) since that is the reference platform.
 */
public class WatchPreviewView extends View {

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

    private int bg = Settings.DEFAULT_BG;
    private int accent = Settings.DEFAULT_ACCENT;
    private boolean imperial;

    public WatchPreviewView(Context c, AttributeSet a) {
        super(c, a);
    }

    public void setColors(int bg, int accent) {
        this.bg = bg;
        this.accent = accent;
        invalidate();
    }

    public void setImperial(boolean imperial) {
        this.imperial = imperial;
        invalidate();
    }

    @Override
    protected void onMeasure(int widthSpec, int heightSpec) {
        // Lock to the watch's 200:228 aspect so the preview is not a lie about
        // how much room the layout has.
        int w = MeasureSpec.getSize(widthSpec);
        int h = Math.round(w * 228f / 200f);
        setMeasuredDimension(w, h);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        int ink = WatchTheme.ink(bg);
        int muted = WatchTheme.muted(bg);
        int rule = WatchTheme.rule(bg);
        int hero = WatchTheme.effectiveAccent(bg, accent);

        float w = getWidth();
        float h = getHeight();

        paint.setStyle(Paint.Style.FILL);
        paint.setColor(WatchTheme.opaque(bg));
        canvas.drawRect(0, 0, w, h, paint);

        float statusH = h * 20f / 228f;
        float heroH = (h - statusH) * 0.38f;
        float gridTop = statusH + heroH;
        float rowH = (h - gridTop) / 2f;

        // Status bar: fix bars, REC, battery.
        paint.setColor(WatchTheme.opaque(ink));
        float barW = w * 0.015f;
        for (int i = 0; i < 3; i++) {
            float bh = statusH * (0.25f + i * 0.18f);
            float bx = w * 0.04f + i * (barW * 1.8f);
            canvas.drawRect(bx, statusH * 0.75f - bh, bx + barW, statusH * 0.75f, paint);
        }
        drawText(canvas, "REC", w / 2, statusH * 0.72f, statusH * 0.6f, ink, Paint.Align.CENTER);
        drawText(canvas, "76%", w * 0.96f, statusH * 0.72f, statusH * 0.6f, muted, Paint.Align.RIGHT);

        paint.setColor(WatchTheme.opaque(rule));
        paint.setStrokeWidth(Math.max(1f, w / 200f));
        canvas.drawLine(0, statusH, w, statusH, paint);

        // Hero.
        drawText(canvas, "SPEED " + Format.speedUnit(imperial).toUpperCase(),
                w / 2, statusH + heroH * 0.34f, h * 0.045f, muted, Paint.Align.CENTER);
        drawText(canvas, imperial ? "18.6" : "29.9",
                w / 2, statusH + heroH * 0.86f, h * 0.19f, hero, Paint.Align.CENTER);

        paint.setColor(WatchTheme.opaque(rule));
        canvas.drawLine(0, gridTop, w, gridTop, paint);
        canvas.drawLine(w / 2, gridTop, w / 2, h, paint);
        canvas.drawLine(0, gridTop + rowH, w, gridTop + rowH, paint);

        // Grid cells.
        drawCell(canvas, "DIST " + Format.distanceUnit(imperial).toUpperCase(),
                imperial ? "8.42" : "13.55",
                w * 0.25f, gridTop, rowH, muted, ink, h);
        drawCell(canvas, "TIME", "0:41:12", w * 0.75f, gridTop, rowH, muted, ink, h);
        drawCell(canvas, "AVG " + Format.speedUnit(imperial).toUpperCase(),
                imperial ? "16.1" : "25.9", w * 0.25f, gridTop + rowH, rowH, muted, ink, h);
        drawCell(canvas, "ASCENT " + Format.ascentUnit(imperial).toUpperCase(),
                imperial ? "1049" : "320", w * 0.75f, gridTop + rowH, rowH, muted, ink, h);
    }

    private void drawCell(Canvas canvas, String label, String value, float cx,
                          float top, float rowH, int muted, int ink, float h) {
        drawText(canvas, label, cx, top + rowH * 0.36f, h * 0.04f, muted, Paint.Align.CENTER);
        drawText(canvas, value, cx, top + rowH * 0.78f, h * 0.085f, ink, Paint.Align.CENTER);
    }

    private void drawText(Canvas canvas, String text, float x, float y, float size,
                          int color, Paint.Align align) {
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(WatchTheme.opaque(color));
        paint.setTextSize(size);
        paint.setTextAlign(align);
        paint.setFakeBoldText(true);
        canvas.drawText(text, x, y, paint);
    }
}
