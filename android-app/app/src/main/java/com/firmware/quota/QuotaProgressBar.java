package com.firmware.quota;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.View;

import java.util.ArrayList;
import java.util.List;

public class QuotaProgressBar extends View {
    
    private float percentage = 0f;
    private boolean hasData = false;
    private boolean isStale = false;
    private String resetTime = "N/A";
    private long lastWindowResetTime = 0;
    private int timeLinePx = 2;
    
    private final Paint backgroundPaint;
    private final Paint fillPaint;
    private final Paint borderPaint;
    private final Paint textPaint;
    private final Paint textShadowPaint;
    private final Paint windowTimerPaint;
    private final Paint deltaIncreasePaint;
    private final Paint deltaDecreasePaint;
    private final Paint separatorPaint;
    private final Paint staleOverlayPaint;
    
    private final List<DeltaEntry> deltaHistory = new ArrayList<>();
    
    private static class DeltaEntry {
        final double delta;
        final long timestamp;
        
        DeltaEntry(double delta, long timestamp) {
            this.delta = delta;
            this.timestamp = timestamp;
        }
    }
    
    public QuotaProgressBar(Context context) {
        this(context, null);
    }
    
    public QuotaProgressBar(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }
    
    public QuotaProgressBar(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        
        backgroundPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        backgroundPaint.setColor(Color.argb(140, 0, 0, 0));
        
        fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        
        borderPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        borderPaint.setStyle(Paint.Style.STROKE);
        borderPaint.setStrokeWidth(2f);
        
        textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        textPaint.setTextAlign(Paint.Align.CENTER);
        textPaint.setFakeBoldText(true);
        
        textShadowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        textShadowPaint.setTextAlign(Paint.Align.CENTER);
        textShadowPaint.setFakeBoldText(true);
        
        windowTimerPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        
        deltaIncreasePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        deltaIncreasePaint.setColor(Color.argb(230, 64, 166, 250));
        
        deltaDecreasePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        deltaDecreasePaint.setColor(Color.argb(230, 250, 140, 38));
        
        separatorPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        separatorPaint.setColor(Color.argb(30, 0, 0, 0));
        separatorPaint.setStrokeWidth(1f);
        
        staleOverlayPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        staleOverlayPaint.setColor(Color.argb(64, 250, 184, 38));
        staleOverlayPaint.setStrokeWidth(1f);
    }
    
    public void setPercentage(float percentage) {
        this.percentage = Math.max(0f, Math.min(100f, percentage));
        this.hasData = true;
        invalidate();
    }
    
    public void setHasData(boolean hasData) {
        this.hasData = hasData;
        invalidate();
    }
    
    public void setStale(boolean stale) {
        this.isStale = stale;
        invalidate();
    }
    
    public void setResetTime(String resetTime) {
        this.resetTime = resetTime != null ? resetTime : "N/A";
        invalidate();
    }
    
    public void setLastWindowResetTime(long timestamp) {
        this.lastWindowResetTime = timestamp;
        invalidate();
    }
    
    public void setTimeLinePx(int px) {
        this.timeLinePx = Math.max(0, Math.min(10, px));
        invalidate();
    }
    
    public void addDelta(double delta) {
        deltaHistory.add(new DeltaEntry(delta, System.currentTimeMillis()));
        if (deltaHistory.size() > 5) {
            deltaHistory.remove(0);
        }
        invalidate();
    }
    
    public void clearDeltaHistory() {
        deltaHistory.clear();
        invalidate();
    }
    
    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        
        int width = getWidth();
        int height = getHeight();
        
        if (width <= 0 || height <= 0) return;
        
        // Background
        canvas.drawRect(0, 0, width, height, backgroundPaint);
        
        // Determine colors based on percentage
        int fillColor;
        int textColor;
        float fillFraction = hasData ? (percentage / 100f) : 0f;
        
        if (!hasData) {
            fillColor = Color.argb(242, 153, 153, 153);
            textColor = Color.WHITE;
        } else if (percentage < 50f) {
            fillColor = Color.argb(242, 51, 199, 77);
            textColor = Color.WHITE;
        } else if (percentage < 80f) {
            fillColor = Color.argb(242, 242, 191, 51);
            textColor = Color.argb(255, 33, 33, 33);
        } else {
            fillColor = Color.argb(242, 232, 71, 97);
            textColor = Color.WHITE;
        }
        
        fillPaint.setColor(fillColor);
        textPaint.setColor(textColor);
        textShadowPaint.setColor(textColor == Color.WHITE ? 
                Color.argb(140, 0, 0, 0) : Color.argb(140, 255, 255, 255));
        
        // Fill bar
        int filledWidth = (int) Math.round(fillFraction * width);
        if (filledWidth > 0) {
            canvas.drawRect(0, 0, filledWidth, height, fillPaint);
        }
        
        // Delta history overlay
        if (hasData && !deltaHistory.isEmpty()) {
            drawDeltaHistory(canvas, width, height, filledWidth);
        }
        
        // Stale overlay (diagonal hatch pattern)
        if (isStale) {
            drawStaleOverlay(canvas, width, height);
        }
        
        // Window timer line at bottom
        if (timeLinePx > 0 && hasData) {
            drawWindowTimerLine(canvas, width, height, fillColor);
        }
        
        // Border
        borderPaint.setColor(isStale ? Color.argb(217, 250, 184, 38) : Color.argb(153, 255, 255, 255));
        RectF borderRect = new RectF(1, 1, width - 1, height - 1);
        canvas.drawRect(borderRect, borderPaint);
        
        // Percentage text
        drawPercentageText(canvas, width, height);
    }
    
    private void drawDeltaHistory(Canvas canvas, int width, int height, int filledWidth) {
        if (filledWidth <= 0) return;
        
        final double maxPPPerSeg = 15.0;
        final double minPx = 2.0;
        final double maxHistPx = Math.min(filledWidth, width * 0.35);
        
        List<Double> segmentSizes = new ArrayList<>();
        List<Boolean> isIncrease = new ArrayList<>();
        
        for (DeltaEntry entry : deltaHistory) {
            if (Math.abs(entry.delta) < 0.05) continue;
            double pp = Math.min(Math.abs(entry.delta), maxPPPerSeg);
            double px = (pp / 100.0) * width;
            if (px < minPx) px = minPx;
            segmentSizes.add(px);
            isIncrease.add(entry.delta > 0);
        }
        
        if (segmentSizes.isEmpty()) return;
        
        // Scale to fit
        double sumPx = 0;
        for (double px : segmentSizes) sumPx += px;
        double scale = Math.min(1.0, maxHistPx / sumPx);
        
        // Draw from newest to oldest at leading edge
        double cursor = 0;
        for (int i = segmentSizes.size() - 1; i >= 0; i--) {
            double segWidth = segmentSizes.get(i) * scale;
            Paint paint = isIncrease.get(i) ? deltaIncreasePaint : deltaDecreasePaint;
            
            // Alpha based on recency
            int alpha = 230 - (int)((segmentSizes.size() - 1 - i) * 51);
            alpha = Math.max(77, Math.min(230, alpha));
            paint.setAlpha(alpha);
            
            double x = filledWidth - cursor - segWidth;
            if (x < filledWidth && x + segWidth > 0) {
                canvas.drawRect((float) Math.max(0, x), 0, 
                        (float) Math.min(filledWidth, x + segWidth), height, paint);
            }
            
            // Separator line
            canvas.drawLine((float) x, 0, (float) x, height, separatorPaint);
            
            cursor += segWidth;
            if (cursor >= maxHistPx) break;
        }
    }
    
    private void drawStaleOverlay(Canvas canvas, int width, int height) {
        int step = 6;
        for (int x = -height; x < width + height; x += step) {
            canvas.drawLine(x, 0, x + height, height, staleOverlayPaint);
        }
    }
    
    private void drawWindowTimerLine(Canvas canvas, int width, int height, int fillColor) {
        long remainingSeconds = calculateRemainingSeconds();
        if (remainingSeconds < 0 || remainingSeconds > (5 * 60 * 60) - 5) return;
        
        double fraction = (double) remainingSeconds / (5.0 * 60 * 60);
        double lineWidth = Math.max(0, Math.min(1.0, fraction)) * width;
        int y0 = Math.max(0, height - timeLinePx);
        
        // White when green, cyan when yellow or red
        if (fillColor == Color.argb(242, 51, 199, 77)) {
            windowTimerPaint.setColor(Color.argb(242, 255, 255, 255));
        } else {
            windowTimerPaint.setColor(Color.argb(242, 0, 191, 255));
        }
        
        canvas.drawRect(0, y0, (float) lineWidth, Math.min(y0 + timeLinePx, height), windowTimerPaint);
    }
    
    private void drawPercentageText(Canvas canvas, int width, int height) {
        String text;
        if (!hasData) {
            text = "--";
        } else if (isStale) {
            text = Math.round(percentage) + "%*";
        } else {
            text = Math.round(percentage) + "%";
        }
        
        float textSize = Math.max(24f, Math.min(height * 0.7f, 48f));
        textPaint.setTextSize(textSize);
        textShadowPaint.setTextSize(textSize);
        
        float x = width / 2f;
        float y = height / 2f + (textPaint.getTextSize() / 3f);
        
        // Shadow
        canvas.drawText(text, x + 2, y + 2, textShadowPaint);
        // Text
        canvas.drawText(text, x, y, textPaint);
    }
    
    private long calculateRemainingSeconds() {
        long windowSeconds = 5 * 60 * 60; // 5 hours
        
        // Try to parse reset time from server
        if (!"N/A".equals(resetTime) && !resetTime.isEmpty()) {
            try {
                // API may return either whole seconds or milliseconds (e.g. 2026-02-12T00:00:00.000Z)
                java.util.Date resetDate = parseUtcIso8601(resetTime);
                long untilReset = (resetDate.getTime() - System.currentTimeMillis()) / 1000;
                if (untilReset >= 0 && untilReset <= windowSeconds) {
                    return untilReset;
                }
            } catch (Exception e) {
                // Parse failed, continue to fallback
            }
        }
        
        // Fallback to local window reset detection
        if (lastWindowResetTime > 0) {
            long ageSeconds = (System.currentTimeMillis() - lastWindowResetTime) / 1000;
            long untilReset = windowSeconds - ageSeconds;
            if (untilReset >= 0 && untilReset <= windowSeconds) {
                return untilReset;
            }
        }
        
        // Last resort: epoch-aligned window
        long nowSeconds = System.currentTimeMillis() / 1000;
        long windowStart = (nowSeconds / windowSeconds) * windowSeconds;
        long ageSeconds = nowSeconds - windowStart;
        return windowSeconds - ageSeconds;
    }

    private static java.util.Date parseUtcIso8601(String iso) throws java.text.ParseException {
        java.util.TimeZone utc = java.util.TimeZone.getTimeZone("UTC");

        // First try with milliseconds.
        java.text.SimpleDateFormat sdfMs = new java.text.SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'");
        sdfMs.setTimeZone(utc);
        try {
            return sdfMs.parse(iso);
        } catch (java.text.ParseException ignored) {
            // Fall through.
        }

        // Fallback: whole seconds.
        java.text.SimpleDateFormat sdf = new java.text.SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'");
        sdf.setTimeZone(utc);
        return sdf.parse(iso);
    }
}
