package com.firmware.quota;

import android.Manifest;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import java.util.ArrayList;
import java.util.List;

public class MainActivity extends AppCompatActivity {
    
    private QuotaProgressBar progressBar;
    private QuotaPreferences prefs;
    private QuotaApiClient apiClient;
    private Handler refreshHandler;
    private Runnable refreshRunnable;

    private Handler uiTickHandler;
    private Runnable uiTickRunnable;

    private TextView subtitle;
    private TextView statusView;
    private TextView windowUsage;
    private TextView windowDelta;
    private TextView windowResetIn;
    private TextView windowResetsRemaining;
    private TextView weeklyUsage;
    private TextView weeklyResetIn;
    private TextView lastOk;
    private TextView nextRefresh;
    private Button toggleDiagnostics;
    private View diagnosticsContainer;
    private TextView diagnostics;

    private long nextRefreshAtMs = 0;
    private long lastOkAtMs = 0;
    private int consecutiveFailures = 0;
    private String lastError = "";
    private QuotaData lastData = null;
    
    private static final long REFRESH_INTERVAL_MS = 300000; // 5 minutes
    private static final long UI_TICK_MS = 1000;
    private static final int NOTIFICATION_PERMISSION_CODE = 100;

    private static final int MENU_SET_API_KEY = 1;
    private static final int MENU_REFRESH_NOW = 2;
    private static final int MENU_CLEAR_API_KEY = 3;
    private static final int MENU_SHOW_NOTIFICATION = 4;
    private static final int MENU_HIDE_NOTIFICATION = 5;
    private static final int MENU_RESET_WINDOW = 6;

    private boolean resetInFlight = false;
    
    private final List<Double> deltaHistory = new ArrayList<>();
    private double lastPercentage = 0;
    private boolean hasLastPercentage = false;
    
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        
        progressBar = findViewById(R.id.progress_bar);
        subtitle = findViewById(R.id.subtitle);
        statusView = findViewById(R.id.status);
        windowUsage = findViewById(R.id.window_usage);
        windowDelta = findViewById(R.id.window_delta);
        windowResetIn = findViewById(R.id.window_reset_in);
        windowResetsRemaining = findViewById(R.id.window_resets_remaining);
        weeklyUsage = findViewById(R.id.weekly_usage);
        weeklyResetIn = findViewById(R.id.weekly_reset_in);
        lastOk = findViewById(R.id.last_ok);
        nextRefresh = findViewById(R.id.next_refresh);
        toggleDiagnostics = findViewById(R.id.toggle_diagnostics);
        diagnosticsContainer = findViewById(R.id.diagnostics_container);
        diagnostics = findViewById(R.id.diagnostics);

        toggleDiagnostics.setOnClickListener(v -> {
            boolean show = diagnosticsContainer.getVisibility() != View.VISIBLE;
            diagnosticsContainer.setVisibility(show ? View.VISIBLE : View.GONE);
            toggleDiagnostics.setText(show ? "Less details" : "More details");
        });

        prefs = new QuotaPreferences(this);
        apiClient = new QuotaApiClient();
        refreshHandler = new Handler(Looper.getMainLooper());

        uiTickHandler = new Handler(Looper.getMainLooper());
        
        refreshRunnable = new Runnable() {
            @Override
            public void run() {
                refreshQuota();
                refreshHandler.postDelayed(this, REFRESH_INTERVAL_MS);
            }
        };

        uiTickRunnable = new Runnable() {
            @Override
            public void run() {
                updateDerivedUi();
                uiTickHandler.postDelayed(this, UI_TICK_MS);
            }
        };
        
        // Load last known data
        if (prefs.hasApiKey()) {
            double lastPct = prefs.getLastPercentage();
            if (lastPct > 0) {
                progressBar.setPercentage((float) lastPct);
            }
        }

        // Initial UI state
        updateStatus("INIT", false);
        updateDetailPlaceholders();
        
        // Check if API key is set
        if (!prefs.hasApiKey()) {
            showApiKeyDialog();
        } else {
            refreshQuota();
        }
    }
    
    @Override
    protected void onResume() {
        super.onResume();
        refreshHandler.post(refreshRunnable);
        uiTickHandler.post(uiTickRunnable);
    }
    
    @Override
    protected void onPause() {
        super.onPause();
        refreshHandler.removeCallbacks(refreshRunnable);
        uiTickHandler.removeCallbacks(uiTickRunnable);
    }
    
    @Override
    protected void onDestroy() {
        super.onDestroy();
        apiClient.shutdown();
    }
    
    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        menu.add(0, MENU_SET_API_KEY, 0, "Set API Key");
        menu.add(0, MENU_REFRESH_NOW, 0, "Refresh Now");
        menu.add(0, MENU_RESET_WINDOW, 0, "Reset Window...");
        menu.add(0, MENU_CLEAR_API_KEY, 0, "Clear API Key");
        menu.add(0, MENU_SHOW_NOTIFICATION, 0, "Show Notification");
        menu.add(0, MENU_HIDE_NOTIFICATION, 0, "Hide Notification");
        return true;
    }
    
    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        switch (item.getItemId()) {
            case MENU_SET_API_KEY:
                showApiKeyDialog();
                return true;
            case MENU_REFRESH_NOW:
                refreshQuota();
                return true;
            case MENU_RESET_WINDOW:
                confirmAndResetWindow();
                return true;
            case MENU_CLEAR_API_KEY:
                prefs.saveApiKey(null);
                progressBar.setHasData(false);
                Toast.makeText(this, "API key cleared", Toast.LENGTH_SHORT).show();
                return true;
            case MENU_SHOW_NOTIFICATION:
                startNotificationService();
                return true;
            case MENU_HIDE_NOTIFICATION:
                stopNotificationService();
                return true;
        }
        return super.onOptionsItemSelected(item);
    }

    @Override
    public boolean onPrepareOptionsMenu(Menu menu) {
        MenuItem reset = menu.findItem(MENU_RESET_WINDOW);
        if (reset != null) {
            boolean enabled = prefs != null && prefs.hasApiKey() && !resetInFlight;
            reset.setEnabled(enabled);
        }
        return super.onPrepareOptionsMenu(menu);
    }
    
    private void startNotificationService() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) 
                    != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this, 
                        new String[]{Manifest.permission.POST_NOTIFICATIONS}, 
                        NOTIFICATION_PERMISSION_CODE);
                return;
            }
        }
        launchNotificationService();
    }
    
    private void launchNotificationService() {
        Intent serviceIntent = new Intent(this, QuotaNotificationService.class);
        startForegroundService(serviceIntent);
        Toast.makeText(this, "Notification service started", Toast.LENGTH_SHORT).show();
    }
    
    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == NOTIFICATION_PERMISSION_CODE) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                launchNotificationService();
            } else {
                Toast.makeText(this, "Notification permission required for this feature", Toast.LENGTH_LONG).show();
            }
        }
    }
    
    private void stopNotificationService() {
        Intent serviceIntent = new Intent(this, QuotaNotificationService.class);
        stopService(serviceIntent);
        Toast.makeText(this, "Notification service stopped", Toast.LENGTH_SHORT).show();
    }
    
    private void showApiKeyDialog() {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle("Set Firmware API Key");
        
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        int padding = (int) (20 * getResources().getDisplayMetrics().density);
        layout.setPadding(padding, padding, padding, padding);
        
        final EditText input = new EditText(this);
        input.setHint("fw_api_... or token");
        input.setMinLines(1);
        input.setMaxLines(3);
        String currentKey = prefs.getApiKey();
        if (currentKey != null) {
            input.setText(currentKey);
        }
        layout.addView(input);
        
        builder.setView(layout);
        
        builder.setPositiveButton("SAVE", (dialog, which) -> {
            String apiKey = input.getText().toString().trim();
            if (!apiKey.isEmpty()) {
                prefs.saveApiKey(apiKey);
                Toast.makeText(this, "API key saved", Toast.LENGTH_SHORT).show();
                refreshQuota();
            }
        });
        
        builder.setNegativeButton("CANCEL", (dialog, which) -> dialog.cancel());
        
        AlertDialog dialog = builder.create();
        dialog.show();
        
        // Make buttons more visible
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setTextColor(0xFF33C74D);
        dialog.getButton(AlertDialog.BUTTON_NEGATIVE).setTextColor(0xFFE84761);
    }
    
    private void refreshQuota() {
        if (!prefs.hasApiKey()) {
            progressBar.setHasData(false);
            updateStatus("INIT", false);
            return;
        }
        
        String apiKey = prefs.getApiKey();
        nextRefreshAtMs = System.currentTimeMillis() + REFRESH_INTERVAL_MS;
        apiClient.fetchQuota(apiKey, new QuotaApiClient.QuotaCallback() {
            @Override
            public void onSuccess(QuotaData data) {
                lastData = data;
                progressBar.setPercentage((float) data.percentage);
                progressBar.setResetTime(data.resetTime);
                progressBar.setStale(false);

                consecutiveFailures = 0;
                lastError = "";
                lastOkAtMs = System.currentTimeMillis();
                updateStatus("OK", false);
                
                // Calculate and store delta
                if (hasLastPercentage) {
                    double delta = data.percentage - lastPercentage;
                    if (Math.abs(delta) >= 0.05) {
                        progressBar.addDelta(delta);
                        windowDelta.setText(String.format("Delta: %+0.1fpp", delta));
                    }
                }
                
                lastPercentage = data.percentage;
                hasLastPercentage = true;
                
                prefs.saveLastQuotaData(data.percentage, data.resetTime);

                renderDetails(data);

                // Update menu state (e.g. enable reset).
                invalidateOptionsMenu();
            }
            
            @Override
            public void onError(String error) {
                progressBar.setStale(true);
                consecutiveFailures += 1;
                lastError = error != null ? error : "Unknown error";
                updateStatus("STALE", true);
                updateDiagnostics();

                // Keep existing details visible; just tick will update countdowns.
                Toast.makeText(MainActivity.this, "Error: " + lastError, Toast.LENGTH_SHORT).show();

                invalidateOptionsMenu();
            }
        });
    }

    private void confirmAndResetWindow() {
        if (prefs == null || !prefs.hasApiKey()) {
            Toast.makeText(this, "Set API key first", Toast.LENGTH_SHORT).show();
            return;
        }
        if (resetInFlight) {
            return;
        }

        String msg = "Reset your 5-hour spending window now?\n\n" +
                "- Uses 1 of 2 weekly resets\n" +
                "- Only helps if weekly budget remains";

        AlertDialog.Builder b = new AlertDialog.Builder(this);
        b.setTitle("Reset Window");
        b.setMessage(msg);
        b.setNegativeButton("Cancel", (d, w) -> d.dismiss());
        b.setPositiveButton("Reset", (d, w) -> {
            d.dismiss();
            doResetWindow();
        });
        AlertDialog dialog = b.create();
        dialog.show();
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setTextColor(0xFFE84761);
        dialog.getButton(AlertDialog.BUTTON_NEGATIVE).setTextColor(0xFF111111);
    }

    private void doResetWindow() {
        if (resetInFlight) return;
        resetInFlight = true;
        invalidateOptionsMenu();
        updateStatus("RESETTING", false);

        String apiKey = prefs.getApiKey();
        apiClient.resetWindow(apiKey, new QuotaApiClient.ResetWindowCallback() {
            @Override
            public void onSuccess(int windowResetsRemaining) {
                resetInFlight = false;
                invalidateOptionsMenu();

                if (windowResetsRemaining >= 0) {
                    Toast.makeText(MainActivity.this,
                            "Window reset. Resets remaining: " + windowResetsRemaining + "/2",
                            Toast.LENGTH_LONG).show();
                } else {
                    Toast.makeText(MainActivity.this, "Window reset", Toast.LENGTH_LONG).show();
                }

                // Refresh immediately to reflect new window + remaining resets.
                refreshQuota();
            }

            @Override
            public void onError(String error) {
                resetInFlight = false;
                invalidateOptionsMenu();
                String e = (error == null || error.isEmpty()) ? "Reset failed" : error;
                Toast.makeText(MainActivity.this, e, Toast.LENGTH_LONG).show();
                updateStatus("OK", false);
            }
        });
    }

    private void updateStatus(String status, boolean stale) {
        if (statusView == null) return;
        statusView.setText("Status: " + status);
        int color = stale ? 0xFFE84761 : 0xFF111111;
        statusView.setTextColor(color);

        if (subtitle != null) {
            if (!prefs.hasApiKey()) {
                subtitle.setText("Tap menu to set API key");
            } else {
                subtitle.setText(stale ? "Showing last known values" : "Live");
            }
        }
    }

    private void updateDetailPlaceholders() {
        windowUsage.setText("Usage: --");
        windowDelta.setText("Delta: --");
        windowResetIn.setText("Reset: --");
        windowResetsRemaining.setText("Manual resets: --");
        weeklyUsage.setText("Weekly usage: --");
        weeklyResetIn.setText("Weekly reset: --");
        lastOk.setText("Last OK: --");
        nextRefresh.setText("Next refresh: --");
        updateDiagnostics();
    }

    private void renderDetails(QuotaData data) {
        if (data == null) return;

        windowUsage.setText(String.format("Usage: %.1f%%", data.percentage));
        if (!hasLastPercentage) {
            windowDelta.setText("Delta: --");
        }

        int remainingS = computeRemainingSecondsFromResetIso(data.windowReset);
        if (remainingS >= 0) {
            windowResetIn.setText("Reset: " + formatDurationCompact(remainingS));
        } else {
            windowResetIn.setText("Reset: N/A");
        }

        windowResetsRemaining.setText(String.format("Manual resets: %d/2", data.windowResetsRemaining));

        weeklyUsage.setText(String.format("Weekly usage: %.1f%%", data.weeklyUsed * 100.0));
        int weeklyRemainingS = computeRemainingSecondsFromResetIso(data.weeklyReset);
        if (weeklyRemainingS >= 0) {
            long days = weeklyRemainingS / (24L * 60L * 60L);
            weeklyResetIn.setText("Weekly reset: " + formatDurationCompact(weeklyRemainingS) + " (remaining days: " + days + ")");
        } else {
            weeklyResetIn.setText("Weekly reset: N/A");
        }

        updateDerivedUi();
        updateDiagnostics();
    }

    private void updateDerivedUi() {
        // Runs each second while activity is visible.
        if (!prefs.hasApiKey()) {
            return;
        }

        long nowMs = System.currentTimeMillis();

        if (nextRefreshAtMs > 0) {
            long rem = (nextRefreshAtMs - nowMs + 999) / 1000;
            if (rem < 0) rem = 0;
            nextRefresh.setText("Next refresh: " + rem + "s");
        }

        if (lastOkAtMs > 0) {
            long age = (nowMs - lastOkAtMs + 999) / 1000;
            if (age < 0) age = 0;
            lastOk.setText("Last OK: " + formatDurationCompact((int) age) + " ago");
        }

        if (lastData != null) {
            // Keep countdowns fresh.
            int remainingS = computeRemainingSecondsFromResetIso(lastData.windowReset);
            if (remainingS >= 0) {
                windowResetIn.setText("Reset: " + formatDurationCompact(remainingS));
            }

            int weeklyRemainingS = computeRemainingSecondsFromResetIso(lastData.weeklyReset);
            if (weeklyRemainingS >= 0) {
                long days = weeklyRemainingS / (24L * 60L * 60L);
                weeklyResetIn.setText("Weekly reset: " + formatDurationCompact(weeklyRemainingS) + " (remaining days: " + days + ")");
            }
        }
    }

    private void updateDiagnostics() {
        if (diagnostics == null) return;
        if (!prefs.hasApiKey()) {
            diagnostics.setText("Diagnostics: API key not set");
            return;
        }

        StringBuilder sb = new StringBuilder();
        sb.append("Diagnostics");
        sb.append("\nFailures: ").append(consecutiveFailures);
        if (lastError != null && !lastError.isEmpty()) {
            sb.append("\nLast error: ").append(lastError);
        }
        diagnostics.setText(sb.toString());
    }

    private static int computeRemainingSecondsFromResetIso(String iso) {
        if (iso == null || iso.isEmpty() || "N/A".equals(iso)) {
            return -1;
        }
        try {
            java.util.Date d = parseUtcIso8601(iso);
            long until = (d.getTime() - System.currentTimeMillis()) / 1000;
            if (until < 0) until = 0;
            if (until > Integer.MAX_VALUE) until = Integer.MAX_VALUE;
            return (int) until;
        } catch (Exception e) {
            return -1;
        }
    }

    private static java.util.Date parseUtcIso8601(String iso) throws java.text.ParseException {
        java.util.TimeZone utc = java.util.TimeZone.getTimeZone("UTC");

        java.text.SimpleDateFormat sdfMs = new java.text.SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'");
        sdfMs.setTimeZone(utc);
        try {
            return sdfMs.parse(iso);
        } catch (java.text.ParseException ignored) {
            // Fall through.
        }

        java.text.SimpleDateFormat sdf = new java.text.SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'");
        sdf.setTimeZone(utc);
        return sdf.parse(iso);
    }

    private static String formatDurationCompact(int seconds) {
        if (seconds < 0) seconds = 0;
        int h = seconds / 3600;
        int m = (seconds % 3600) / 60;
        int s = seconds % 60;

        if (h > 0) {
            return h + "h " + m + "m";
        }
        if (m > 0) {
            return m + "m " + s + "s";
        }
        return s + "s";
    }
}
