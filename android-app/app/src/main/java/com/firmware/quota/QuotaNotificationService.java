package com.firmware.quota;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
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
            updateNotificationView("--", android.graphics.Color.argb(140, 0, 0, 0), false);
            return;
        }
        
        String apiKey = prefs.getApiKey();
        apiClient.fetchQuota(apiKey, new QuotaApiClient.QuotaCallback() {
            @Override
            public void onSuccess(QuotaData data) {
                int color = getColorForPercentage(data.percentage);
                updateNotificationView(Math.round(data.percentage) + "%", color, false);
                prefs.saveLastQuotaData(data.percentage, data.resetTime);
            }
            
            @Override
            public void onError(String error) {
                double lastPct = prefs.getLastPercentage();
                if (lastPct > 0) {
                    int color = getColorForPercentage(lastPct);
                    updateNotificationView(Math.round(lastPct) + "%*", color, true);
                } else {
                    updateNotificationView("ERR", android.graphics.Color.argb(140, 232, 71, 97), true);
                }
            }
        });
    }
    
    private void updateNotificationView(String text, int color, boolean isStale) {
        Notification notification = buildNotification(text, color, isStale);
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.notify(NOTIFICATION_ID, notification);
    }
    
    private Notification buildNotification(String text, int color, boolean isStale) {
        // Create custom notification layout
        RemoteViews notificationLayout = new RemoteViews(getPackageName(), R.layout.notification_quota);
        notificationLayout.setTextViewText(R.id.notification_text, text);
        notificationLayout.setInt(R.id.notification_container, "setBackgroundColor", color);
        
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
    
    private int getColorForPercentage(double percentage) {
        if (percentage < 50) {
            return android.graphics.Color.argb(242, 51, 199, 77);
        } else if (percentage < 80) {
            return android.graphics.Color.argb(242, 242, 191, 51);
        } else {
            return android.graphics.Color.argb(242, 232, 71, 97);
        }
    }
}
