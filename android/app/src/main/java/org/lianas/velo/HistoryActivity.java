package org.lianas.velo;

import android.content.Intent;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import java.text.DateFormat;
import java.util.Date;
import java.util.List;

/** Every ride ever recorded. The watch keeps only the last fifteen. */
public class HistoryActivity extends AppCompatActivity {

    private RideStore store;
    private Settings settings;
    private final Adapter adapter = new Adapter();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_history);
        ViewInsets.applySystemBars(findViewById(R.id.root));

        store = new RideStore(this);
        settings = new Settings(this);

        RecyclerView list = findViewById(R.id.list);
        list.setLayoutManager(new LinearLayoutManager(this));
        list.setAdapter(adapter);
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Reloaded here rather than in onCreate: coming back from the detail
        // screen after deleting a ride must not leave a stale row on screen.
        adapter.setRides(store.listRides());
        findViewById(R.id.empty).setVisibility(
                adapter.getItemCount() == 0 ? View.VISIBLE : View.GONE);
    }

    @Override
    protected void onDestroy() {
        store.close();
        super.onDestroy();
    }

    private class Adapter extends RecyclerView.Adapter<Holder> {
        private List<RideStore.Ride> rides = java.util.Collections.emptyList();

        void setRides(List<RideStore.Ride> r) {
            rides = r;
            notifyDataSetChanged();
        }

        @NonNull
        @Override
        public Holder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
            View v = LayoutInflater.from(parent.getContext())
                    .inflate(R.layout.item_ride, parent, false);
            return new Holder(v);
        }

        @Override
        public void onBindViewHolder(@NonNull Holder h, int position) {
            RideStore.Ride ride = rides.get(position);
            boolean imperial = settings.isImperial();

            h.date.setText(DateFormat.getDateTimeInstance(
                    DateFormat.MEDIUM, DateFormat.SHORT).format(new Date(ride.startTimeMs)));
            h.summary.setText(Format.distance(ride.distanceM, imperial)
                    + " " + Format.distanceUnit(imperial)
                    + "   " + Format.duration(ride.movingSeconds)
                    + "   " + Format.ascent(ride.ascentM, imperial)
                    + " " + Format.ascentUnit(imperial));

            h.itemView.setOnClickListener(v -> {
                Intent i = new Intent(HistoryActivity.this, RideDetailActivity.class);
                i.putExtra(RideDetailActivity.EXTRA_RIDE_ID, ride.id);
                startActivity(i);
            });
        }

        @Override
        public int getItemCount() {
            return rides.size();
        }
    }

    static class Holder extends RecyclerView.ViewHolder {
        final TextView date, summary;

        Holder(View v) {
            super(v);
            date = v.findViewById(R.id.ride_date);
            summary = v.findViewById(R.id.ride_summary);
        }
    }
}
