package com.firmware.quota;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.view.View;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.widget.RemoteViews;

import androidx.core.app.NotificationCompat;

public class QuotaNotificationService extends Service {
    
    private static final String CHANNEL_ID = "firmware_quota_channel";
    private static final int NOTIFICATION_ID = 1;
    private static final long UPDATE_INTERVAL_MS = 300000; // 5 minutes
    
    private Handler handler;
    private Runnable updateRunnable;
    private QuotaPreferences prefs;
    private QuotaApiClient apiClient;
    
    @Override
    public void onCreate() {
        super.onCreate();
        prefs = new QuotaPreferences(this);
        apiClient = new QuotaApiClient();
        handler = new Handler(Looper.getMainLooper());
        
        updateRunnable = new Runnable() {
            @Override
            public void run() {
                updateNotification();
                handler.postDelayed(this, UPDATE_INTERVAL_MS);
            }
        };
    }
    
    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        createNotificationChannel();
        
        // Build initial notification
        Notification notification = buildNotification("--", android.graphics.Color.argb(140, 0, 0, 0), false);
        startForeground(NOTIFICATION_ID, notification);
        
        // Start updates
        updateNotification();
        handler.post(updateRunnable);
        
        return START_STICKY;
    }
    
    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
    
    @Override
    public void onDestroy() {
        super.onDestroy();
        handler.removeCallbacks(updateRunnable);
        apiClient.shutdown();
    }
    
    private void createNotificationChannel() {
        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID,
                "Firmware Quota",
                NotificationManager.IMPORTANCE_LOW
        );
        channel.setDescription("Shows firmware quota usage percentage");
        channel.setShowBadge(false);
        
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.createNotificationChannel(channel);
    }
    
    private void updateNotification() {
        if (!prefs.hasApiKey()) {
            updateNotificationView("--", 0, false);
            return;
        }
        
        String apiKey = prefs.getApiKey();
        apiClient.fetchQuota(apiKey, new QuotaApiClient.QuotaCallback() {
            @Override
            public void onSuccess(QuotaData data) {
                updateNotificationView(Math.round(data.percentage) + "%", (int) Math.round(data.percentage), false);
                prefs.saveLastQuotaData(data.percentage, data.resetTime);
            }
            
            @Override
            public void onError(String error) {
                double lastPct = prefs.getLastPercentage();
                if (lastPct > 0) {
                    updateNotificationView(Math.round(lastPct) + "%*", (int) Math.round(lastPct), true);
                } else {
                    updateNotificationView("ERR", 0, true);
                }
            }
        });
    }
    
    private void updateNotificationView(String text, int percentage, boolean isStale) {
        Notification notification = buildNotification(text, percentage, isStale);
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.notify(NOTIFICATION_ID, notification);
    }
    
    private Notification buildNotification(String text, int percentage, boolean isStale) {
        // Create custom notification layout
        RemoteViews notificationLayout = new RemoteViews(getPackageName(), R.layout.notification_quota);
        notificationLayout.setTextViewText(R.id.notification_text, text);

        int clamped = Math.max(0, Math.min(100, percentage));
        setQuotaProgress(notificationLayout, clamped);
        
        // Intent to open main activity when notification clicked
        Intent intent = new Intent(this, MainActivity.class);
        PendingIntent pendingIntent = PendingIntent.getActivity(
                this, 0, intent, PendingIntent.FLAG_IMMUTABLE
        );
        
        return new NotificationCompat.Builder(this, CHANNEL_ID)
                .setSmallIcon(R.drawable.ic_launcher)
                .setContentTitle("Firmware Quota")
                .setContentText(isStale ? text + " (stale)" : text)
                .setStyle(new NotificationCompat.DecoratedCustomViewStyle())
                .setCustomContentView(notificationLayout)
                .setContentIntent(pendingIntent)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .build();
    }

    private void setQuotaProgress(RemoteViews views, int percentage) {
        int activeId;
        if (percentage < 50) {
            activeId = R.id.notification_progress_green;
        } else if (percentage < 80) {
            activeId = R.id.notification_progress_yellow;
        } else {
            activeId = R.id.notification_progress_red;
        }

        setProgressBarState(views, R.id.notification_progress_green, activeId, percentage);
        setProgressBarState(views, R.id.notification_progress_yellow, activeId, percentage);
        setProgressBarState(views, R.id.notification_progress_red, activeId, percentage);
    }

    private void setProgressBarState(RemoteViews views, int barId, int activeId, int percentage) {
        if (barId == activeId && percentage > 0) {
            views.setViewVisibility(barId, View.VISIBLE);
            views.setProgressBar(barId, 100, percentage, false);
        } else {
            views.setViewVisibility(barId, View.GONE);
            views.setProgressBar(barId, 100, 0, false);
        }
    }
}
