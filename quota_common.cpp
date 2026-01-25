#include "quota_common.h"

// ============================================================================
// CURL Utilities Implementation
// ============================================================================

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append((char*)contents, total_size);
    return total_size;
}

RequestResult make_request(const std::string& auth_header) {
    RequestResult out;

    CURL* curl = curl_easy_init();
    if (!curl) {
        out.curl_code = CURLE_FAILED_INIT;
        out.curl_error = "curl_easy_init failed";
        return out;
    }

    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, auth_header.c_str());

    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, "https://app.firmware.ai/api/v1/quota");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

    out.curl_code = curl_easy_perform(curl);
    out.body = std::move(response);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    out.http_code = http_code;
    if (errbuf[0] != '\0') {
        out.curl_error = errbuf;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return out;
}

std::string build_auth_header(AuthMethod method, const std::string& api_key, const std::string& token) {
    switch (method) {
        case AuthMethod::BearerFullKey:
            return "Authorization: Bearer " + api_key;
        case AuthMethod::BearerToken:
            return "Authorization: Bearer " + token;
        case AuthMethod::XApiKey:
            return "X-API-Key: " + api_key;
        case AuthMethod::AuthorizationRaw:
            return "Authorization: " + api_key;
    }
    return "Authorization: Bearer " + api_key;
}

bool is_http_success(long code) {
    return code >= 200 && code < 300;
}

bool is_unauthorized(const std::string& response) {
    return response.find("Unauthorized") != std::string::npos || 
           response.find("unauthorized") != std::string::npos;
}

bool is_auth_failure(const RequestResult& r) {
    if (r.http_code == 401) {
        return true;
    }
    return is_unauthorized(r.body);
}

RequestResult try_auth_methods(const std::string& api_key,
                               const std::string& token,
                               std::optional<AuthMethod>& preferred_method,
                               std::optional<AuthMethod>* used_method_out) {
    auto attempt = [&](AuthMethod m) -> RequestResult {
        return make_request(build_auth_header(m, api_key, token));
    };

    const AuthMethod all_methods[] = {
        AuthMethod::BearerFullKey,
        AuthMethod::BearerToken,
        AuthMethod::XApiKey,
        AuthMethod::AuthorizationRaw,
    };

    auto check_success = [&](const RequestResult& r) {
        if (r.curl_code != CURLE_OK) {
            return false;
        }
        if (!is_http_success(r.http_code)) {
            return false;
        }
        if (is_auth_failure(r)) {
            return false;
        }
        return true;
    };

    RequestResult last;

    // First try the cached method (if any).
    if (preferred_method.has_value()) {
        last = attempt(*preferred_method);
        if (check_success(last)) {
            if (used_method_out) {
                *used_method_out = *preferred_method;
            }
            return last;
        }

        // If it wasn't an auth failure, don't spam other auth methods.
        if (last.curl_code != CURLE_OK || (!is_auth_failure(last) && !is_http_success(last.http_code))) {
            return last;
        }
    }

    // Fall back through all auth methods.
    for (AuthMethod m : all_methods) {
        if (preferred_method.has_value() && m == *preferred_method) {
            continue;
        }

        last = attempt(m);
        if (check_success(last)) {
            preferred_method = m;
            if (used_method_out) {
                *used_method_out = m;
            }
            return last;
        }

        // Stop early if the failure isn't auth-related.
        if (last.curl_code != CURLE_OK || (!is_auth_failure(last) && !is_http_success(last.http_code))) {
            break;
        }
    }

    return last;
}

// ============================================================================
// Token/Key Utilities Implementation
// ============================================================================

std::string extract_token(const std::string& api_key) {
    if (api_key.substr(0, 7) == "fw_api_") {
        return api_key.substr(7);
    }
    return api_key;
}

std::string truncate_for_display(const std::string& s, size_t max_len) {
    if (s.size() <= max_len) {
        return s;
    }
    return s.substr(0, max_len) + "...";
}

// ============================================================================
// Time Utilities Implementation
// ============================================================================

bool parse_iso8601_utc_to_time_t(const std::string& iso_timestamp, time_t* out) {
    if (!out) {
        return false;
    }
    if (iso_timestamp.length() < 19) {
        return false;
    }

    struct tm tm_info = {};
    std::istringstream ss(iso_timestamp);
    ss >> std::get_time(&tm_info, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) {
        return false;
    }

    time_t utc_time = timegm(&tm_info);
    if (utc_time == -1) {
        return false;
    }

    *out = utc_time;
    return true;
}

std::string format_duration_compact(int64_t seconds) {
    if (seconds < 0) {
        seconds = 0;
    }

    int64_t hours = seconds / 3600;
    int64_t minutes = (seconds % 3600) / 60;
    int64_t secs = seconds % 60;

    std::ostringstream out;
    if (hours > 0) {
        out << hours << "h " << minutes << "m";
        return out.str();
    }
    if (minutes > 0) {
        out << minutes << "m " << secs << "s";
        return out.str();
    }
    out << secs << "s";
    return out.str();
}

std::string format_duration_tight(int64_t seconds) {
    if (seconds < 0) {
        seconds = 0;
    }

    int64_t hours = seconds / 3600;
    int64_t minutes = (seconds % 3600) / 60;
    int64_t secs = seconds % 60;

    std::ostringstream out;
    if (hours > 99) {
        return "99h+";
    }
    if (hours > 0) {
        out << hours << "h" << minutes << "m";
        return out.str();
    }
    if (minutes > 0) {
        out << minutes << "m" << secs << "s";
        return out.str();
    }
    out << secs << "s";
    return out.str();
}

std::string format_timestamp(const std::string& iso_timestamp) {
    // Simple parsing for ISO 8601 format: YYYY-MM-DDTHH:MM:SS.sssZ
    if (iso_timestamp.length() < 19) {
        return iso_timestamp;
    }
    
    struct tm tm_info = {};
    std::istringstream ss(iso_timestamp);
    
    // Parse: YYYY-MM-DDTHH:MM:SS (this is in UTC)
    ss >> std::get_time(&tm_info, "%Y-%m-%dT%H:%M:%S");
    
    if (ss.fail()) {
        return iso_timestamp; // Return original if parsing fails
    }
    
    // Convert from UTC to time_t
    time_t utc_time = timegm(&tm_info);
    
    if (utc_time == -1) {
        return iso_timestamp; // Return original if conversion fails
    }
    
    // Convert to local time
    struct tm* local_tm = localtime(&utc_time);
    if (!local_tm) {
        return iso_timestamp; // Return original if conversion fails
    }
    
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", local_tm);
    
    return std::string(buffer);
}

std::string get_timestamp_string() {
    time_t now = time(nullptr);
    struct tm* local_tm = localtime(&now);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local_tm);
    return std::string(buffer);
}

// ============================================================================
// Logging Implementation
// ============================================================================

QuotaData read_last_log_entry(const std::string& log_file) {
    QuotaData last_data = {0.0, 0.0, "", 0};
    
    std::ifstream file(log_file);
    if (!file.is_open()) {
        return last_data; // No previous log
    }
    
    std::string line;
    std::string last_line;
    
    // Skip header if present
    std::getline(file, line);
    if (line.find("Timestamp") == std::string::npos) {
        last_line = line; // First line is data, not header
    }
    
    // Read to end to get last line
    while (std::getline(file, line)) {
        if (!line.empty()) {
            last_line = line;
        }
    }
    file.close();
    
    if (last_line.empty()) {
        return last_data;
    }
    
    // Parse CSV: Timestamp,Used,Percentage,Reset,Event
    std::istringstream ss(last_line);
    std::string timestamp_str, used_str, percentage_str, reset_str, event;
    
    std::getline(ss, timestamp_str, ',');
    std::getline(ss, used_str, ',');
    std::getline(ss, percentage_str, ',');
    std::getline(ss, reset_str, ',');
    
    try {
        last_data.used = std::stod(used_str);
        last_data.percentage = std::stod(percentage_str);
        last_data.reset_time = reset_str;
        
        // Parse timestamp to time_t
        struct tm tm_info = {};
        strptime(timestamp_str.c_str(), "%Y-%m-%d %H:%M:%S", &tm_info);
        last_data.timestamp = mktime(&tm_info);
    } catch (...) {
        // Parsing failed, return empty data
    }
    
    return last_data;
}

std::string detect_event(const QuotaData& current, const QuotaData& previous) {
    if (previous.timestamp == 0) {
        return "FIRST_RUN";
    }
    
    // Calculate time difference in hours
    double hours_diff = difftime(current.timestamp, previous.timestamp) / 3600.0;
    
    // If usage decreased significantly (more than 20%), it's likely a reset
    if (current.percentage < previous.percentage - 20.0) {
        return "QUOTA_RESET";
    }
    
    // If more than 5 hours passed and usage is low, might be a reset
    if (hours_diff >= 5.0 && current.percentage < 10.0) {
        return "POSSIBLE_RESET";
    }
    
    // If usage increased significantly
    if (current.percentage > previous.percentage + 10.0) {
        return "HIGH_USAGE";
    }
    
    // Normal update
    return "UPDATE";
}

void write_log_entry(const std::string& log_file, const QuotaData& data, const std::string& event) {
    bool file_exists = false;
    struct stat buffer;
    if (stat(log_file.c_str(), &buffer) == 0) {
        file_exists = true;
    }
    
    std::ofstream file(log_file, std::ios::app);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open log file: " << log_file << std::endl;
        return;
    }
    
    // Write header if new file
    if (!file_exists) {
        file << "Timestamp,Used,Percentage,Reset,Event" << std::endl;
    }
    
    // Write data
    file << get_timestamp_string() << ","
         << std::fixed << std::setprecision(4) << data.used << ","
         << std::fixed << std::setprecision(2) << data.percentage << ","
         << data.reset_time << ","
         << event << std::endl;
    
    file.close();
}
