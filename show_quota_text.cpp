// =============================================================================
// show_quota_text.cpp - Text-only version of Firmware API Quota Viewer
// =============================================================================
// This version has NO GUI dependencies - only requires libcurl
// Build: g++ -std=c++17 -O2 -o show_quota_text show_quota_text.cpp quota_common.cpp -lcurl
// =============================================================================

#include "quota_common.h"
#include <sys/ioctl.h>
#include <clocale>
#include <signal.h>
#include <algorithm>

// ============================================================================
// Terminal UI - Cursor Control
// ============================================================================

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

// ============================================================================
// Terminal UI - Utilities
// ============================================================================

// Get terminal width
static int get_terminal_width() {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
        return w.ws_col;
    }
    return 80; // Default fallback
}

// Check if terminal supports UTF-8
static bool is_utf8_locale() {
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

// Truncate string from right
static std::string truncate_right(const std::string& s, size_t max_len) {
    if (s.size() <= max_len) {
        return s;
    }
    return s.substr(0, max_len);
}

// ============================================================================
// Terminal UI - Color Functions
// ============================================================================

// Get ANSI color code based on usage percentage
static std::string get_color_for_percentage(double percentage, bool use_colors) {
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

// ============================================================================
// Terminal UI - Progress Bar Rendering
// ============================================================================

// Render progress bar
static std::string render_progress_bar(double percentage, int terminal_width, bool use_colors) {
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

// ============================================================================
// Terminal UI - Reset Time Bar Rendering
// ============================================================================

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

// ============================================================================
// Main Logic
// ============================================================================

// Print usage information
static void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [OPTIONS] [API_KEY]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Text-only version - no GUI dependencies required." << std::endl;
    std::cerr << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --refresh <seconds>  Refresh continuously every N seconds (default: 15)" << std::endl;
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
    std::cerr << "  " << program_name << " fw_api_xxx" << std::endl;
    std::cerr << "  " << program_name << " --refresh 60 fw_api_xxx" << std::endl;
    std::cerr << "  " << program_name << " -1 fw_api_xxx" << std::endl;
    std::cerr << "  " << program_name << " --text --refresh 60 --log quota.csv" << std::endl;
    std::cerr << "  " << program_name << " --no-log --refresh 60" << std::endl;
    std::cerr << "  " << program_name << " --compact --refresh 60" << std::endl;
    std::cerr << "  " << program_name << " --tiny --refresh 60" << std::endl;
}

// Fetch and display quota information
static int fetch_and_display_quota(const std::string& api_key, const std::string& token, 
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
    int refresh_interval = 15;
    bool text_mode = false;
    bool compact_mode = false;
    bool tiny_mode = false;
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
            } else {
                refresh_interval = 15; // Default to 15 seconds
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
