#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <sys/ioctl.h>
#include <unistd.h>
#include <clocale>
#include <fstream>
#include <sys/stat.h>
#include <optional>
#include <cmath>
#include <signal.h>

#ifdef GUI_MODE_ENABLED
extern "C" {
#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>
#include <libnotify/notify.h>
}
#include <pthread.h>
#endif

using json = nlohmann::json;

static constexpr int kQuotaWindowSeconds = 5 * 60 * 60;

static volatile sig_atomic_t g_cursor_hidden = 0;

static void cursor_hide_raw() {
    static const char kHide[] = "\033[?25l";
    (void)!write(STDOUT_FILENO, kHide, sizeof(kHide) - 1);
}

static void cursor_show_raw() {
    static const char kShow[] = "\033[?25h";
    (void)!write(STDOUT_FILENO, kShow, sizeof(kShow) - 1);
}

static void show_cursor_if_hidden() {
    if (g_cursor_hidden) {
        cursor_show_raw();
        g_cursor_hidden = 0;
    }
}

static void hide_cursor_if_tty() {
    if (!isatty(STDOUT_FILENO)) {
        return;
    }
    if (!g_cursor_hidden) {
        cursor_hide_raw();
        g_cursor_hidden = 1;
    }
}

static void handle_term_signal(int sig) {
    if (g_cursor_hidden) {
        cursor_show_raw();
        g_cursor_hidden = 0;
    }
    _exit(128 + sig);
}

// Structure to hold quota data
struct QuotaData {
    double used;
    double percentage;
    std::string reset_time;
    time_t timestamp;
};

// Callback function to write curl response to string
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append((char*)contents, total_size);
    return total_size;
}

// Get terminal width
int get_terminal_width() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
        return w.ws_col;
    }
    return 80; // Default fallback
}

// Check if terminal supports UTF-8
bool is_utf8_locale() {
    // Initialize locale from environment
    std::setlocale(LC_CTYPE, "");
    
    const char* locale = std::setlocale(LC_CTYPE, nullptr);
    if (locale) {
        std::string loc(locale);
        return loc.find("UTF-8") != std::string::npos || 
               loc.find("utf8") != std::string::npos;
    }
    
    // Also check LANG environment variable as fallback
    const char* lang = std::getenv("LANG");
    if (lang) {
        std::string lang_str(lang);
        return lang_str.find("UTF-8") != std::string::npos || 
               lang_str.find("utf8") != std::string::npos;
    }
    
    return false;
}

static bool parse_iso8601_utc_to_time_t(const std::string& iso_timestamp, time_t* out) {
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

static std::string format_duration_compact(int64_t seconds) {
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

static std::string format_duration_tight(int64_t seconds) {
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

// Get ANSI color code based on usage percentage
std::string get_color_for_percentage(double percentage, bool use_colors) {
    if (!use_colors) {
        return "";
    }
    
    if (percentage < 50.0) {
        return "\033[32m"; // Green
    } else if (percentage < 80.0) {
        return "\033[33m"; // Yellow
    } else {
        return "\033[31m"; // Red
    }
}

static std::string get_color_for_percentage_tiny(double percentage, bool use_colors) {
    if (!use_colors) {
        return "";
    }

    // Tighter thresholds so "near 100%" trends red.
    if (percentage < 70.0) {
        return "\033[32m"; // Green
    } else if (percentage < 90.0) {
        return "\033[33m"; // Yellow
    }
    return "\033[31m"; // Red
}

// Render progress bar
std::string render_progress_bar(double percentage, int terminal_width, bool use_colors) {
    // Calculate bar width (leave space for "Usage: [] XX.XX%")
    int fixed_width = 17; // "Usage: [] " + "XX.XX%" approximately
    int bar_width = terminal_width - fixed_width;
    
    // Ensure minimum bar width
    if (bar_width < 20) {
        bar_width = 20;
    }
    if (bar_width > 50) {
        bar_width = 50; // Maximum reasonable width
    }
    
    // Calculate filled and empty blocks
    int filled = static_cast<int>((percentage / 100.0) * bar_width);
    int empty = bar_width - filled;
    
    // Ensure bounds
    if (filled < 0) filled = 0;
    if (filled > bar_width) filled = bar_width;
    if (empty < 0) empty = 0;
    
    // Choose characters based on locale
    bool use_utf8 = is_utf8_locale();
    std::string filled_char = use_utf8 ? "█" : "#";
    std::string empty_char = use_utf8 ? "░" : "-";
    
    // Get color
    std::string color = get_color_for_percentage(percentage, use_colors);
    std::string reset = use_colors ? "\033[0m" : "";
    
    // Build the bar
    std::ostringstream bar;
    bar << "Usage: [" << color;
    for (int i = 0; i < filled; i++) {
        bar << filled_char;
    }
    for (int i = 0; i < empty; i++) {
        bar << empty_char;
    }
    bar << reset << "] ";
    bar << std::fixed << std::setprecision(2) << percentage << "%";
    
    return bar.str();
}

static std::string truncate_right(const std::string& s, size_t max_len) {
    if (s.size() <= max_len) {
        return s;
    }
    return s.substr(0, max_len);
}

static std::string render_progress_bar_compact(double percentage, int terminal_width, bool use_colors) {
    int pct_i = static_cast<int>(std::llround(percentage));
    if (pct_i < 0) pct_i = 0;
    if (pct_i > 100) pct_i = 100;

    std::string suffix = std::to_string(pct_i) + "%";
    const std::string label = "U:";

    int label_len = static_cast<int>(label.size());
    int suffix_len = static_cast<int>(suffix.size());
    int bar_width = terminal_width - (label_len + suffix_len + 3);
    if (bar_width < 1) {
        bar_width = 1;
        int max_suffix = terminal_width - (label_len + bar_width + 3);
        if (max_suffix < 0) max_suffix = 0;
        suffix = truncate_right(suffix, static_cast<size_t>(max_suffix));
        suffix_len = static_cast<int>(suffix.size());
    }

    int filled = static_cast<int>((static_cast<double>(pct_i) / 100.0) * bar_width);
    int empty = bar_width - filled;
    if (filled < 0) filled = 0;
    if (filled > bar_width) filled = bar_width;
    if (empty < 0) empty = 0;

    bool use_utf8 = is_utf8_locale();
    std::string filled_char = use_utf8 ? "█" : "#";
    std::string empty_char = use_utf8 ? "░" : "-";

    std::string color = get_color_for_percentage(static_cast<double>(pct_i), use_colors);
    std::string reset = use_colors ? "\033[0m" : "";

    std::ostringstream bar;
    bar << label << "[" << color;
    for (int i = 0; i < filled; i++) {
        bar << filled_char;
    }
    for (int i = 0; i < empty; i++) {
        bar << empty_char;
    }
    bar << reset << "] ";
    bar << suffix;
    return bar.str();
}

static std::string render_tiny_usage_line(double percentage, bool use_colors) {
    int pct_i = static_cast<int>(std::llround(percentage));
    if (pct_i < 0) pct_i = 0;
    if (pct_i > 100) pct_i = 100;

    std::string color = get_color_for_percentage_tiny(static_cast<double>(pct_i), use_colors);
    std::string reset = use_colors ? "\033[0m" : "";

    std::ostringstream out;
    out << color << pct_i << "%" << reset;
    return out.str();
}

static std::string render_reset_time_bar(time_t reset_utc, int terminal_width, bool use_colors) {
    time_t now = time(nullptr);
    int64_t remaining_seconds = static_cast<int64_t>(difftime(reset_utc, now));
    if (remaining_seconds < 0) {
        remaining_seconds = 0;
    }

    const int64_t window_seconds = kQuotaWindowSeconds;

    // Clamp for bar visualization (but keep the real remaining for text)
    int64_t remaining_for_bar = remaining_seconds;
    if (remaining_for_bar > window_seconds) {
        remaining_for_bar = window_seconds;
    }

    double remaining_pct = window_seconds > 0 ? (static_cast<double>(remaining_for_bar) * 100.0 / static_cast<double>(window_seconds)) : 0.0;
    if (remaining_pct < 0.0) remaining_pct = 0.0;
    if (remaining_pct > 100.0) remaining_pct = 100.0;

    // We want the bar to become "worse" as remaining time gets low.
    double approaching_pct = 100.0 - remaining_pct;

    int fixed_width = 34; // "Reset: [] " + "XXh YYm left (of 5h)"
    int bar_width = terminal_width - fixed_width;
    if (bar_width < 20) {
        bar_width = 20;
    }
    if (bar_width > 50) {
        bar_width = 50;
    }

    int filled = static_cast<int>((remaining_pct / 100.0) * bar_width);
    int empty = bar_width - filled;

    if (filled < 0) filled = 0;
    if (filled > bar_width) filled = bar_width;
    if (empty < 0) empty = 0;

    bool use_utf8 = is_utf8_locale();
    std::string filled_char = use_utf8 ? "█" : "#";
    std::string empty_char = use_utf8 ? "░" : "-";

    std::string color = get_color_for_percentage(approaching_pct, use_colors);
    std::string reset = use_colors ? "\033[0m" : "";

    std::ostringstream bar;
    bar << "Reset: [" << color;
    for (int i = 0; i < filled; i++) {
        bar << filled_char;
    }
    for (int i = 0; i < empty; i++) {
        bar << empty_char;
    }
    bar << reset << "] ";
    bar << format_duration_compact(remaining_seconds) << " left (of 5h)";

    return bar.str();
}

static std::string render_reset_time_bar_compact(time_t reset_utc, int terminal_width, bool use_colors) {
    time_t now = time(nullptr);
    int64_t remaining_seconds = static_cast<int64_t>(difftime(reset_utc, now));
    if (remaining_seconds < 0) {
        remaining_seconds = 0;
    }

    const int64_t window_seconds = kQuotaWindowSeconds;

    int64_t remaining_for_bar = remaining_seconds;
    if (remaining_for_bar > window_seconds) {
        remaining_for_bar = window_seconds;
    }

    double remaining_pct = window_seconds > 0 ? (static_cast<double>(remaining_for_bar) * 100.0 / static_cast<double>(window_seconds)) : 0.0;
    if (remaining_pct < 0.0) remaining_pct = 0.0;
    if (remaining_pct > 100.0) remaining_pct = 100.0;

    double approaching_pct = 100.0 - remaining_pct;

    std::string suffix = format_duration_tight(remaining_seconds);
    const std::string label = "R:";

    int label_len = static_cast<int>(label.size());
    int suffix_len = static_cast<int>(suffix.size());
    int bar_width = terminal_width - (label_len + suffix_len + 3);
    if (bar_width < 1) {
        bar_width = 1;
        int max_suffix = terminal_width - (label_len + bar_width + 3);
        if (max_suffix < 0) max_suffix = 0;
        suffix = truncate_right(suffix, static_cast<size_t>(max_suffix));
        suffix_len = static_cast<int>(suffix.size());
    }

    int filled = static_cast<int>((remaining_pct / 100.0) * bar_width);
    int empty = bar_width - filled;
    if (filled < 0) filled = 0;
    if (filled > bar_width) filled = bar_width;
    if (empty < 0) empty = 0;

    bool use_utf8 = is_utf8_locale();
    std::string filled_char = use_utf8 ? "█" : "#";
    std::string empty_char = use_utf8 ? "░" : "-";

    std::string color = get_color_for_percentage(approaching_pct, use_colors);
    std::string reset = use_colors ? "\033[0m" : "";

    std::ostringstream bar;
    bar << label << "[" << color;
    for (int i = 0; i < filled; i++) {
        bar << filled_char;
    }
    for (int i = 0; i < empty; i++) {
        bar << empty_char;
    }
    bar << reset << "] ";
    bar << suffix;
    return bar.str();
}

// Function to make HTTP request with given auth header
struct RequestResult {
    CURLcode curl_code = CURLE_OK;
    long http_code = 0;
    std::string body;
    std::string curl_error;
};

static RequestResult make_request(const std::string& auth_header) {
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

// Check if response contains unauthorized error
bool is_unauthorized(const std::string& response) {
    return response.find("Unauthorized") != std::string::npos || 
           response.find("unauthorized") != std::string::npos;
}

// Extract token from API key (remove fw_api_ prefix if present)
std::string extract_token(const std::string& api_key) {
    if (api_key.substr(0, 7) == "fw_api_") {
        return api_key.substr(7);
    }
    return api_key;
}

// Try different authentication methods
enum class AuthMethod {
    BearerFullKey,
    BearerToken,
    XApiKey,
    AuthorizationRaw,
};

#ifdef GUI_MODE_ENABLED
// GUI presentation modes
enum class GUIMode {
    Standard,   // Full window (400x250)
    Compact,    // Compact window (300x150)
    Tiny,       // Minimal window (150x50)
    Bar,        // Horizontal bar (350x100) - thick bars
    Mini,       // Small with chunky bars (200x120)
    Wide,       // Ultra-wide thin (400x80) - large bars
    Gauge       // Circular gauge (280x280) - radial progress
};

// Structure to hold GUI state (defined here after AuthMethod)
struct GUIState {
    // GTK Widgets
    GtkWidget* window;
    GtkWidget* usage_progress;
    GtkWidget* reset_progress;
    GtkWidget* usage_label;
    GtkWidget* reset_label;
    GtkWidget* timestamp_label;
    GtkWidget* gauge_drawing_area;  // For circular gauge mode

    // System Tray
    AppIndicator* indicator;
    GtkWidget* tray_menu;

    // Application State
    std::string api_key;
    std::string token;
    std::string log_file;
    bool logging_enabled;
    int refresh_interval;
    std::optional<AuthMethod> preferred_auth_method;

    // Current Data
    QuotaData current_quota;
    std::string event_type;

    // Update Timer
    guint timer_id;

    // Window State
    int window_x;
    int window_y;
    bool window_visible;
    GUIMode gui_mode;

    // Constructor with defaults
    GUIState() : window(nullptr), usage_progress(nullptr), reset_progress(nullptr),
                 usage_label(nullptr), reset_label(nullptr), timestamp_label(nullptr),
                 gauge_drawing_area(nullptr), indicator(nullptr), tray_menu(nullptr),
                 logging_enabled(true), refresh_interval(60),
                 timer_id(0), window_x(-1), window_y(-1), window_visible(true),
                 gui_mode(GUIMode::Standard) {
        current_quota.used = 0.0;
        current_quota.percentage = 0.0;
        current_quota.reset_time = "";
        current_quota.timestamp = 0;
    }
};
#endif

static std::string build_auth_header(AuthMethod method, const std::string& api_key, const std::string& token) {
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

static bool is_http_success(long code) {
    return code >= 200 && code < 300;
}

static bool is_auth_failure(const RequestResult& r) {
    if (r.http_code == 401) {
        return true;
    }
    return is_unauthorized(r.body);
}

static std::string truncate_for_display(const std::string& s, size_t max_len) {
    if (s.size() <= max_len) {
        return s;
    }
    return s.substr(0, max_len) + "...";
}

static RequestResult try_auth_methods(const std::string& api_key,
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

// Format ISO 8601 timestamp to readable format in local timezone
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

// Get current timestamp as string
std::string get_timestamp_string() {
    time_t now = time(nullptr);
    struct tm* local_tm = localtime(&now);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local_tm);
    return std::string(buffer);
}

// Read last quota entry from log file
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

// Detect if quota was reset
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

// Write log entry
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

#ifdef GUI_MODE_ENABLED
// Forward declaration for GUI mode
static int run_gui_mode(const std::string& api_key, int refresh_interval,
                       const std::string& log_file, bool logging_enabled,
                       GUIMode gui_mode, int* argc, char*** argv);
#endif

// Print usage information
void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [OPTIONS] [API_KEY]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --gui, -g            Launch GUI mode with system tray icon (standard size)" << std::endl;
    std::cerr << "  --gui-compact        Launch GUI in compact mode (300x150 window)" << std::endl;
    std::cerr << "  --gui-tiny           Launch GUI in tiny mode (150x80 window)" << std::endl;
    std::cerr << "  --refresh <seconds>  Refresh continuously every N seconds (default/min: 60)" << std::endl;
    std::cerr << "  -1                   Single run (no refresh loop)" << std::endl;
    std::cerr << "  --text              Pure text output (no progress bar)" << std::endl;
    std::cerr << "  --log <file>        Log quota changes to CSV file (default: ./show_quota.log)" << std::endl;
    std::cerr << "  --no-log            Disable logging" << std::endl;
    std::cerr << "  --compact           Compact bar layout for ~40-column terminals" << std::endl;
    std::cerr << "  --tiny              Extra small single-line output: XX%" << std::endl;
    std::cerr << "  --help              Show this help message" << std::endl;
    std::cerr << std::endl;
    std::cerr << "API Key:" << std::endl;
    std::cerr << "  Can be passed as argument or set FIRMWARE_API_KEY environment variable" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Logging:" << std::endl;
    std::cerr << "  Logs are written in CSV format with columns:" << std::endl;
    std::cerr << "  Timestamp, Used, Percentage, Reset, Event" << std::endl;
    std::cerr << "  Events: FIRST_RUN, UPDATE, QUOTA_RESET, POSSIBLE_RESET, HIGH_USAGE" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Examples:" << std::endl;
    std::cerr << "  " << program_name << " --gui fw_api_xxx" << std::endl;
    std::cerr << "  " << program_name << " --gui-compact fw_api_xxx" << std::endl;
    std::cerr << "  " << program_name << " --gui-tiny fw_api_xxx" << std::endl;
    std::cerr << "  " << program_name << " fw_api_xxx" << std::endl;
    std::cerr << "  " << program_name << " --refresh 60 fw_api_xxx" << std::endl;
    std::cerr << "  " << program_name << " -1 fw_api_xxx" << std::endl;
    std::cerr << "  " << program_name << " --text --refresh 60 --log quota.csv" << std::endl;
    std::cerr << "  " << program_name << " --no-log --refresh 60" << std::endl;
    std::cerr << "  " << program_name << " --log /var/log/firmware_quota.csv" << std::endl;
    std::cerr << "  " << program_name << " --compact --refresh 60" << std::endl;
    std::cerr << "  " << program_name << " --tiny --refresh 60" << std::endl;
}

// Fetch and display quota information
int fetch_and_display_quota(const std::string& api_key, const std::string& token, 
                              bool text_mode, bool compact_mode, bool tiny_mode, bool use_colors, int terminal_width,
                              const std::string& log_file,
                              std::optional<AuthMethod>& preferred_auth_method,
                              bool truncate_error_body) {
    // Try different auth methods
    std::optional<AuthMethod> used_method;
    RequestResult result = try_auth_methods(api_key, token, preferred_auth_method, &used_method);

    if (result.curl_code != CURLE_OK) {
        std::cerr << "Request failed: " << curl_easy_strerror(result.curl_code);
        if (!result.curl_error.empty()) {
            std::cerr << " (" << result.curl_error << ")";
        }
        std::cerr << std::endl;
        return 1;
    }

    if (!is_http_success(result.http_code)) {
        std::cerr << "HTTP error: " << result.http_code << std::endl;
        if (!result.body.empty()) {
            std::cerr << (truncate_error_body ? truncate_for_display(result.body, 300) : result.body) << std::endl;
        }
        return 1;
    }

    if (is_auth_failure(result)) {
        std::cerr << "Error: Unauthorized after trying all auth methods." << std::endl;
        if (!result.body.empty()) {
            std::cerr << (truncate_error_body ? truncate_for_display(result.body, 300) : result.body) << std::endl;
        }
        return 1;
    }
    
    // Parse JSON response
    json j;
    try {
        j = json::parse(result.body);
    } catch (const json::parse_error& e) {
        std::cerr << "Failed to parse response. Raw response:" << std::endl;
        std::cerr << (truncate_error_body ? truncate_for_display(result.body, 300) : result.body) << std::endl;
        return 1;
    }
    
    // Extract used and reset fields
    if (!j.contains("used") || j["used"].is_null()) {
        std::cerr << "Failed to parse response. Raw response:" << std::endl;
        std::cerr << (truncate_error_body ? truncate_for_display(result.body, 300) : result.body) << std::endl;
        return 1;
    }
    
    double used = j["used"];
    std::string reset = j.contains("reset") && !j["reset"].is_null() ? j["reset"].get<std::string>() : "";
    
    // Calculate percentage
    double percentage = used * 100.0;
    
    // Prepare current quota data
    QuotaData current_data;
    current_data.used = used;
    current_data.percentage = percentage;
    current_data.reset_time = reset.empty() ? "N/A" : reset;
    current_data.timestamp = time(nullptr);
    
    // Handle logging if enabled
    std::string event = "UPDATE";
    if (!log_file.empty()) {
        QuotaData previous_data = read_last_log_entry(log_file);
        event = detect_event(current_data, previous_data);
        write_log_entry(log_file, current_data, event);
        
        // Show event notification for important changes
        if (!compact_mode && !tiny_mode && (event == "QUOTA_RESET" || event == "POSSIBLE_RESET")) {
            if (use_colors) {
                std::cout << "\033[33m"; // Yellow
            }
            std::cout << "*** " << event << " DETECTED ***" << std::endl;
            if (use_colors) {
                std::cout << "\033[0m"; // Reset
            }
        }
    }

    if (tiny_mode) {
        std::cout << render_tiny_usage_line(percentage, use_colors) << std::endl;
        return 0;
    }
    
    // Display results
    if (!compact_mode) {
        std::cout << "Firmware API Quota Details:" << std::endl;
        std::cout << "==========================" << std::endl;
    }
    
    if (text_mode) {
        // Pure text output
        std::cout << std::fixed << std::setprecision(2);
        if (!compact_mode) {
            std::cout << "Used: " << percentage << "% (" << used << ")" << std::endl;
        } else {
            std::cout << std::fixed << std::setprecision(0);
            std::cout << "U: " << percentage << "%" << std::endl;
        }
    } else {
        // Progress bar output
        if (compact_mode) {
            std::cout << render_progress_bar_compact(percentage, terminal_width, use_colors) << std::endl;
        } else {
            std::cout << render_progress_bar(percentage, terminal_width, use_colors) << std::endl;
        }
    }

    if (!reset.empty()) {
        time_t reset_utc = 0;
        bool parsed = parse_iso8601_utc_to_time_t(reset, &reset_utc);
        if (parsed) {
            if (!text_mode) {
                if (compact_mode) {
                    std::cout << render_reset_time_bar_compact(reset_utc, terminal_width, use_colors) << std::endl;
                } else {
                    std::cout << render_reset_time_bar(reset_utc, terminal_width, use_colors) << std::endl;
                }
            } else {
                time_t now = time(nullptr);
                int64_t remaining_seconds = static_cast<int64_t>(difftime(reset_utc, now));
                if (remaining_seconds < 0) {
                    remaining_seconds = 0;
                }
                if (!compact_mode) {
                    std::cout << "Reset in: " << format_duration_compact(remaining_seconds) << " (of 5h)" << std::endl;
                } else {
                    std::cout << "R: " << format_duration_tight(remaining_seconds) << std::endl;
                }
            }

            std::string reset_readable = format_timestamp(reset);
            if (!compact_mode) {
                std::cout << "Resets at: " << reset_readable << std::endl;
            }
        } else {
            std::string reset_readable = format_timestamp(reset);
            if (!compact_mode) {
                std::cout << "Reset: " << reset_readable << std::endl;
            } else {
                std::cout << "R: " << truncate_right(reset_readable, static_cast<size_t>(terminal_width)) << std::endl;
            }
        }
    } else {
        if (!compact_mode) {
            std::cout << "Reset: No active window (quota not used recently)" << std::endl;
        } else {
            std::cout << "R: none" << std::endl;
        }
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    std::string api_key;
    int refresh_interval = 60;
    bool text_mode = false;
    bool compact_mode = false;
    bool tiny_mode = false;
    bool gui_mode = false;
    std::string log_file = "show_quota.log";
    bool logging_enabled = true;
#ifdef GUI_MODE_ENABLED
    GUIMode selected_gui_mode = GUIMode::Standard;
#endif

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--gui" || arg == "-g") {
            gui_mode = true;
#ifdef GUI_MODE_ENABLED
            selected_gui_mode = GUIMode::Standard;
#endif
        } else if (arg == "--gui-compact") {
            gui_mode = true;
#ifdef GUI_MODE_ENABLED
            selected_gui_mode = GUIMode::Compact;
#endif
        } else if (arg == "--gui-tiny") {
            gui_mode = true;
#ifdef GUI_MODE_ENABLED
            selected_gui_mode = GUIMode::Tiny;
#endif
        } else if (arg == "-1") {
            refresh_interval = 0;
        } else if (arg == "--refresh" || arg == "-r") {
            if (i + 1 < argc) {
                refresh_interval = std::atoi(argv[++i]);
                if (refresh_interval < 60) {
                    refresh_interval = 60; // Enforce minimum of 60 seconds
                }
            } else {
                refresh_interval = 60; // Default to 60 seconds
            }
        } else if (arg == "--text" || arg == "-t") {
            text_mode = true;
        } else if (arg == "--compact") {
            compact_mode = true;
            tiny_mode = false;
        } else if (arg == "--tiny") {
            tiny_mode = true;
            compact_mode = false;
        } else if (arg == "--log" || arg == "-l") {
            if (i + 1 < argc) {
                log_file = argv[++i];
                logging_enabled = true;
            } else {
                std::cerr << "Error: --log requires a file path" << std::endl;
                print_usage(argv[0]);
                return 1;
            }
        } else if (arg == "--no-log") {
            logging_enabled = false;
        } else if (arg[0] != '-') {
            // Assume it's the API key
            api_key = arg;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    if (compact_mode || tiny_mode) {
        std::atexit(show_cursor_if_hidden);
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = handle_term_signal;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
        hide_cursor_if_tty();
    }
    
    // Get API key from environment variable if not provided
    if (api_key.empty()) {
        const char* env_key = std::getenv("FIRMWARE_API_KEY");
        if (env_key) {
            api_key = env_key;
        }
    }
    
    // Check if API key is provided
    if (api_key.empty()) {
        std::cerr << "Error: API key not provided." << std::endl;
        std::cerr << std::endl;
        print_usage(argv[0]);
        return 1;
    }
    
    // Extract token
    std::string token = extract_token(api_key);

    // Initialize curl globally
    curl_global_init(CURL_GLOBAL_DEFAULT);

    int result = 0;

    // GUI mode dispatcher
    if (gui_mode) {
#ifdef GUI_MODE_ENABLED
        result = run_gui_mode(api_key, refresh_interval, log_file, logging_enabled, selected_gui_mode, &argc, &argv);
        curl_global_cleanup();
        return result;
#else
        std::cerr << "Error: GUI mode not compiled. Rebuild with GTK3 support." << std::endl;
        std::cerr << "Install dependencies: sudo apt-get install libgtk-3-dev libayatana-appindicator3-dev libnotify-dev" << std::endl;
        std::cerr << "Then run: make clean && make" << std::endl;
        curl_global_cleanup();
        return 1;
#endif
    }

    // Terminal mode (existing code)
    std::optional<AuthMethod> preferred_auth_method;

    if (refresh_interval > 0) {
        // Continuous refresh mode
        while (true) {
            int terminal_width = get_terminal_width();
            bool use_colors = isatty(STDOUT_FILENO);

            // Clear screen (ANSI escape code)
            if (use_colors) {
                std::cout << "\033[2J\033[H"; // Clear screen and move cursor to home
                std::cout.flush();
            }
            
            result = fetch_and_display_quota(api_key,
                                             token,
                                             text_mode,
                                             compact_mode,
                                             tiny_mode,
                                             use_colors,
                                             terminal_width,
                                             logging_enabled ? log_file : std::string(),
                                             preferred_auth_method,
                                             true);
            
            if (result != 0) {
                // Error occurred, but continue trying
                std::cerr << std::endl << "Will retry in " << refresh_interval << " seconds..." << std::endl;
            }
            
            // Show next refresh time
            if (!compact_mode && !tiny_mode) {
                std::cout << std::endl << "Refreshing every " << refresh_interval << " seconds (Ctrl+C to stop)..." << std::endl;
            }
            std::cout.flush();
            
            // Sleep for specified interval
            sleep(refresh_interval);
        }
    } else {
        // Single run mode
        int terminal_width = get_terminal_width();
        bool use_colors = isatty(STDOUT_FILENO);
        result = fetch_and_display_quota(api_key,
                                         token,
                                         text_mode,
                                         compact_mode,
                                         tiny_mode,
                                         use_colors,
                                         terminal_width,
                                         logging_enabled ? log_file : std::string(),
                                         preferred_auth_method,
                                         false);
    }

    // Cleanup curl
    curl_global_cleanup();

    return result;
}

#ifdef GUI_MODE_ENABLED
// ============================================================================
// GUI Mode Implementation
// ============================================================================

// Forward declarations
static GtkWidget* create_main_window(GUIState* state);
static void update_gui_widgets(GUIState* state, const QuotaData* data);
static void update_tray_display(GUIState* state, const QuotaData* data);
static void show_desktop_notification(const std::string& event, double percentage);
static void save_gui_state(const GUIState* state);
static gboolean on_timer_update(gpointer user_data);

// Apply CSS styling for color-coded progress bars
static void apply_css_styling() {
    GtkCssProvider* provider = gtk_css_provider_new();
    const char* css =
        ".quota-green progressbar progress { "
        "    background-color: #4caf50; "
        "    background-image: none; "
        "} "
        ".quota-yellow progressbar progress { "
        "    background-color: #ff9800; "
        "    background-image: none; "
        "} "
        ".quota-red progressbar progress { "
        "    background-color: #f44336; "
        "    background-image: none; "
        "}";

    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

// Update widget colors based on percentage threshold
static void update_widget_colors(GtkWidget* progress, double percentage) {
    GtkStyleContext* context = gtk_widget_get_style_context(progress);

    // Remove old classes
    gtk_style_context_remove_class(context, "quota-green");
    gtk_style_context_remove_class(context, "quota-yellow");
    gtk_style_context_remove_class(context, "quota-red");

    // Add new class based on threshold (matching terminal thresholds)
    if (percentage < 50.0) {
        gtk_style_context_add_class(context, "quota-green");
    } else if (percentage < 80.0) {
        gtk_style_context_add_class(context, "quota-yellow");
    } else {
        gtk_style_context_add_class(context, "quota-red");
    }
}

// Window delete event handler - hide instead of destroy
static gboolean on_window_delete(GtkWidget* widget, GdkEvent* event, gpointer user_data) {
    (void)event;
    GUIState* state = (GUIState*)user_data;
    gtk_widget_hide(widget);
    state->window_visible = false;
    return TRUE;  // Prevent default destroy
}

// Window configure event handler - track position
static gboolean on_window_configure(GtkWidget* widget, GdkEventConfigure* event, gpointer user_data) {
    (void)widget;
    GUIState* state = (GUIState*)user_data;
    state->window_x = event->x;
    state->window_y = event->y;
    return FALSE;
}

// Tray menu callbacks
static void on_tray_show(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    GUIState* state = (GUIState*)user_data;
    gtk_widget_show_all(state->window);
    state->window_visible = true;
}

static void on_tray_hide(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    GUIState* state = (GUIState*)user_data;
    gtk_widget_hide(state->window);
    state->window_visible = false;
}

static void on_tray_quit(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    (void)user_data;
    gtk_main_quit();
}

// Recreate window with new GUI mode
static void recreate_window_with_mode(GUIState* state, GUIMode new_mode) {
    if (state->gui_mode == new_mode) {
        return;  // Already in this mode
    }

    // Store current visibility
    bool was_visible = state->window_visible;

    // Destroy old window
    if (state->window) {
        gtk_widget_destroy(state->window);
    }

    // Update mode
    state->gui_mode = new_mode;

    // Create new window with new mode
    state->window = create_main_window(state);

    // Restore visibility
    if (was_visible) {
        gtk_widget_show_all(state->window);
        state->window_visible = true;
    } else {
        gtk_widget_realize(state->window);
        state->window_visible = false;
    }

    // Update with current data if available
    if (state->current_quota.timestamp > 0) {
        update_gui_widgets(state, &state->current_quota);
    }

    // Save new mode preference
    save_gui_state(state);
}

// Mode change callbacks
static void on_mode_standard(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    GUIState* state = (GUIState*)user_data;
    recreate_window_with_mode(state, GUIMode::Standard);
}

static void on_mode_compact(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    GUIState* state = (GUIState*)user_data;
    recreate_window_with_mode(state, GUIMode::Compact);
}

static void on_mode_tiny(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    GUIState* state = (GUIState*)user_data;
    recreate_window_with_mode(state, GUIMode::Tiny);
}

static void on_mode_bar(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    GUIState* state = (GUIState*)user_data;
    recreate_window_with_mode(state, GUIMode::Bar);
}

static void on_mode_mini(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    GUIState* state = (GUIState*)user_data;
    recreate_window_with_mode(state, GUIMode::Mini);
}

static void on_mode_wide(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    GUIState* state = (GUIState*)user_data;
    recreate_window_with_mode(state, GUIMode::Wide);
}

static void on_mode_gauge(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    GUIState* state = (GUIState*)user_data;
    recreate_window_with_mode(state, GUIMode::Gauge);
}

// Cairo drawing callback for circular gauge
static gboolean on_gauge_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    GUIState* state = (GUIState*)user_data;

    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    double center_x = width / 2.0;
    double center_y = height / 2.0;
    double radius = (width < height ? width : height) / 2.0 - 20;

    double percentage = state->current_quota.percentage;

    // Background circle
    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
    cairo_set_line_width(cr, 20);
    cairo_arc(cr, center_x, center_y, radius, 0, 2 * M_PI);
    cairo_stroke(cr);

    // Foreground arc (progress)
    double angle = (percentage / 100.0) * 2 * M_PI;

    // Color based on percentage
    if (percentage < 50.0) {
        cairo_set_source_rgb(cr, 0.30, 0.69, 0.31);  // Green
    } else if (percentage < 80.0) {
        cairo_set_source_rgb(cr, 1.0, 0.60, 0.0);    // Orange
    } else {
        cairo_set_source_rgb(cr, 0.96, 0.28, 0.21);  // Red
    }

    cairo_set_line_width(cr, 20);
    cairo_arc(cr, center_x, center_y, radius, -M_PI/2, -M_PI/2 + angle);
    cairo_stroke(cr);

    // Draw percentage text in center
    char text[32];
    snprintf(text, sizeof(text), "%.1f%%", percentage);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 36);

    cairo_text_extents_t extents;
    cairo_text_extents(cr, text, &extents);

    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_move_to(cr, center_x - extents.width/2 - extents.x_bearing,
                     center_y - extents.height/2 - extents.y_bearing);
    cairo_show_text(cr, text);

    return FALSE;
}

// Create main window with widgets based on GUI mode
static GtkWidget* create_main_window(GUIState* state) {
    // Create window
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    // Configure window based on mode
    int width, height, border, spacing, bar_height;
    const char* title;
    bool show_frames = true;
    bool show_timestamp = true;
    bool show_reset = true;

    switch (state->gui_mode) {
        case GUIMode::Tiny:
            width = 150;
            height = 50;
            border = 5;
            spacing = 3;
            bar_height = 15;
            title = "Quota";
            show_frames = false;
            show_timestamp = false;
            show_reset = false;  // Tiny mode: only usage bar
            break;
        case GUIMode::Bar:
            width = 350;
            height = 100;
            border = 8;
            spacing = 5;
            bar_height = 30;  // Thick bars!
            title = "Quota";
            show_frames = false;
            show_timestamp = false;
            break;
        case GUIMode::Mini:
            width = 200;
            height = 120;
            border = 6;
            spacing = 4;
            bar_height = 25;  // Chunky bars
            title = "Quota";
            show_frames = false;
            show_timestamp = false;
            break;
        case GUIMode::Wide:
            width = 400;
            height = 80;
            border = 6;
            spacing = 4;
            bar_height = 28;  // Large bars in wide format
            title = "Firmware Quota";
            show_frames = false;
            show_timestamp = false;
            break;
        case GUIMode::Gauge:
            width = 280;
            height = 280;
            border = 10;
            spacing = 5;
            bar_height = 0;  // No bars, using circular gauge
            title = "Quota";
            show_frames = false;
            show_timestamp = false;
            show_reset = false;  // Circular gauge shows only usage
            break;
        case GUIMode::Compact:
            width = 300;
            height = 150;
            border = 8;
            spacing = 5;
            bar_height = 20;
            title = "Firmware Quota";
            show_frames = false;
            show_timestamp = false;
            break;
        case GUIMode::Standard:
        default:
            width = 400;
            height = 250;
            border = 10;
            spacing = 10;
            bar_height = 30;
            title = "Firmware API Quota Monitor";
            break;
    }

    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_window_set_default_size(GTK_WINDOW(window), width, height);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(window), border);

    // Create vertical box layout
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // Usage section
    if (show_frames) {
        GtkWidget* usage_frame = gtk_frame_new("Quota Usage");
        gtk_box_pack_start(GTK_BOX(vbox), usage_frame, FALSE, FALSE, 0);
        GtkWidget* usage_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(usage_vbox), 10);
        gtk_container_add(GTK_CONTAINER(usage_frame), usage_vbox);

        state->usage_progress = gtk_progress_bar_new();
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(state->usage_progress), FALSE);
        gtk_widget_set_size_request(state->usage_progress, -1, bar_height);
        gtk_box_pack_start(GTK_BOX(usage_vbox), state->usage_progress, FALSE, FALSE, 0);

        state->usage_label = gtk_label_new("Initializing...");
        gtk_label_set_xalign(GTK_LABEL(state->usage_label), 0.0);
        gtk_box_pack_start(GTK_BOX(usage_vbox), state->usage_label, FALSE, FALSE, 0);
    } else {
        // Compact/Tiny: no frame
        state->usage_progress = gtk_progress_bar_new();
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(state->usage_progress), FALSE);
        gtk_widget_set_size_request(state->usage_progress, -1, bar_height);
        gtk_box_pack_start(GTK_BOX(vbox), state->usage_progress, FALSE, FALSE, 0);

        state->usage_label = gtk_label_new("Initializing...");
        gtk_label_set_xalign(GTK_LABEL(state->usage_label), 0.0);
        gtk_box_pack_start(GTK_BOX(vbox), state->usage_label, FALSE, FALSE, 0);
    }

    // Reset countdown section (skip in Tiny mode)
    if (show_reset) {
        if (show_frames) {
            GtkWidget* reset_frame = gtk_frame_new("Reset Countdown");
            gtk_box_pack_start(GTK_BOX(vbox), reset_frame, FALSE, FALSE, 0);
            GtkWidget* reset_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
            gtk_container_set_border_width(GTK_CONTAINER(reset_vbox), 10);
            gtk_container_add(GTK_CONTAINER(reset_frame), reset_vbox);

            state->reset_progress = gtk_progress_bar_new();
            gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(state->reset_progress), FALSE);
            gtk_widget_set_size_request(state->reset_progress, -1, bar_height);
            gtk_box_pack_start(GTK_BOX(reset_vbox), state->reset_progress, FALSE, FALSE, 0);

            state->reset_label = gtk_label_new("Waiting for data...");
            gtk_label_set_xalign(GTK_LABEL(state->reset_label), 0.0);
            gtk_box_pack_start(GTK_BOX(reset_vbox), state->reset_label, FALSE, FALSE, 0);
        } else {
            // Compact: no frame
            state->reset_progress = gtk_progress_bar_new();
            gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(state->reset_progress), FALSE);
            gtk_widget_set_size_request(state->reset_progress, -1, bar_height);
            gtk_box_pack_start(GTK_BOX(vbox), state->reset_progress, FALSE, FALSE, 0);

            state->reset_label = gtk_label_new("Waiting...");
            gtk_label_set_xalign(GTK_LABEL(state->reset_label), 0.0);
            gtk_box_pack_start(GTK_BOX(vbox), state->reset_label, FALSE, FALSE, 0);
        }
    } else {
        // Tiny mode: no reset section at all
        state->reset_progress = nullptr;
        state->reset_label = nullptr;
    }

    // Timestamp label (only in Standard mode)
    if (show_timestamp) {
        state->timestamp_label = gtk_label_new("");
        gtk_label_set_selectable(GTK_LABEL(state->timestamp_label), TRUE);
        gtk_label_set_xalign(GTK_LABEL(state->timestamp_label), 0.0);
        gtk_box_pack_start(GTK_BOX(vbox), state->timestamp_label, FALSE, FALSE, 5);
    } else {
        state->timestamp_label = nullptr;
    }

    // Connect signals
    g_signal_connect(window, "delete-event", G_CALLBACK(on_window_delete), state);
    g_signal_connect(window, "configure-event", G_CALLBACK(on_window_configure), state);

    return window;
}

// Create system tray icon with context menu
static AppIndicator* create_system_tray(GUIState* state) {
    // Try to use custom Firmware icon, fallback to default
    const char* icon_path = "firmware-icon";  // Will look for firmware-icon.svg in icon theme paths

    AppIndicator* indicator = app_indicator_new(
        "firmware-quota-indicator",
        icon_path,
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS
    );

    // Set icon theme path to current directory so it finds firmware-icon.svg
    app_indicator_set_icon_theme_path(indicator, ".");

    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_title(indicator, "Firmware Quota: Initializing...");

    // Create context menu
    GtkWidget* menu = gtk_menu_new();

    GtkWidget* show_item = gtk_menu_item_new_with_label("Show Window");
    g_signal_connect(show_item, "activate", G_CALLBACK(on_tray_show), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), show_item);

    GtkWidget* hide_item = gtk_menu_item_new_with_label("Hide Window");
    g_signal_connect(hide_item, "activate", G_CALLBACK(on_tray_hide), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), hide_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    // Window Style submenu
    GtkWidget* style_item = gtk_menu_item_new_with_label("Window Style");
    GtkWidget* style_submenu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(style_item), style_submenu);

    GtkWidget* standard_item = gtk_menu_item_new_with_label("Standard (400×250)");
    g_signal_connect(standard_item, "activate", G_CALLBACK(on_mode_standard), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(style_submenu), standard_item);

    GtkWidget* compact_item = gtk_menu_item_new_with_label("Compact (300×150)");
    g_signal_connect(compact_item, "activate", G_CALLBACK(on_mode_compact), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(style_submenu), compact_item);

    GtkWidget* bar_item = gtk_menu_item_new_with_label("Bar (350×100) - Thick bars");
    g_signal_connect(bar_item, "activate", G_CALLBACK(on_mode_bar), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(style_submenu), bar_item);

    GtkWidget* mini_item = gtk_menu_item_new_with_label("Mini (200×120) - Chunky");
    g_signal_connect(mini_item, "activate", G_CALLBACK(on_mode_mini), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(style_submenu), mini_item);

    GtkWidget* wide_item = gtk_menu_item_new_with_label("Wide (400×80) - Large bars");
    g_signal_connect(wide_item, "activate", G_CALLBACK(on_mode_wide), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(style_submenu), wide_item);

    GtkWidget* tiny_item = gtk_menu_item_new_with_label("Tiny (150×50) - Minimal");
    g_signal_connect(tiny_item, "activate", G_CALLBACK(on_mode_tiny), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(style_submenu), tiny_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), style_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    GtkWidget* quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_tray_quit), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

    gtk_widget_show_all(menu);
    app_indicator_set_menu(indicator, GTK_MENU(menu));

    state->tray_menu = menu;

    return indicator;
}

// Update tray icon color based on quota
static void update_tray_icon_color(AppIndicator* indicator, double percentage) {
    // Keep the Firmware logo icon constant
    // Color coding is shown in the progress bars instead
    (void)indicator;
    (void)percentage;
    // Icon stays as "firmware-icon" always
}

// Show desktop notification for important events
static void show_desktop_notification(const std::string& event, double percentage) {
    const char* title;
    char body[256];
    NotifyUrgency urgency;

    if (event == "QUOTA_RESET") {
        title = "Quota Reset Detected";
        snprintf(body, sizeof(body),
                 "Your quota has been reset. Current usage: %.2f%%",
                 percentage);
        urgency = NOTIFY_URGENCY_NORMAL;
    } else if (event == "HIGH_USAGE") {
        title = "High Quota Usage Warning";
        snprintf(body, sizeof(body),
                 "Your quota usage has increased significantly to %.2f%%",
                 percentage);
        urgency = NOTIFY_URGENCY_CRITICAL;
    } else {
        return;  // Don't notify for other events
    }

    NotifyNotification* notification = notify_notification_new(
        title,
        body,
        event == "QUOTA_RESET" ? "dialog-information" : "dialog-warning"
    );

    notify_notification_set_urgency(notification, urgency);
    notify_notification_set_timeout(notification, 10000);  // 10 seconds
    notify_notification_show(notification, NULL);
    g_object_unref(notification);
}

// Update GUI widgets with quota data
static void update_gui_widgets(GUIState* state, const QuotaData* data) {
    // Update usage progress bar
    double fraction = data->percentage / 100.0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->usage_progress), fraction);

    // Update usage label
    char usage_text[128];
    snprintf(usage_text, sizeof(usage_text), "%.2f%% (%.4f used)",
             data->percentage, data->used);
    gtk_label_set_text(GTK_LABEL(state->usage_label), usage_text);

    // Apply color coding
    update_widget_colors(state->usage_progress, data->percentage);

    // Update reset countdown (only if exists - not in Tiny mode)
    if (state->reset_progress != nullptr && state->reset_label != nullptr) {
        if (data->reset_time != "N/A" && !data->reset_time.empty()) {
            time_t reset_utc;
            if (parse_iso8601_utc_to_time_t(data->reset_time, &reset_utc)) {
                time_t now = time(nullptr);
                int64_t remaining = static_cast<int64_t>(difftime(reset_utc, now));
                if (remaining < 0) remaining = 0;

                double remaining_pct = (double)remaining / (double)kQuotaWindowSeconds;
                if (remaining_pct > 1.0) remaining_pct = 1.0;

                gtk_progress_bar_set_fraction(
                    GTK_PROGRESS_BAR(state->reset_progress),
                    remaining_pct
                );

                std::string duration_str = format_duration_compact(remaining);
                char reset_text[128];
                snprintf(reset_text, sizeof(reset_text),
                         "%s left (of 5h)",
                         duration_str.c_str());
                gtk_label_set_text(GTK_LABEL(state->reset_label), reset_text);

                // Color based on approaching deadline (inverse logic)
                double approaching_pct = 100.0 - (remaining_pct * 100.0);
                update_widget_colors(state->reset_progress, approaching_pct);
            }
        } else {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->reset_progress), 0.0);
            gtk_label_set_text(GTK_LABEL(state->reset_label), "No active window");
        }
    }

    // Update timestamp (only if exists - not in Compact/Tiny modes)
    if (state->timestamp_label != nullptr) {
        std::string formatted_time = format_timestamp(data->reset_time);
        std::string current_time = get_timestamp_string();
        char timestamp_text[512];
        snprintf(timestamp_text, sizeof(timestamp_text),
                 "Last updated: %s\nResets at: %s",
                 current_time.c_str(),
                 formatted_time.c_str());
        gtk_label_set_text(GTK_LABEL(state->timestamp_label), timestamp_text);
    }

    // Store current data
    state->current_quota = *data;
}

// Update system tray display
static void update_tray_display(GUIState* state, const QuotaData* data) {
    // Update icon color
    update_tray_icon_color(state->indicator, data->percentage);

    // Update tooltip
    char tooltip[512];
    if (data->reset_time != "N/A" && !data->reset_time.empty()) {
        time_t reset_utc;
        if (parse_iso8601_utc_to_time_t(data->reset_time, &reset_utc)) {
            time_t now = time(nullptr);
            int64_t remaining = static_cast<int64_t>(difftime(reset_utc, now));
            std::string duration_str = format_duration_compact(remaining);
            snprintf(tooltip, sizeof(tooltip),
                     "Firmware Quota: %.1f%%\nReset: %s",
                     data->percentage,
                     duration_str.c_str());
        } else {
            snprintf(tooltip, sizeof(tooltip),
                     "Firmware Quota: %.1f%%",
                     data->percentage);
        }
    } else {
        snprintf(tooltip, sizeof(tooltip),
                 "Firmware Quota: %.1f%%\nNo active window",
                 data->percentage);
    }

    app_indicator_set_title(state->indicator, tooltip);
}

// Show error in GUI
static void show_error_in_gui(GUIState* state, const char* message) {
    gtk_label_set_text(GTK_LABEL(state->usage_label), "Error fetching data");
    gtk_label_set_text(GTK_LABEL(state->reset_label), message);

    // Show notification
    NotifyNotification* notification = notify_notification_new(
        "Firmware Quota Error",
        message,
        "dialog-error"
    );
    notify_notification_set_urgency(notification, NOTIFY_URGENCY_NORMAL);
    notify_notification_show(notification, NULL);
    g_object_unref(notification);
}

// Structure for passing data between threads
struct FetchThreadData {
    GUIState* state;
    RequestResult result;
    bool success;
    QuotaData quota_data;
    std::string event;
    std::optional<AuthMethod> used_method;
};

// Forward declaration
static gboolean on_fetch_complete(gpointer user_data);

// Background thread for fetching quota data
static void* fetch_quota_thread(void* arg) {
    FetchThreadData* data = (FetchThreadData*)arg;
    GUIState* state = data->state;

    // Perform HTTP request (reuse existing code)
    data->result = try_auth_methods(
        state->api_key,
        state->token,
        state->preferred_auth_method,
        &data->used_method
    );

    if (data->result.curl_code != CURLE_OK ||
        !is_http_success(data->result.http_code)) {
        data->success = false;
        // Schedule callback on main thread
        g_idle_add(on_fetch_complete, data);
        return nullptr;
    }

    // Parse JSON (reuse existing code)
    try {
        json j = json::parse(data->result.body);
        double used = j["used"];
        std::string reset = j.contains("reset") ? j["reset"].get<std::string>() : "";

        data->quota_data.used = used;
        data->quota_data.percentage = used * 100.0;
        data->quota_data.reset_time = reset.empty() ? "N/A" : reset;
        data->quota_data.timestamp = time(nullptr);

        // Detect event (reuse existing code)
        if (state->logging_enabled && !state->log_file.empty()) {
            QuotaData previous = read_last_log_entry(state->log_file);
            data->event = detect_event(data->quota_data, previous);
            write_log_entry(state->log_file, data->quota_data, data->event);
        }

        data->success = true;
    } catch (...) {
        data->success = false;
    }

    // Schedule callback on main thread
    g_idle_add(on_fetch_complete, data);
    return nullptr;
}

// GTK main thread callback after fetch completes
static gboolean on_fetch_complete(gpointer user_data) {
    FetchThreadData* data = (FetchThreadData*)user_data;

    if (data->success) {
        // Update preferred auth method if changed
        if (data->used_method.has_value()) {
            data->state->preferred_auth_method = data->used_method;
        }

        update_gui_widgets(data->state, &data->quota_data);
        update_tray_display(data->state, &data->quota_data);

        // Show notification for important events
        if (!data->event.empty() &&
            (data->event == "QUOTA_RESET" || data->event == "HIGH_USAGE")) {
            show_desktop_notification(data->event, data->quota_data.percentage);
        }

        data->state->event_type = data->event;
    } else {
        show_error_in_gui(data->state, "Failed to fetch quota data");
    }

    delete data;
    return G_SOURCE_REMOVE;
}

// Timer callback for periodic updates
static gboolean on_timer_update(gpointer user_data) {
    GUIState* state = (GUIState*)user_data;

    // Start fetch in background thread
    FetchThreadData* data = new FetchThreadData();
    data->state = state;
    data->success = false;

    pthread_t thread;
    if (pthread_create(&thread, nullptr, fetch_quota_thread, data) == 0) {
        pthread_detach(thread);
        // Thread will call g_idle_add when done
    } else {
        delete data;
    }

    return G_SOURCE_CONTINUE;  // Keep timer running
}

// Load GUI state from config file
static void load_gui_state(GUIState* state) {
    const char* home = getenv("HOME");
    if (!home) {
        state->window_x = -1;
        state->window_y = -1;
        state->window_visible = true;
        return;
    }

    std::string config_path = std::string(home) + "/.firmware_quota_gui.conf";
    std::ifstream file(config_path);
    if (!file.is_open()) {
        state->window_x = -1;
        state->window_y = -1;
        state->window_visible = true;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        if (key == "window_x") {
            state->window_x = std::atoi(value.c_str());
        } else if (key == "window_y") {
            state->window_y = std::atoi(value.c_str());
        } else if (key == "window_visible") {
            state->window_visible = (value == "1");
        } else if (key == "gui_mode") {
            int mode = std::atoi(value.c_str());
            if (mode >= 0 && mode <= 6) {
                state->gui_mode = static_cast<GUIMode>(mode);
            }
        }
    }
    file.close();
}

// Save GUI state to config file
static void save_gui_state(const GUIState* state) {
    const char* home = getenv("HOME");
    if (!home) return;

    std::string config_path = std::string(home) + "/.firmware_quota_gui.conf";
    std::ofstream file(config_path);
    if (!file.is_open()) return;

    file << "window_x=" << state->window_x << "\n";
    file << "window_y=" << state->window_y << "\n";
    file << "window_visible=" << (state->window_visible ? "1" : "0") << "\n";
    file << "gui_mode=" << static_cast<int>(state->gui_mode) << "\n";
    file.close();
}

// Restore window position and visibility
static void restore_window_position(GUIState* state) {
    if (state->window_x >= 0 && state->window_y >= 0) {
        gtk_window_move(GTK_WINDOW(state->window),
                       state->window_x,
                       state->window_y);
    }

    if (state->window_visible) {
        gtk_widget_show_all(state->window);
    } else {
        // Start hidden, only tray icon visible
        gtk_widget_realize(state->window);
    }
}

// GUI main function
static int run_gui_mode(const std::string& api_key,
                       int refresh_interval,
                       const std::string& log_file,
                       bool logging_enabled,
                       GUIMode gui_mode,
                       int* argc, char*** argv) {

    // Initialize GTK
    if (!gtk_init_check(argc, argv)) {
        std::cerr << "Failed to initialize GTK. Install libgtk-3-dev." << std::endl;
        return 1;
    }

    // Initialize libnotify
    notify_init("Firmware Quota");

    // Create GUI state
    GUIState* state = new GUIState();
    state->api_key = api_key;
    state->token = extract_token(api_key);
    state->log_file = log_file;
    state->logging_enabled = logging_enabled;
    state->refresh_interval = refresh_interval;
    state->gui_mode = gui_mode;

    // Load saved state (may override gui_mode if saved)
    load_gui_state(state);

    // If gui_mode was explicitly set on command line, override saved setting
    if (gui_mode != GUIMode::Standard) {
        state->gui_mode = gui_mode;
    }

    // Apply CSS styling
    apply_css_styling();

    // Create UI
    state->window = create_main_window(state);
    state->indicator = create_system_tray(state);

    // Restore window position and visibility
    restore_window_position(state);

    // Initial fetch
    on_timer_update(state);

    // Start update timer (convert seconds to milliseconds)
    state->timer_id = g_timeout_add(
        refresh_interval * 1000,
        on_timer_update,
        state
    );

    // Run GTK main loop
    gtk_main();

    // Cleanup
    save_gui_state(state);
    if (state->timer_id > 0) {
        g_source_remove(state->timer_id);
    }
    notify_uninit();
    delete state;

    return 0;
}

#endif  // GUI_MODE_ENABLED
