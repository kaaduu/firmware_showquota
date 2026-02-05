package com.firmware.quota;

import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.Handler;
import android.os.Looper;
import android.widget.RemoteViews;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class QuotaWidgetProvider extends AppWidgetProvider {
    
    private static final String ACTION_REFRESH = "com.firmware.quota.REFRESH";
    private static final long UPDATE_INTERVAL_MS = 300000; // 5 minutes
    
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    
    @Override
    public void onUpdate(Context context, AppWidgetManager appWidgetManager, int[] appWidgetIds) {
        for (int appWidgetId : appWidgetIds) {
            updateWidget(context, appWidgetManager, appWidgetId);
        }
    }
    
    @Override
    public void onReceive(Context context, Intent intent) {
        super.onReceive(context, intent);
        
        if (ACTION_REFRESH.equals(intent.getAction())) {
            AppWidgetManager appWidgetManager = AppWidgetManager.getInstance(context);
            ComponentName componentName = new ComponentName(context, QuotaWidgetProvider.class);
            int[] appWidgetIds = appWidgetManager.getAppWidgetIds(componentName);
            onUpdate(context, appWidgetManager, appWidgetIds);
        }
    }
    
    private void updateWidget(Context context, AppWidgetManager appWidgetManager, int appWidgetId) {
        RemoteViews views = new RemoteViews(context.getPackageName(), R.layout.widget_layout);
        
        QuotaPreferences prefs = new QuotaPreferences(context);
        
        if (!prefs.hasApiKey()) {
            views.setTextViewText(R.id.widget_text, "--");
            views.setInt(R.id.widget_container, "setBackgroundColor", 
                    android.graphics.Color.argb(140, 0, 0, 0));
            appWidgetManager.updateAppWidget(appWidgetId, views);
            return;
        }
        
        // Show loading state
        views.setTextViewText(R.id.widget_text, "...");
        appWidgetManager.updateAppWidget(appWidgetId, views);
        
        // Fetch data
        String apiKey = prefs.getApiKey();
        QuotaApiClient client = new QuotaApiClient();
        
        executor.execute(() -> {
            try {
                client.fetchQuota(apiKey, new QuotaApiClient.QuotaCallback() {
                    @Override
                    public void onSuccess(QuotaData data) {
                        mainHandler.post(() -> {
                            updateWidgetWithData(context, appWidgetManager, appWidgetId, 
                                    views, data, prefs);
                        });
                    }
                    
                    @Override
                    public void onError(String error) {
                        mainHandler.post(() -> {
                            // Show last known good data if available
                            double lastPct = prefs.getLastPercentage();
                            if (lastPct > 0) {
                                views.setTextViewText(R.id.widget_text, 
                                        Math.round(lastPct) + "%*");
                                int color = getColorForPercentage(lastPct);
                                views.setInt(R.id.widget_container, "setBackgroundColor", color);
                            } else {
                                views.setTextViewText(R.id.widget_text, "ERR");
                                views.setInt(R.id.widget_container, "setBackgroundColor",
                                        android.graphics.Color.argb(140, 232, 71, 97));
                            }
                            appWidgetManager.updateAppWidget(appWidgetId, views);
                        });
                    }
                });
            } catch (Exception e) {
                mainHandler.post(() -> {
                    views.setTextViewText(R.id.widget_text, "ERR");
                    appWidgetManager.updateAppWidget(appWidgetId, views);
                });
            }
        });
    }
    
    private void updateWidgetWithData(Context context, AppWidgetManager appWidgetManager,
                                     int appWidgetId, RemoteViews views, QuotaData data, 
                                     QuotaPreferences prefs) {
        int percentage = (int) Math.round(data.percentage);
        views.setTextViewText(R.id.widget_text, percentage + "%");
        
        int color = getColorForPercentage(data.percentage);
        views.setInt(R.id.widget_container, "setBackgroundColor", color);
        
        prefs.saveLastQuotaData(data.percentage, data.resetTime);
        
        appWidgetManager.updateAppWidget(appWidgetId, views);
    }
    
    private int getColorForPercentage(double percentage) {
        if (percentage < 50) {
            return android.graphics.Color.argb(242, 51, 199, 77); // Green
        } else if (percentage < 80) {
            return android.graphics.Color.argb(242, 242, 191, 51); // Yellow
        } else {
            return android.graphics.Color.argb(242, 232, 71, 97); // Red
        }
    }
    
    @Override
    public void onDisabled(Context context) {
        executor.shutdown();
        super.onDisabled(context);
    }
}
