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
    private static final String RESET_WINDOW_URL = "https://app.firmware.ai/api/v1/quota/reset-window";
    private static final int TIMEOUT_MS = 30000;
    
    private final ExecutorService executor;
    private final Handler mainHandler;
    
    public interface QuotaCallback {
        void onSuccess(QuotaData data);
        void onError(String error);
    }

    public interface ResetWindowCallback {
        void onSuccess(int windowResetsRemaining);
        void onError(String error);
    }

    private static final class ResetAttempt {
        boolean success;
        boolean authFailure;
        int windowResetsRemaining;
        String error;
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

    public void resetWindow(String apiKey, ResetWindowCallback callback) {
        if (apiKey == null || apiKey.isEmpty()) {
            callback.onError("Missing API key");
            return;
        }

        executor.execute(() -> {
            try {
                String token = extractToken(apiKey);
                int remaining = tryAuthMethodsResetWindow(apiKey, token);
                mainHandler.post(() -> callback.onSuccess(remaining));
            } catch (Exception e) {
                Log.e(TAG, "Error resetting window", e);
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

    private int tryAuthMethodsResetWindow(String apiKey, String token) throws Exception {
        ResetAttempt a;

        a = makeResetWindowAttemptAuth("Bearer " + apiKey);
        if (a.success) return a.windowResetsRemaining;
        if (!a.authFailure) throw new Exception(a.error);
        Log.d(TAG, "Reset: Bearer full key failed, trying token...");

        a = makeResetWindowAttemptAuth("Bearer " + token);
        if (a.success) return a.windowResetsRemaining;
        if (!a.authFailure) throw new Exception(a.error);
        Log.d(TAG, "Reset: Bearer token failed, trying X-API-Key...");

        a = makeResetWindowAttemptXApiKey(apiKey);
        if (a.success) return a.windowResetsRemaining;
        if (!a.authFailure) throw new Exception(a.error);
        Log.d(TAG, "Reset: X-API-Key failed");

        throw new Exception("Unauthorized");
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

    private ResetAttempt makeResetWindowAttemptAuth(String authHeader) {
        ResetAttempt out = new ResetAttempt();
        try {
            URL url = new URL(RESET_WINDOW_URL);
            HttpURLConnection conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("POST");
            conn.setRequestProperty("Authorization", authHeader);
            conn.setRequestProperty("Accept", "application/json");
            conn.setConnectTimeout(TIMEOUT_MS);
            conn.setReadTimeout(TIMEOUT_MS);
            conn.setDoOutput(true);

            try {
                conn.getOutputStream().close();
            } catch (Exception ignored) {
            }

            int responseCode = conn.getResponseCode();
            String body = readResponseBody(conn, responseCode);

            if (responseCode >= 200 && responseCode < 300) {
                out.success = true;
                out.windowResetsRemaining = parseResetWindowResponse(body);
                return out;
            }
            out.authFailure = responseCode == HttpURLConnection.HTTP_UNAUTHORIZED || isUnauthorizedBody(body);
            out.error = buildResetWindowError(responseCode, body);
            return out;
        } catch (Exception e) {
            out.authFailure = false;
            out.error = e.getMessage() != null ? e.getMessage() : "Reset failed";
            return out;
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

    private ResetAttempt makeResetWindowAttemptXApiKey(String apiKey) {
        ResetAttempt out = new ResetAttempt();
        try {
            URL url = new URL(RESET_WINDOW_URL);
            HttpURLConnection conn = (HttpURLConnection) url.openConnection();
            conn.setRequestMethod("POST");
            conn.setRequestProperty("X-API-Key", apiKey);
            conn.setRequestProperty("Accept", "application/json");
            conn.setConnectTimeout(TIMEOUT_MS);
            conn.setReadTimeout(TIMEOUT_MS);
            conn.setDoOutput(true);

            try {
                conn.getOutputStream().close();
            } catch (Exception ignored) {
            }

            int responseCode = conn.getResponseCode();
            String body = readResponseBody(conn, responseCode);

            if (responseCode >= 200 && responseCode < 300) {
                out.success = true;
                out.windowResetsRemaining = parseResetWindowResponse(body);
                return out;
            }
            out.authFailure = responseCode == HttpURLConnection.HTTP_UNAUTHORIZED || isUnauthorizedBody(body);
            out.error = buildResetWindowError(responseCode, body);
            return out;
        } catch (Exception e) {
            out.authFailure = false;
            out.error = e.getMessage() != null ? e.getMessage() : "Reset failed";
            return out;
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

    private static String readResponseBody(HttpURLConnection conn, int responseCode) {
        try {
            java.io.InputStream is = (responseCode >= 200 && responseCode < 300) ? conn.getInputStream() : conn.getErrorStream();
            if (is == null) {
                return "";
            }
            BufferedReader reader = new BufferedReader(new InputStreamReader(is));
            StringBuilder response = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) {
                response.append(line);
            }
            reader.close();
            return response.toString();
        } catch (Exception e) {
            return "";
        }
    }

    private static int parseResetWindowResponse(String json) throws Exception {
        if (json == null || json.isEmpty()) {
            throw new Exception("Empty response");
        }
        JSONObject obj = new JSONObject(json);

        // Success payload: { success: true, windowResetsRemaining: n }
        if (obj.has("success") && !obj.isNull("success") && obj.getBoolean("success")) {
            if (obj.has("windowResetsRemaining") && !obj.isNull("windowResetsRemaining")) {
                return obj.getInt("windowResetsRemaining");
            }
            // If backend ever omits it, treat as unknown but non-fatal.
            return -1;
        }

        // Error payload: { success: false, error, message }
        if (obj.has("message") && !obj.isNull("message")) {
            throw new Exception(obj.getString("message"));
        }
        if (obj.has("error") && !obj.isNull("error")) {
            throw new Exception(obj.getString("error"));
        }
        throw new Exception("Reset failed");
    }

    private static boolean isUnauthorizedBody(String body) {
        if (body == null || body.isEmpty()) return false;
        if (body.contains("Unauthorized") || body.contains("unauthorized")) return true;
        try {
            JSONObject obj = new JSONObject(body);
            if (obj.has("error") && !obj.isNull("error")) {
                String e = obj.optString("error", "");
                return "Unauthorized".equalsIgnoreCase(e);
            }
        } catch (Exception ignored) {
        }
        return false;
    }

    private static String buildResetWindowError(int responseCode, String body) {
        if (body != null && !body.isEmpty()) {
            try {
                JSONObject obj = new JSONObject(body);
                if (obj.has("message") && !obj.isNull("message")) {
                    return obj.getString("message");
                }
                if (obj.has("error") && !obj.isNull("error")) {
                    return obj.getString("error");
                }
                if (obj.has("error") && obj.get("error") instanceof String) {
                    return obj.getString("error");
                }
            } catch (Exception ignored) {
                // Fall through.
            }
        }
        return "HTTP error: " + responseCode;
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
