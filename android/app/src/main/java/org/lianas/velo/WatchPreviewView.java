package org.lianas.velo;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.View;

/**
 * A miniature of the watch's ride screen, drawn with the currently chosen
 * colours.
 *
 * <p>Worth the code: colour choices on a phone screen are a poor predictor of a
 * reflective LCD, and more importantly the watch <em>derives</em> three of its
 * five colours. Showing a swatch alone would hide the interesting part — that
 * picking a dark background silently flips the text to white, and that a
 * low-contrast accent is rejected outright.
 *
 * <p>Everything below is laid out in <b>emery pixels</b> (200x228) and scaled
 * to fit, so the constants can be read straight across from ui_ride.c and
 * ui.h. That matters more than it looks: this preview claims to show what the
 * watch will do, so when the watch layout changes and this does not, it starts
 * confidently lying. Keep the two in step.
 */
public class WatchPreviewView extends View {

    // Mirrors of the watchapp's layout constants. See ui.h / ui_ride.c.
    private static final float WATCH_W = 200f;
    private static final float WATCH_H = 228f;
    private static final float STATUS_H = 26f;
    private static final float PANEL_INSET = 4f;
    private static final float PANEL_GAP = 7f;
    private static final float PANEL_RADIUS = 10f;
    private static final float HERO_FRACTION = 0.52f;

    private static final float FONT_HERO = 42f;
    private static final float FONT_VALUE = 30f;
    private static final float FONT_LABEL = 18f;

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

    private int bg = Settings.DEFAULT_BG;
    private int accent = Settings.DEFAULT_ACCENT;
    private boolean imperial;

    /** Watch pixels to view pixels. */
    private float s = 1f;

    public WatchPreviewView(Context c, AttributeSet a) {
        super(c, a);
        paint.setFakeBoldText(true);
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
        // Locked to the watch's aspect so the preview is not a lie about how
        // much room the layout actually has.
        int w = MeasureSpec.getSize(widthSpec);
        setMeasuredDimension(w, Math.round(w * WATCH_H / WATCH_W));
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        s = getWidth() / WATCH_W;

        final int ink = WatchTheme.ink(bg);
        final int muted = WatchTheme.muted(bg);
        final int on = onAccent();

        paint.setStyle(Paint.Style.FILL);
        paint.setColor(WatchTheme.opaque(bg));
        canvas.drawRect(0, 0, getWidth(), getHeight(), paint);

        // ---- Status band: full bleed, square corners --------------------
        paint.setColor(WatchTheme.opaque(WatchTheme.effectiveAccent(bg, accent)));
        canvas.drawRect(0, 0, getWidth(), STATUS_H * s, paint);

        drawFixBars(canvas, PANEL_INSET * s + 1, on);
        text(canvas, "REC", getWidth() / 2f, FONT_LABEL, on, Paint.Align.CENTER,
             STATUS_H * s * 0.72f);
        text(canvas, "76%", getWidth() - PANEL_INSET * s - 1, FONT_LABEL, on,
             Paint.Align.RIGHT, STATUS_H * s * 0.72f);

        // ---- Hero: a rounded card floating below the band ---------------
        float rest = WATCH_H - STATUS_H;
        float heroH = rest * HERO_FRACTION;
        RectF hero = new RectF(
                PANEL_INSET * s,
                (STATUS_H + PANEL_GAP) * s,
                (WATCH_W - PANEL_INSET) * s,
                (STATUS_H + heroH - 2) * s);

        paint.setColor(WatchTheme.opaque(WatchTheme.effectiveAccent(bg, accent)));
        canvas.drawRoundRect(hero, PANEL_RADIUS * s, PANEL_RADIUS * s, paint);

        metric(canvas, hero, "SPEED " + Format.speedUnit(imperial).toUpperCase(),
               imperial ? "18.6" : "29.9", FONT_HERO, on, on);

        // ---- Two cells, separated by whitespace and nothing else --------
        float cellTop = (STATUS_H + heroH) * s;
        float cellH = getHeight() - cellTop;
        float colW = getWidth() / 2f;

        metric(canvas, new RectF(0, cellTop, colW, cellTop + cellH),
               "DIST " + Format.distanceUnit(imperial).toUpperCase(),
               imperial ? "8.42" : "13.55", FONT_VALUE, muted, ink);

        metric(canvas, new RectF(colW, cellTop, getWidth(), cellTop + cellH),
               "TIME", "0:41", FONT_VALUE, muted, ink);
    }

    /**
     * Ink for text sitting on the accent.
     *
     * <p>Mirrors theme.c: when the accent is rejected for poor contrast the
     * panel is drawn in ink instead, so the text on it has to become the
     * background colour rather than the accent's own opposite.
     */
    private int onAccent() {
        if (WatchTheme.accentRejected(bg, accent)) {
            return bg;
        }
        return WatchTheme.luma(accent) < 145 ? 0xFFFFFF : 0x000000;
    }

    /** Label above value, the pair centred as a block. Mirrors ui_draw_metric. */
    private void metric(Canvas canvas, RectF box, String label, String value,
                        float valueSize, int labelColour, int valueColour) {
        paint.setTextAlign(Paint.Align.CENTER);

        paint.setTextSize(FONT_LABEL * s);
        Paint.FontMetrics lm = paint.getFontMetrics();
        float labelH = lm.descent - lm.ascent;

        paint.setTextSize(valueSize * s);
        Paint.FontMetrics vm = paint.getFontMetrics();
        float valueH = vm.descent - vm.ascent;

        float top = box.top + (box.height() - (labelH + valueH)) / 2f;
        float cx = box.centerX();

        paint.setTextSize(FONT_LABEL * s);
        paint.setColor(WatchTheme.opaque(labelColour));
        canvas.drawText(label, cx, top - lm.ascent, paint);

        paint.setTextSize(valueSize * s);
        paint.setColor(WatchTheme.opaque(valueColour));
        canvas.drawText(value, cx, top + labelH - vm.ascent, paint);
    }

    private void drawFixBars(Canvas canvas, float x, int colour) {
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(WatchTheme.opaque(colour));
        float barW = 4 * s, gap = 2 * s, maxH = 12 * s;
        float baseline = STATUS_H * s / 2f + maxH / 2f;
        for (int i = 0; i < 3; i++) {
            float h = (5 + i * 4) * s;
            canvas.drawRect(x + i * (barW + gap), baseline - h,
                            x + i * (barW + gap) + barW, baseline, paint);
        }
    }

    private void text(Canvas canvas, String str, float x, float sizeWatchPx,
                      int colour, Paint.Align align, float baseline) {
        paint.setTextAlign(align);
        paint.setTextSize(sizeWatchPx * s);
        paint.setColor(WatchTheme.opaque(colour));
        canvas.drawText(str, x, baseline, paint);
    }
}
