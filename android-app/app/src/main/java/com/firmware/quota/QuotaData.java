// QuotaData.java - Data model for quota information
package com.firmware.quota;

public class QuotaData {
    // Legacy fields (kept for widget/notification/simple UI).
    // "used" is a fraction (0..1) of the 5h window.
    public double used;
    public double percentage;
    public String resetTime;
    public long timestamp;

    // Extended fields (mirrors panel details).
    public double windowUsed;
    public String windowReset;
    public double weeklyUsed;
    public String weeklyReset;
    public int windowResetsRemaining;
    public long fetchedAtEpochSeconds;
    
    public QuotaData() {
        this.used = 0.0;
        this.percentage = 0.0;
        this.resetTime = "N/A";
        this.timestamp = 0;

        this.windowUsed = 0.0;
        this.windowReset = "N/A";
        this.weeklyUsed = 0.0;
        this.weeklyReset = "N/A";
        this.windowResetsRemaining = 0;
        this.fetchedAtEpochSeconds = 0;
    }
    
    public QuotaData(double used, String resetTime) {
        final String rt = resetTime != null ? resetTime : "N/A";

        // Legacy fields.
        this.used = used;
        this.percentage = used * 100.0;
        this.resetTime = rt;
        this.timestamp = System.currentTimeMillis() / 1000;

        // Extended fields defaults to window.
        this.windowUsed = used;
        this.windowReset = rt;
        this.weeklyUsed = 0.0;
        this.weeklyReset = "N/A";
        this.windowResetsRemaining = 0;
        this.fetchedAtEpochSeconds = this.timestamp;
    }
}
