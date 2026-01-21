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

using json = nlohmann::json;

static constexpr int kQuotaWindowSeconds = 5 * 60 * 60;

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

// Function to make HTTP request with given auth header
std::string make_request(const std::string& auth_header) {
    CURL* curl;
    CURLcode res;
    std::string response;
    
    curl = curl_easy_init();
    if (!curl) {
        return "";
    }
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, auth_header.c_str());
    
    curl_easy_setopt(curl, CURLOPT_URL, "https://app.firmware.ai/api/v1/quota");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    
    res = curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        return "";
    }
    
    return response;
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
std::string try_auth_methods(const std::string& api_key, const std::string& token) {
    std::string response;
    
    // Method 1: Bearer with full key
    response = make_request("Authorization: Bearer " + api_key);
    if (!response.empty() && !is_unauthorized(response)) {
        return response;
    }
    
    // Method 2: Bearer with extracted token
    response = make_request("Authorization: Bearer " + token);
    if (!response.empty() && !is_unauthorized(response)) {
        return response;
    }
    
    // Method 3: X-API-Key header
    response = make_request("X-API-Key: " + api_key);
    if (!response.empty() && !is_unauthorized(response)) {
        return response;
    }
    
    // Method 4: Authorization without Bearer
    response = make_request("Authorization: " + api_key);
    if (!response.empty() && !is_unauthorized(response)) {
        return response;
    }
    
    return response; // Return last response (will be unauthorized)
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

// Print usage information
void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [OPTIONS] [API_KEY]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --refresh <seconds>  Refresh continuously every N seconds (default: 60)" << std::endl;
    std::cerr << "  -1                   Single run (no refresh loop)" << std::endl;
    std::cerr << "  --text              Pure text output (no progress bar)" << std::endl;
    std::cerr << "  --log <file>        Log quota changes to CSV file (default: ./show_quota.log)" << std::endl;
    std::cerr << "  --no-log            Disable logging" << std::endl;
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
    std::cerr << "  " << program_name << " fw_api_xxx" << std::endl;
    std::cerr << "  " << program_name << " --refresh 30 fw_api_xxx" << std::endl;
    std::cerr << "  " << program_name << " -1 fw_api_xxx" << std::endl;
    std::cerr << "  " << program_name << " --text --refresh 60 --log quota.csv" << std::endl;
    std::cerr << "  " << program_name << " --no-log --refresh 10" << std::endl;
    std::cerr << "  " << program_name << " --log /var/log/firmware_quota.csv" << std::endl;
}

// Fetch and display quota information
int fetch_and_display_quota(const std::string& api_key, const std::string& token, 
                             bool text_mode, bool use_colors, int terminal_width,
                             const std::string& log_file) {
    // Try different auth methods
    std::string response = try_auth_methods(api_key, token);
    
    // Check if we got an unauthorized response
    if (response.empty() || is_unauthorized(response)) {
        std::cerr << "Error: Unauthorized after trying all auth methods. Raw response:" << std::endl;
        std::cerr << response << std::endl;
        return 1;
    }
    
    // Parse JSON response
    json j;
    try {
        j = json::parse(response);
    } catch (const json::parse_error& e) {
        std::cerr << "Failed to parse response. Raw response:" << std::endl;
        std::cerr << response << std::endl;
        return 1;
    }
    
    // Extract used and reset fields
    if (!j.contains("used") || j["used"].is_null()) {
        std::cerr << "Failed to parse response. Raw response:" << std::endl;
        std::cerr << response << std::endl;
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
        if (event == "QUOTA_RESET" || event == "POSSIBLE_RESET") {
            if (use_colors) {
                std::cout << "\033[33m"; // Yellow
            }
            std::cout << "*** " << event << " DETECTED ***" << std::endl;
            if (use_colors) {
                std::cout << "\033[0m"; // Reset
            }
        }
    }
    
    // Display results
    std::cout << "Firmware API Quota Details:" << std::endl;
    std::cout << "==========================" << std::endl;
    
    if (text_mode) {
        // Pure text output
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Used: " << percentage << "% (" << used << ")" << std::endl;
    } else {
        // Progress bar output
        std::cout << render_progress_bar(percentage, terminal_width, use_colors) << std::endl;
    }

    if (!reset.empty()) {
        time_t reset_utc = 0;
        bool parsed = parse_iso8601_utc_to_time_t(reset, &reset_utc);
        if (parsed) {
            if (!text_mode) {
                std::cout << render_reset_time_bar(reset_utc, terminal_width, use_colors) << std::endl;
            } else {
                time_t now = time(nullptr);
                int64_t remaining_seconds = static_cast<int64_t>(difftime(reset_utc, now));
                if (remaining_seconds < 0) {
                    remaining_seconds = 0;
                }
                std::cout << "Reset in: " << format_duration_compact(remaining_seconds) << " (of 5h)" << std::endl;
            }

            std::string reset_readable = format_timestamp(reset);
            std::cout << "Resets at: " << reset_readable << std::endl;
        } else {
            std::string reset_readable = format_timestamp(reset);
            std::cout << "Reset: " << reset_readable << std::endl;
        }
    } else {
        std::cout << "Reset: No active window (quota not used recently)" << std::endl;
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    std::string api_key;
    int refresh_interval = 60;
    bool text_mode = false;
    std::string log_file = "show_quota.log";
    bool logging_enabled = true;
    
    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-1") {
            refresh_interval = 0;
        } else if (arg == "--refresh" || arg == "-r") {
            if (i + 1 < argc) {
                refresh_interval = std::atoi(argv[++i]);
                if (refresh_interval <= 0) {
                    refresh_interval = 60; // Default to 60 seconds
                }
            } else {
                refresh_interval = 60; // Default to 60 seconds
            }
        } else if (arg == "--text" || arg == "-t") {
            text_mode = true;
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
    
    // Get terminal properties
    int terminal_width = get_terminal_width();
    bool use_colors = isatty(STDOUT_FILENO);
    
    int result = 0;

    if (refresh_interval > 0) {
        // Continuous refresh mode
        while (true) {
            // Clear screen (ANSI escape code)
            if (use_colors) {
                std::cout << "\033[2J\033[H"; // Clear screen and move cursor to home
                std::cout.flush();
            }
            
            result = fetch_and_display_quota(api_key, token, text_mode, use_colors, terminal_width, logging_enabled ? log_file : std::string());
            
            if (result != 0) {
                // Error occurred, but continue trying
                std::cerr << std::endl << "Will retry in " << refresh_interval << " seconds..." << std::endl;
            }
            
            // Show next refresh time
            std::cout << std::endl << "Refreshing every " << refresh_interval << " seconds (Ctrl+C to stop)..." << std::endl;
            std::cout.flush();
            
            // Sleep for specified interval
            sleep(refresh_interval);
        }
    } else {
        // Single run mode
        result = fetch_and_display_quota(api_key, token, text_mode, use_colors, terminal_width, logging_enabled ? log_file : std::string());
    }
    
    // Cleanup curl
    curl_global_cleanup();
    
    return result;
}
