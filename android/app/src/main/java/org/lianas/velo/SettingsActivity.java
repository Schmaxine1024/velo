package org.lianas.velo;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.IBinder;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.RadioGroup;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.materialswitch.MaterialSwitch;

/**
 * Units, auto-pause, and the watch's two customizable colours.
 *
 * <p>Binds to {@link RideService} so a change reaches the watch immediately.
 * Without that the watch would keep its old look until the next ride started,
 * since an idle ride sends nothing on a timer.
 */
public class SettingsActivity extends AppCompatActivity {

    private static final int SWATCH_DP = 34;
    private static final int SWATCH_GAP_DP = 8;

    private Settings settings;
    private WatchPreviewView preview;
    private TextView accentWarning;
    private LinearLayout bgRow, accentRow;

    private RideService service;
    private boolean bound;

    private final ServiceConnection connection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName n, IBinder b) {
            service = ((RideService.LocalBinder) b).getService();
            bound = true;
            // Flush anything changed before the binding landed. Taps on the
            // swatches are quite capable of beating the service connection,
            // and those changes would otherwise sit on the phone until the
            // watchapp happened to send a SYNC.
            service.onSettingsChanged();
        }

        @Override
        public void onServiceDisconnected(ComponentName n) {
            service = null;
            bound = false;
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_settings);
        ViewInsets.applySystemBars(findViewById(R.id.root));

        settings = new Settings(this);
        preview = findViewById(R.id.preview);
        accentWarning = findViewById(R.id.accent_warning);
        bgRow = findViewById(R.id.bg_swatches);
        accentRow = findViewById(R.id.accent_swatches);

        // One listener on the group, not one per button. With per-button
        // listeners every tap fires twice -- once with checked=false for the
        // button being cleared, once with true for the new one -- and the
        // whole thing only works because both handlers guard on `checked`.
        // The group tells you the winner directly.
        RadioGroup units = findViewById(R.id.units_group);
        units.check(settings.isImperial() ? R.id.units_imperial : R.id.units_metric);
        units.setOnCheckedChangeListener((group, checkedId) -> {
            settings.setUnits(checkedId == R.id.units_imperial
                    ? Protocol.UNITS_IMPERIAL : Protocol.UNITS_METRIC);
            onSettingsChanged();
        });

        MaterialSwitch autoPause = findViewById(R.id.switch_autopause);
        autoPause.setChecked(settings.isAutoPause());
        autoPause.setOnCheckedChangeListener((v, checked) -> {
            settings.setAutoPause(checked);
            onSettingsChanged();
        });

        buildSwatches();
        refreshPreview();
    }

    @Override
    protected void onStart() {
        super.onStart();
        bindService(new Intent(this, RideService.class), connection, Context.BIND_AUTO_CREATE);
    }

    @Override
    protected void onStop() {
        super.onStop();
        if (bound) {
            unbindService(connection);
            bound = false;
        }
    }

    // ---- Colour swatches -------------------------------------------------

    private void buildSwatches() {
        bgRow.removeAllViews();
        for (int rgb : WatchTheme.BACKGROUNDS) {
            bgRow.addView(makeSwatch(rgb, settings.getBackgroundColor() == rgb, () -> {
                settings.setBackgroundColor(rgb);
                buildSwatches();
                refreshPreview();
                onSettingsChanged();
            }));
        }

        accentRow.removeAllViews();
        for (int rgb : WatchTheme.ACCENTS) {
            accentRow.addView(makeSwatch(rgb, settings.getAccentColor() == rgb, () -> {
                settings.setAccentColor(rgb);
                buildSwatches();
                refreshPreview();
                onSettingsChanged();
            }));
        }
    }

    private View makeSwatch(int rgb, boolean selected, Runnable onClick) {
        View v = new View(this);

        int size = dp(SWATCH_DP);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(size, size);
        lp.setMarginEnd(dp(SWATCH_GAP_DP));
        v.setLayoutParams(lp);

        GradientDrawable d = new GradientDrawable();
        d.setShape(GradientDrawable.OVAL);
        d.setColor(WatchTheme.opaque(rgb));
        // Always outlined, because white and black swatches would otherwise
        // vanish into the light and dark themes respectively.
        d.setStroke(dp(selected ? 3 : 1),
                selected ? getColor(R.color.velo_orange) : 0x66888888);
        v.setBackground(d);

        v.setOnClickListener(x -> onClick.run());
        return v;
    }

    private void refreshPreview() {
        int bg = settings.getBackgroundColor();
        int accent = settings.getAccentColor();

        preview.setColors(bg, accent);
        preview.setImperial(settings.isImperial());

        // Say it plainly when a choice will not survive, and say what to do
        // about it. The old wording explained the rejection but left the rider
        // with no move -- which reads as "white is not available" when in fact
        // white is a fine accent, just not on a pale background.
        if (WatchTheme.accentRejected(bg, accent)) {
            accentWarning.setVisibility(View.VISIBLE);
            accentWarning.setText(WatchTheme.isDark(bg)
                    ? "Too close to the background to read. Pick a lighter "
                      + "accent, or a lighter background."
                    : "Too close to the background to read. Pick a darker "
                      + "accent, or a darker background.");
        } else {
            accentWarning.setVisibility(View.GONE);
        }
    }

    private void onSettingsChanged() {
        refreshPreview();
        if (service != null) {
            service.onSettingsChanged();
        }
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }
}
