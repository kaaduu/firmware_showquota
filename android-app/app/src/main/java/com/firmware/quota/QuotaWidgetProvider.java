package com.firmware.quota;

import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
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
            setQuotaProgress(views, 0);
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
                                setQuotaProgress(views, (int) Math.round(lastPct));
                            } else {
                                views.setTextViewText(R.id.widget_text, "ERR");
                                setQuotaProgress(views, 0);
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

        setQuotaProgress(views, percentage);
        
        prefs.saveLastQuotaData(data.percentage, data.resetTime);
        
        appWidgetManager.updateAppWidget(appWidgetId, views);
    }

    private void setQuotaProgress(RemoteViews views, int percentage) {
        int clamped = Math.max(0, Math.min(100, percentage));
        int activeId;
        if (clamped < 50) {
            activeId = R.id.widget_progress_green;
        } else if (clamped < 80) {
            activeId = R.id.widget_progress_yellow;
        } else {
            activeId = R.id.widget_progress_red;
        }

        setProgressBarState(views, R.id.widget_progress_green, activeId, clamped);
        setProgressBarState(views, R.id.widget_progress_yellow, activeId, clamped);
        setProgressBarState(views, R.id.widget_progress_red, activeId, clamped);
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
    
    @Override
    public void onDisabled(Context context) {
        executor.shutdown();
        super.onDisabled(context);
    }
}
