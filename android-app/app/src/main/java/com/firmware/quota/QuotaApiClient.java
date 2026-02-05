// QuotaApiClient.java - API client for fetching quota data
package com.firmware.quota;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class QuotaApiClient {
    private static final String TAG = "QuotaApiClient";
    private static final String API_URL = "https://app.firmware.ai/api/v1/quota";
    private static final int TIMEOUT_MS = 30000;
    
    private final ExecutorService executor;
    private final Handler mainHandler;
    
    public interface QuotaCallback {
        void onSuccess(QuotaData data);
        void onError(String error);
    }
    
    public QuotaApiClient() {
        this.executor = Executors.newSingleThreadExecutor();
        this.mainHandler = new Handler(Looper.getMainLooper());
    }
    
    public void fetchQuota(String apiKey, QuotaCallback callback) {
        if (apiKey == null || apiKey.isEmpty()) {
            callback.onError("Missing API key");
            return;
        }
        
        executor.execute(() -> {
            try {
                String token = extractToken(apiKey);
                QuotaData result = tryAuthMethods(apiKey, token);
                
                mainHandler.post(() -> callback.onSuccess(result));
            } catch (Exception e) {
                Log.e(TAG, "Error fetching quota", e);
                mainHandler.post(() -> callback.onError(e.getMessage()));
            }
        });
    }
    
    private QuotaData tryAuthMethods(String apiKey, String token) throws Exception {
        // Try Bearer with full key
        try {
            return makeRequest("Bearer " + apiKey);
        } catch (Exception e) {
            Log.d(TAG, "Bearer full key failed, trying token...");
        }
        
        // Try Bearer with token only
        try {
            return makeRequest("Bearer " + token);
        } catch (Exception e) {
            Log.d(TAG, "Bearer token failed, trying X-API-Key...");
        }
        
        // Try X-API-Key header
        try {
            return makeRequestXApiKey(apiKey);
        } catch (Exception e) {
            Log.d(TAG, "X-API-Key failed");
        }
        
        throw new Exception("All authentication methods failed");
    }
    
    private QuotaData makeRequest(String authHeader) throws Exception {
        URL url = new URL(API_URL);
        HttpURLConnection conn = (HttpURLConnection) url.openConnection();
        conn.setRequestMethod("GET");
        conn.setRequestProperty("Authorization", authHeader);
        conn.setRequestProperty("Accept", "application/json");
        conn.setConnectTimeout(TIMEOUT_MS);
        conn.setReadTimeout(TIMEOUT_MS);
        
        int responseCode = conn.getResponseCode();
        
        if (responseCode == HttpURLConnection.HTTP_OK) {
            BufferedReader reader = new BufferedReader(new InputStreamReader(conn.getInputStream()));
            StringBuilder response = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) {
                response.append(line);
            }
            reader.close();
            
            return parseResponse(response.toString());
        } else {
            throw new Exception("HTTP error: " + responseCode);
        }
    }
    
    private QuotaData makeRequestXApiKey(String apiKey) throws Exception {
        URL url = new URL(API_URL);
        HttpURLConnection conn = (HttpURLConnection) url.openConnection();
        conn.setRequestMethod("GET");
        conn.setRequestProperty("X-API-Key", apiKey);
        conn.setRequestProperty("Accept", "application/json");
        conn.setConnectTimeout(TIMEOUT_MS);
        conn.setReadTimeout(TIMEOUT_MS);
        
        int responseCode = conn.getResponseCode();
        
        if (responseCode == HttpURLConnection.HTTP_OK) {
            BufferedReader reader = new BufferedReader(new InputStreamReader(conn.getInputStream()));
            StringBuilder response = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) {
                response.append(line);
            }
            reader.close();
            
            return parseResponse(response.toString());
        } else {
            throw new Exception("HTTP error: " + responseCode);
        }
    }
    
    private QuotaData parseResponse(String json) throws Exception {
        JSONObject obj = new JSONObject(json);

        // New API fields: windowUsed/windowReset (plus weekly fields not used here).
        // Backward compatible with legacy used/reset.
        Double usedFraction = null;
        if (obj.has("windowUsed") && !obj.isNull("windowUsed")) {
            usedFraction = obj.getDouble("windowUsed");
        } else if (obj.has("used") && !obj.isNull("used")) {
            usedFraction = obj.getDouble("used");
        }

        if (usedFraction == null) {
            throw new Exception("Missing 'windowUsed' (or legacy 'used') field in response");
        }

        // Defensive normalization: if server ever returns percent (0..100), convert to fraction.
        if (usedFraction > 1.0 && usedFraction <= 100.0) {
            usedFraction = usedFraction / 100.0;
        }

        String reset = "N/A";
        if (obj.has("windowReset") && !obj.isNull("windowReset")) {
            reset = obj.getString("windowReset");
        } else if (obj.has("reset") && !obj.isNull("reset")) {
            reset = obj.getString("reset");
        }

        QuotaData out = new QuotaData(usedFraction, reset);

        // Extended fields (best-effort).
        out.windowUsed = usedFraction;
        out.windowReset = reset;
        if (obj.has("weeklyUsed") && !obj.isNull("weeklyUsed")) {
            double wu = obj.getDouble("weeklyUsed");
            if (wu > 1.0 && wu <= 100.0) {
                wu = wu / 100.0;
            }
            out.weeklyUsed = wu;
        }
        if (obj.has("weeklyReset") && !obj.isNull("weeklyReset")) {
            out.weeklyReset = obj.getString("weeklyReset");
        }
        if (obj.has("windowResetsRemaining") && !obj.isNull("windowResetsRemaining")) {
            out.windowResetsRemaining = obj.getInt("windowResetsRemaining");
        }
        out.fetchedAtEpochSeconds = System.currentTimeMillis() / 1000;
        return out;
    }
    
    private String extractToken(String apiKey) {
        if (apiKey.startsWith("fw_api_")) {
            return apiKey.substring(7);
        }
        return apiKey;
    }
    
    public void shutdown() {
        executor.shutdown();
    }
}
