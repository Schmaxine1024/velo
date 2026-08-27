package org.lianas.velo;

import android.view.View;

import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;

/**
 * Keeps content out from under the system bars.
 *
 * <p>Needed because targetSdk 35 makes edge-to-edge mandatory on Android 15:
 * the window is laid out behind the status and navigation bars whether the app
 * asks for it or not. Without this every screen's first line sits under the
 * clock.
 *
 * <p>Applied as padding on the root view rather than a margin, so the
 * background still runs to the edges of the screen while the content does not.
 */
public final class ViewInsets {

    private ViewInsets() {}

    /**
     * Pad {@code view} by the system bar insets, preserving whatever padding
     * it already has from the layout.
     */
    public static void applySystemBars(View view) {
        final int left = view.getPaddingLeft();
        final int top = view.getPaddingTop();
        final int right = view.getPaddingRight();
        final int bottom = view.getPaddingBottom();

        ViewCompat.setOnApplyWindowInsetsListener(view, (v, windowInsets) -> {
            // Display cutout as well as system bars: on a device with a
            // punch-hole in landscape the bars inset is zero on that edge, but
            // the cutout still eats content.
            Insets bars = windowInsets.getInsets(
                    WindowInsetsCompat.Type.systemBars()
                            | WindowInsetsCompat.Type.displayCutout());

            v.setPadding(left + bars.left, top + bars.top,
                         right + bars.right, bottom + bars.bottom);

            // Consumed here rather than passed on: this is the only view in
            // the hierarchy that pads for the bars, and letting the insets
            // propagate would have children pad for them a second time.
            return WindowInsetsCompat.CONSUMED;
        });

        ViewCompat.requestApplyInsets(view);
    }
}
