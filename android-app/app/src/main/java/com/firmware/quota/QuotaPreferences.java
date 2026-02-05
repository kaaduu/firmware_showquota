// QuotaPreferences.java - SharedPreferences wrapper for API key storage
package com.firmware.quota;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;

import java.security.KeyStore;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

public class QuotaPreferences {
    private static final String PREFS_NAME = "FirmwareQuotaPrefs";
    private static final String KEY_API_KEY = "api_key";
    private static final String KEY_LAST_PERCENTAGE = "last_percentage";
    private static final String KEY_LAST_RESET_TIME = "last_reset_time";
    private static final String KEY_LAST_UPDATE = "last_update";
    private static final String ANDROID_KEYSTORE = "AndroidKeyStore";
    private static final String KEY_ALIAS = "FirmwareQuotaKey";
    private static final int GCM_TAG_LENGTH = 128;
    private static final int GCM_IV_LENGTH = 12;
    
    private final SharedPreferences prefs;
    private final Context context;
    
    public QuotaPreferences(Context context) {
        this.context = context.getApplicationContext();
        this.prefs = this.context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
    }
    
    public void saveApiKey(String apiKey) {
        if (apiKey == null || apiKey.isEmpty()) {
            prefs.edit().remove(KEY_API_KEY).apply();
            return;
        }
        
        try {
            String encrypted = encrypt(apiKey);
            prefs.edit().putString(KEY_API_KEY, encrypted).apply();
        } catch (Exception e) {
            // Fallback to plain storage if encryption fails
            prefs.edit().putString(KEY_API_KEY, apiKey).apply();
        }
    }
    
    public String getApiKey() {
        String stored = prefs.getString(KEY_API_KEY, null);
        if (stored == null) return null;
        
        try {
            return decrypt(stored);
        } catch (Exception e) {
            // If decryption fails, assume it's plain text (legacy)
            return stored;
        }
    }
    
    public void saveLastQuotaData(double percentage, String resetTime) {
        SharedPreferences.Editor editor = prefs.edit();
        editor.putFloat(KEY_LAST_PERCENTAGE, (float) percentage);
        editor.putString(KEY_LAST_RESET_TIME, resetTime);
        editor.putLong(KEY_LAST_UPDATE, System.currentTimeMillis());
        editor.apply();
    }
    
    public double getLastPercentage() {
        return prefs.getFloat(KEY_LAST_PERCENTAGE, 0f);
    }
    
    public String getLastResetTime() {
        return prefs.getString(KEY_LAST_RESET_TIME, "N/A");
    }
    
    public long getLastUpdateTime() {
        return prefs.getLong(KEY_LAST_UPDATE, 0);
    }
    
    public boolean hasApiKey() {
        return getApiKey() != null && !getApiKey().isEmpty();
    }
    
    private String encrypt(String plaintext) throws Exception {
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.ENCRYPT_MODE, getOrCreateKey());
        byte[] iv = cipher.getIV();
        byte[] ciphertext = cipher.doFinal(plaintext.getBytes("UTF-8"));
        
        // Combine IV + ciphertext
        byte[] combined = new byte[iv.length + ciphertext.length];
        System.arraycopy(iv, 0, combined, 0, iv.length);
        System.arraycopy(ciphertext, 0, combined, iv.length, ciphertext.length);
        
        return Base64.encodeToString(combined, Base64.DEFAULT);
    }
    
    private String decrypt(String encrypted) throws Exception {
        byte[] combined = Base64.decode(encrypted, Base64.DEFAULT);
        
        // Extract IV and ciphertext
        byte[] iv = new byte[GCM_IV_LENGTH];
        byte[] ciphertext = new byte[combined.length - GCM_IV_LENGTH];
        System.arraycopy(combined, 0, iv, 0, iv.length);
        System.arraycopy(combined, iv.length, ciphertext, 0, ciphertext.length);
        
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.DECRYPT_MODE, getOrCreateKey(), new GCMParameterSpec(GCM_TAG_LENGTH, iv));
        byte[] plaintext = cipher.doFinal(ciphertext);
        
        return new String(plaintext, "UTF-8");
    }
    
    private SecretKey getOrCreateKey() throws Exception {
        KeyStore keyStore = KeyStore.getInstance(ANDROID_KEYSTORE);
        keyStore.load(null);
        
        if (keyStore.containsAlias(KEY_ALIAS)) {
            return (SecretKey) keyStore.getKey(KEY_ALIAS, null);
        }
        
        KeyGenParameterSpec spec = new KeyGenParameterSpec.Builder(
                KEY_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build();
        
        KeyGenerator keyGenerator = KeyGenerator.getInstance("AES", ANDROID_KEYSTORE);
        keyGenerator.init(spec);
        return keyGenerator.generateKey();
    }
}
