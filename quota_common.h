#ifndef QUOTA_COMMON_H
#define QUOTA_COMMON_H

#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <unistd.h>
#include <fstream>
#include <sys/stat.h>
#include <optional>
#include <cmath>

using json = nlohmann::json;

// ============================================================================
// Constants
// ============================================================================

static constexpr int kQuotaWindowSeconds = 5 * 60 * 60;

// ============================================================================
// Data Structures
// ============================================================================

// Structure to hold quota data
struct QuotaData {
    double used;
    double percentage;
    std::string reset_time;
    time_t timestamp;
};

// Structure to hold HTTP request results
struct RequestResult {
    CURLcode curl_code = CURLE_OK;
    long http_code = 0;
    std::string body;
    std::string curl_error;
};

// Authentication methods enumeration
enum class AuthMethod {
    BearerFullKey,
    BearerToken,
    XApiKey,
    AuthorizationRaw,
};

// ============================================================================
// Function Declarations - CURL Utilities
// ============================================================================

// Callback function to write curl response to string
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp);

// Make HTTP request with given auth header
RequestResult make_request(const std::string& auth_header);

// Build authentication header based on method
std::string build_auth_header(AuthMethod method, const std::string& api_key, const std::string& token);

// Check if response is successful HTTP code
bool is_http_success(long code);

// Check if response contains unauthorized error
bool is_unauthorized(const std::string& response);

// Check if result indicates auth failure
bool is_auth_failure(const RequestResult& r);

// Try different authentication methods
RequestResult try_auth_methods(const std::string& api_key,
                               const std::string& token,
                               std::optional<AuthMethod>& preferred_method,
                               std::optional<AuthMethod>* used_method_out);

// ============================================================================
// Function Declarations - Token/Key Utilities
// ============================================================================

// Extract token from API key (remove fw_api_ prefix if present)
std::string extract_token(const std::string& api_key);

// Truncate string for display
std::string truncate_for_display(const std::string& s, size_t max_len);

// ============================================================================
// Function Declarations - Time Utilities
// ============================================================================

// Parse ISO 8601 UTC timestamp to time_t
bool parse_iso8601_utc_to_time_t(const std::string& iso_timestamp, time_t* out);

// Format duration in compact form (Xh Ym or Ym Zs)
std::string format_duration_compact(int64_t seconds);

// Format duration in tight form (XhYm or YmZs)
std::string format_duration_tight(int64_t seconds);

// Format ISO 8601 timestamp to readable format in local timezone
std::string format_timestamp(const std::string& iso_timestamp);

// Get current timestamp as string
std::string get_timestamp_string();

// ============================================================================
// Function Declarations - Logging
// ============================================================================

// Read last quota entry from log file
QuotaData read_last_log_entry(const std::string& log_file);

// Detect if quota was reset or other events
std::string detect_event(const QuotaData& current, const QuotaData& previous);

// Write log entry
void write_log_entry(const std::string& log_file, const QuotaData& data, const std::string& event);

#endif // QUOTA_COMMON_H
