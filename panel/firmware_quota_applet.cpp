// Firmware Quota MATE Panel Applet
//
// Build (from repo root):
//   make panel-applet

// Note: compile with `pkg-config --cflags libmatepanelapplet-4.0`.
#include <mate-panel-applet.h>

#include <pthread.h>

#include <glib.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>

#include <atomic>
#include <algorithm>
#include <array>
#include <vector>

#include <cstdio>
#include <ctime>

#include <cstdarg>

#include <pwd.h>
#include <sys/types.h>

#include <gtk/gtkx.h>

#include <syslog.h>

#include "../quota_common.h"

static constexpr const char* kFactoryId = "FirmwareQuotaAppletFactory";
static constexpr const char* kAppletId = "FirmwareQuotaApplet";

static constexpr int kAppletDefaultWidthPx = 120;
static constexpr int kAppletMinWidthPx = 60;
// Fallback hard cap; real cap is computed from the active monitor size.
static constexpr int kAppletMaxWidthPxFallback = 1600;
static constexpr int kAppletWidthStepPx = 10;

static constexpr const char* kEnvFileRelPath = "/.config/firmware-quota/env";

static const char* get_home_dir_fallback() {
    const char* home = getenv("HOME");
    if (home && *home) {
        return home;
    }
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir && pw->pw_dir[0] != '\0') {
        return pw->pw_dir;
    }
    return nullptr;
}

static void panel_log(const char* fmt, ...) {
    const char* home = get_home_dir_fallback();

    std::string path;
    if (home && *home) {
        path = std::string(home) + "/.cache/firmware-quota-panel.log";
    } else {
        path = "/tmp/firmware-quota-panel.log";
    }

    FILE* f = fopen(path.c_str(), "a");
    if (!f) return;

    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

    fprintf(f, "[%s] ", ts);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);

    // Also emit to syslog (reliable under D-Bus activation).
    char msg[512];
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    openlog("firmware-quota-panel", LOG_PID, LOG_USER);
    syslog(LOG_INFO, "%s", msg);
    closelog();
}

struct AppletState {
    MatePanelApplet* applet = nullptr;
    GtkWidget* drawing = nullptr;

    // Keep menu action group alive; otherwise menu items may not appear.
    GtkActionGroup* action_group = nullptr;

    std::string prefs_path;
    int width_px = kAppletDefaultWidthPx;
    int max_width_px = kAppletMaxWidthPxFallback;

    // Lifetime management: panel may destroy the applet while a background fetch is
    // still running. We keep the state alive until all in-flight fetch callbacks
    // have completed.
    std::atomic<int> refcount{1};
    std::atomic<bool> destroy_requested{false};

    guint refresh_timer_id = 0;
    guint ui_tick_id = 0;
    int refresh_interval_s = 30;
    gint64 next_refresh_us = 0;

    std::string api_key;
    std::string token;
    std::optional<AuthMethod> preferred_auth_method;

    bool fetching = false;
    bool have_quota = false;
    QuotaData current_quota{};
    std::string last_error;

    // Last known good state (used to keep UI stable on network errors).
    bool have_last_good = false;
    QuotaData last_good_quota{};
    double prev_good_pct = 0.0;
    double last_delta_pp = 0.0;
    time_t last_window_start_utc = 0;
    time_t last_window_reset_ts = 0;

    static constexpr int kDeltaHistN = 5;
    std::array<double, kDeltaHistN> delta_hist_pp{};
    std::array<time_t, kDeltaHistN> delta_hist_ts{};
    int delta_hist_count = 0;
    int delta_hist_next = 0;
    time_t last_success_ts = 0;
    time_t last_failure_ts = 0;
    int consecutive_failures = 0;
    long last_http_code = 0;
    CURLcode last_curl_code = CURLE_OK;
    std::string last_curl_error;

    std::mutex mu;
};

static void delta_hist_clear(AppletState* s) {
    if (!s) return;
    s->delta_hist_count = 0;
    s->delta_hist_next = 0;
    s->delta_hist_pp.fill(0.0);
    s->delta_hist_ts.fill(0);
}

static void delta_hist_push(AppletState* s, double delta_pp, time_t ts) {
    if (!s) return;
    if (s->delta_hist_next < 0 || s->delta_hist_next >= AppletState::kDeltaHistN) {
        s->delta_hist_next = 0;
    }
    s->delta_hist_pp[s->delta_hist_next] = delta_pp;
    s->delta_hist_ts[s->delta_hist_next] = ts;
    s->delta_hist_next = (s->delta_hist_next + 1) % AppletState::kDeltaHistN;
    if (s->delta_hist_count < AppletState::kDeltaHistN) {
        s->delta_hist_count++;
    }
}

static void state_ref(AppletState* s) {
    if (!s) return;
    s->refcount.fetch_add(1, std::memory_order_relaxed);
}

static void state_unref(AppletState* s) {
    if (!s) return;
    const int prev = s->refcount.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1) {
        delete s;
    }
}

static double clamp_pct(double v) {
    if (v < 0.0) return 0.0;
    if (v > 100.0) return 100.0;
    return v;
}

static int clamp_width(int w, int max_w) {
    if (w < kAppletMinWidthPx) return kAppletMinWidthPx;
    if (max_w < kAppletMinWidthPx) max_w = kAppletMinWidthPx;
    if (w > max_w) return max_w;
    return w;
}

static int compute_dynamic_max_width_px(AppletState* state) {
    // We cannot reliably know "free" panel space (other applets can constrain us),
    // but we can cap to the current monitor major axis.
    if (!state || !state->applet) return kAppletMaxWidthPxFallback;

    MatePanelAppletOrient orient = mate_panel_applet_get_orient(state->applet);

    GtkWidget* widget = GTK_WIDGET(state->applet);
    GdkWindow* win = gtk_widget_get_window(widget);
    if (!win && state->drawing) {
        win = gtk_widget_get_window(state->drawing);
    }

    int major_px = 0;
    if (win) {
        GdkDisplay* display = gdk_window_get_display(win);
        if (display) {
            GdkMonitor* mon = gdk_display_get_monitor_at_window(display, win);
            if (mon) {
                GdkRectangle geo{};
                gdk_monitor_get_geometry(mon, &geo);
                major_px = (orient == MATE_PANEL_APPLET_ORIENT_LEFT || orient == MATE_PANEL_APPLET_ORIENT_RIGHT)
                               ? geo.height
                               : geo.width;
            }
        }
    }

    if (major_px <= 0) {
        GdkScreen* screen = gtk_widget_get_screen(widget);
        if (!screen) return kAppletMaxWidthPxFallback;
        major_px = (orient == MATE_PANEL_APPLET_ORIENT_LEFT || orient == MATE_PANEL_APPLET_ORIENT_RIGHT)
                       ? gdk_screen_get_height(screen)
                       : gdk_screen_get_width(screen);
    }

    // Leave margin so we don't request the full panel length.
    int max_w = major_px - 20;
    if (max_w < kAppletMinWidthPx) max_w = kAppletMinWidthPx;

    // Safety cap: keeps the hint array bounded on very wide screens.
    const int safety_cap = 4096;
    if (max_w > safety_cap) max_w = safety_cap;

    return max_w;
}

static std::string get_panel_cfg_path() {
    const char* home = get_home_dir_fallback();
    if (home && *home) {
        return std::string(home) + "/.config/firmware-quota/panel-applet.conf";
    }
    return "/tmp/firmware-quota-panel-applet.conf";
}

static void ensure_panel_cfg_dir() {
    const char* home = get_home_dir_fallback();
    if (!home || !*home) return;
    std::string dir = std::string(home) + "/.config/firmware-quota";
    g_mkdir_with_parents(dir.c_str(), 0700);
}

static bool load_width_for_prefs_path(const std::string& prefs_path, int* out_width) {
    if (!out_width) return false;
    if (prefs_path.empty()) return false;

    std::ifstream in(get_panel_cfg_path());
    if (!in.is_open()) return false;

    std::string key;
    int w = 0;
    while (in >> key >> w) {
        if (key == prefs_path) {
            // Don't clamp here; runtime clamp depends on current monitor size.
            // Still guard against nonsense values in the config file.
            *out_width = clamp_width(w, 4096);
            return true;
        }
    }
    return false;
}

static void save_width_for_prefs_path(const std::string& prefs_path, int width) {
    if (prefs_path.empty()) return;
    ensure_panel_cfg_dir();

    const std::string path = get_panel_cfg_path();

    std::vector<std::pair<std::string, int>> entries;
    {
        std::ifstream in(path);
        if (in.is_open()) {
            std::string key;
            int w = 0;
            while (in >> key >> w) {
                if (!key.empty()) {
                    entries.emplace_back(key, clamp_width(w, 4096));
                }
            }
        }
    }

    bool updated = false;
    for (auto& kv : entries) {
        if (kv.first == prefs_path) {
            kv.second = clamp_width(width, 4096);
            updated = true;
            break;
        }
    }
    if (!updated) {
        entries.emplace_back(prefs_path, clamp_width(width, 4096));
    }

    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) return;
        for (const auto& kv : entries) {
            out << kv.first << " " << kv.second << "\n";
        }
    }
    (void)rename(tmp.c_str(), path.c_str());
}

static bool read_key_from_env_file(std::string* out_key) {
    if (!out_key) return false;

    const char* home = getenv("HOME");
    if (!home || !*home) return false;

    std::string path = std::string(home) + kEnvFileRelPath;
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("#", 0) == 0) continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        if (key == "FIRMWARE_API_KEY" && !val.empty()) {
            *out_key = val;
            return true;
        }
    }

    return false;
}

static std::string get_env_file_path() {
    const char* home = get_home_dir_fallback();
    if (home && *home) {
        return std::string(home) + kEnvFileRelPath;
    }
    return "/tmp/firmware-quota-env";
}

static bool write_api_key_env_file(const std::string& api_key) {
    if (api_key.empty()) return false;
    ensure_panel_cfg_dir();

    const std::string path = get_env_file_path();
    const std::string tmp = path + ".tmp";

    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) return false;
        out << "# Managed by firmware-quota panel applet\n";
        out << "# NOTE: this is a plaintext key file. chmod 600 recommended.\n";
        out << "FIRMWARE_API_KEY=" << api_key << "\n";
    }

    if (rename(tmp.c_str(), path.c_str()) != 0) {
        (void)remove(tmp.c_str());
        return false;
    }

    (void)chmod(path.c_str(), 0600);
    return true;
}

static void delete_api_key_env_file() {
    const std::string path = get_env_file_path();
    (void)remove(path.c_str());
}

static bool load_api_key(AppletState* state) {
    if (!state) return false;

    const char* env = getenv("FIRMWARE_API_KEY");
    if (env && *env) {
        state->api_key = env;
        state->token = extract_token(state->api_key);
        return true;
    }

    std::string key;
    if (read_key_from_env_file(&key)) {
        state->api_key = key;
        state->token = extract_token(state->api_key);
        return true;
    }

    return false;
}

static void reload_api_key(AppletState* state) {
    if (!state) return;

    // Clear so we don't keep using a stale value.
    state->api_key.clear();
    state->token.clear();

    (void)load_api_key(state);
}

static void set_tooltip(AppletState* state) {
    if (!state || !state->drawing) return;

    std::lock_guard<std::mutex> lock(state->mu);

    const gint64 now = g_get_monotonic_time();
    gint64 remaining_us = state->next_refresh_us - now;
    if (remaining_us < 0) remaining_us = 0;
    int remaining_s = (int)((remaining_us + 999999) / 1000000);

    char tip[512];

    const bool stale = !state->last_error.empty();
    const bool have = state->have_last_good || state->have_quota;
    const QuotaData q = state->have_last_good ? state->last_good_quota : state->current_quota;

    std::string status = stale ? "STALE" : (have ? "OK" : "INIT");
    std::string extra;

    if (have) {
        // Delta in percentage points (since last successful refresh).
        char delta_buf[128];
        if (state->last_success_ts != 0) {
            snprintf(delta_buf, sizeof(delta_buf), "Delta: %+0.1fpp", state->last_delta_pp);
        } else {
            snprintf(delta_buf, sizeof(delta_buf), "Delta: --");
        }

        char last_ok_buf[64];
        if (state->last_success_ts != 0) {
            const int64_t age_s = (int64_t)difftime(time(nullptr), state->last_success_ts);
            snprintf(last_ok_buf, sizeof(last_ok_buf), "Last OK: %s ago", format_duration_compact(age_s).c_str());
        } else {
            snprintf(last_ok_buf, sizeof(last_ok_buf), "Last OK: --");
        }

        // Reset display if available.
        std::string reset_line = "Reset: N/A";
        if (q.reset_time != "N/A" && !q.reset_time.empty()) {
            time_t reset_utc;
            if (parse_iso8601_utc_to_time_t(q.reset_time, &reset_utc)) {
                time_t now_s = time(nullptr);
                int64_t until_reset = static_cast<int64_t>(difftime(reset_utc, now_s));
                if (until_reset < 0) until_reset = 0;
                reset_line = "Reset: " + format_duration_compact(until_reset);
            }
        }

        extra = std::string(delta_buf) + "\n" + last_ok_buf + "\n" + reset_line;

        if (state->delta_hist_count > 0) {
            std::string hist = "Recent deltas (old->new): ";
            // Reconstruct oldest->newest from ring.
            const int n = state->delta_hist_count;
            int start = state->delta_hist_next - n;
            while (start < 0) start += AppletState::kDeltaHistN;
            for (int i = 0; i < n; i++) {
                const int idx = (start + i) % AppletState::kDeltaHistN;
                char b[32];
                snprintf(b, sizeof(b), "%+0.1f", state->delta_hist_pp[idx]);
                if (i != 0) hist += ", ";
                hist += b;
            }
            hist += " pp";
            extra += "\n" + hist;
        }

        if (state->last_window_reset_ts != 0) {
            const int64_t age_s = (int64_t)difftime(time(nullptr), state->last_window_reset_ts);
            extra += "\n" + (std::string("Window reset: ") + format_duration_compact(age_s) + " ago");
        }
    }

    if (stale) {
        char err_meta[256];
        const char* curl_name = curl_easy_strerror(state->last_curl_code);
        snprintf(err_meta, sizeof(err_meta),
                 "Failures: %d\nLast error: %s\nHTTP: %ld\nCURL: %d (%s)",
                 state->consecutive_failures,
                 truncate_for_display(state->last_error, 120).c_str(),
                 state->last_http_code,
                 (int)state->last_curl_code,
                 curl_name ? curl_name : "?");
        if (!extra.empty()) {
            extra += "\n";
        }
        extra += err_meta;
    }

    if (!have) {
        snprintf(tip, sizeof(tip),
                 "Firmware Quota (panel)\nStatus: %s\nNext refresh: %ds",
                 status.c_str(),
                 remaining_s);
    } else {
        snprintf(tip, sizeof(tip),
                 "Firmware Quota (panel)\nStatus: %s\nUsage: %.1f%%\n%s\nNext refresh: %ds",
                 status.c_str(),
                 q.percentage,
                 extra.c_str(),
                 remaining_s);
    }

    gtk_widget_set_tooltip_text(state->drawing, tip);
}

static void pick_color(double pct, double* r, double* g, double* b) {
    // Match existing thresholds.
    if (pct < 50.0) {
        *r = 0.20; *g = 0.78; *b = 0.30;
    } else if (pct < 80.0) {
        *r = 0.95; *g = 0.75; *b = 0.20;
    } else {
        *r = 0.91; *g = 0.28; *b = 0.38;
    }
}

static bool compute_window_start_utc(const std::string& reset_time, time_t* out_window_start_utc) {
    if (!out_window_start_utc) return false;
    *out_window_start_utc = 0;
    if (reset_time.empty() || reset_time == "N/A") return false;

    time_t reset_utc = 0;
    if (!parse_iso8601_utc_to_time_t(reset_time, &reset_utc)) {
        return false;
    }
    const time_t window_start = reset_utc - (time_t)kQuotaWindowSeconds;
    if (window_start <= 0) return false;
    *out_window_start_utc = window_start;
    return true;
}

static gboolean on_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    AppletState* state = (AppletState*)user_data;
    if (!state) return FALSE;

    static bool logged = false;
    if (!logged) {
        logged = true;
        panel_log("on_draw first call");
    }

    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);
    const int w = a.width;
    const int h = a.height;
    if (w <= 0 || h <= 0) return FALSE;

    double pct = 0.0;
    std::string err;
    bool have = false;
    bool have_good = false;
    {
        std::lock_guard<std::mutex> lock(state->mu);
        have_good = state->have_last_good;
        have = have_good || state->have_quota;
        const QuotaData q = have_good ? state->last_good_quota : state->current_quota;
        pct = clamp_pct(q.percentage);
        err = state->last_error;
    }

    const bool stale = (!err.empty()) && have_good;

    // Background (make it very visible against the panel).
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.55);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);

    // Fill.
    double fr = 0.6, fg = 0.6, fb = 0.6;
    if (have) {
        pick_color(pct, &fr, &fg, &fb);
    }
    const double fill_frac = have ? (pct / 100.0) : 0.0;

    MatePanelAppletOrient orient = mate_panel_applet_get_orient(state->applet);
    if (orient == MATE_PANEL_APPLET_ORIENT_LEFT || orient == MATE_PANEL_APPLET_ORIENT_RIGHT) {
        // Vertical panel: fill from bottom.
        int filled = (int)std::lround(fill_frac * h);
        cairo_set_source_rgba(cr, fr, fg, fb, 0.95);
        cairo_rectangle(cr, 0, h - filled, w, filled);
        cairo_fill(cr);
    } else {
        // Horizontal panel: fill from left.
        int filled = (int)std::lround(fill_frac * w);
        cairo_set_source_rgba(cr, fr, fg, fb, 0.95);
        cairo_rectangle(cr, 0, 0, filled, h);
        cairo_fill(cr);
    }

    // Delta history overlay (last 5 successful refreshes), stacked from the leading edge.
    if (have_good) {
        std::array<double, AppletState::kDeltaHistN> dpp{};
        std::array<time_t, AppletState::kDeltaHistN> dts{};
        int n = 0;
        int next = 0;
        {
            std::lock_guard<std::mutex> lock(state->mu);
            n = state->delta_hist_count;
            next = state->delta_hist_next;
            dpp = state->delta_hist_pp;
            dts = state->delta_hist_ts;
        }

        if (n > 0) {
            const double fill_px = (orient == MATE_PANEL_APPLET_ORIENT_LEFT || orient == MATE_PANEL_APPLET_ORIENT_RIGHT)
                                       ? (pct / 100.0) * h
                                       : (pct / 100.0) * w;

            const double max_hist_px = std::min(fill_px, ((orient == MATE_PANEL_APPLET_ORIENT_LEFT || orient == MATE_PANEL_APPLET_ORIENT_RIGHT) ? h : w) * 0.35);
            if (max_hist_px > 0.0) {
                // Oldest->newest indices from ring.
                int start = next - n;
                while (start < 0) start += AppletState::kDeltaHistN;

                struct Seg { double px; double delta; int age_rank; };
                std::vector<Seg> segs;
                segs.reserve(n);

                const double max_pp_per_seg = 15.0;
                const double min_px = 2.0;
                const double axis_px = (orient == MATE_PANEL_APPLET_ORIENT_LEFT || orient == MATE_PANEL_APPLET_ORIENT_RIGHT) ? (double)h : (double)w;

                for (int i = 0; i < n; i++) {
                    const int idx = (start + i) % AppletState::kDeltaHistN;
                    const double delta = dpp[idx];
                    if (std::fabs(delta) < 0.05) continue;

                    const double pp = std::min(std::fabs(delta), max_pp_per_seg);
                    double px = (pp / 100.0) * axis_px;
                    if (px < min_px) px = min_px;
                    segs.push_back({px, delta, i});
                }

                // Fit to available space.
                double sum_px = 0.0;
                for (const auto& s : segs) sum_px += s.px;
                if (sum_px > 0.0) {
                    const double scale = std::min(1.0, max_hist_px / sum_px);
                    for (auto& s : segs) s.px *= scale;

                    // Draw from newest to oldest, starting at leading edge.
                    double cursor = 0.0;
                    for (int i = (int)segs.size() - 1; i >= 0; i--) {
                        const auto& s = segs[i];
                        const bool inc = s.delta > 0.0;

                        // Recency alpha: newest strongest.
                        const int newest_rank = (int)segs.size() - 1;
                        const double t = newest_rank > 0 ? (double)i / (double)newest_rank : 1.0;
                        const double alpha = 0.90 - (1.0 - t) * 0.60; // ~0.30..0.90

                        const double dr = inc ? 0.25 : 0.98;
                        const double dg = inc ? 0.65 : 0.55;
                        const double db = inc ? 0.98 : 0.15;

                        cairo_set_source_rgba(cr, dr, dg, db, alpha);

                        if (orient == MATE_PANEL_APPLET_ORIENT_LEFT || orient == MATE_PANEL_APPLET_ORIENT_RIGHT) {
                            // Leading edge is the top (since fill grows from bottom). Place near fill top.
                            const double lead_y = h - fill_px;
                            const double y0 = lead_y + cursor;
                            const double y1 = y0 + s.px;
                            if (y0 < h) {
                                cairo_rectangle(cr, 0, y0, w, std::min((double)h, y1) - y0);
                                cairo_fill(cr);
                            }
                        } else {
                            const double lead_x = fill_px;
                            const double x1 = lead_x - cursor;
                            const double x0 = x1 - s.px;
                            if (x1 > 0) {
                                cairo_rectangle(cr, std::max(0.0, x0), 0, x1 - std::max(0.0, x0), h);
                                cairo_fill(cr);
                            }
                        }

                        // Subtle separator.
                        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.12);
                        cairo_set_line_width(cr, 1.0);
                        if (orient == MATE_PANEL_APPLET_ORIENT_LEFT || orient == MATE_PANEL_APPLET_ORIENT_RIGHT) {
                            const double lead_y = h - fill_px;
                            const double y = lead_y + cursor;
                            cairo_move_to(cr, 0, y);
                            cairo_line_to(cr, w, y);
                            cairo_stroke(cr);
                        } else {
                            const double lead_x = fill_px;
                            const double x = lead_x - cursor;
                            cairo_move_to(cr, x, 0);
                            cairo_line_to(cr, x, h);
                            cairo_stroke(cr);
                        }

                        cursor += s.px;
                        if (cursor >= max_hist_px) break;
                    }
                }
            }
        }
    }

    // Stale overlay (network issue) - hatch pattern.
    if (stale) {
        cairo_set_source_rgba(cr, 0.98, 0.72, 0.15, 0.25);
        cairo_set_line_width(cr, 1.0);
        const int step = 6;
        for (int x = -h; x < w + h; x += step) {
            cairo_move_to(cr, x, 0);
            cairo_line_to(cr, x + h, h);
        }
        cairo_stroke(cr);
    }

    // Border.
    if (stale) {
        cairo_set_source_rgba(cr, 0.98, 0.72, 0.15, 0.85);
    } else {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.60);
    }
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, 0.5, 0.5, w - 1.0, h - 1.0);
    cairo_stroke(cr);

    // Tiny percent text overlay.
    char text[16];
    if (!have) {
        snprintf(text, sizeof(text), "--");
    } else {
        int pct_i = (int)std::llround(pct);
        if (!err.empty() && have_good) {
            snprintf(text, sizeof(text), "%d%%*", pct_i);
        } else if (!err.empty() && !have_good) {
            snprintf(text, sizeof(text), "ERR");
        } else {
            snprintf(text, sizeof(text), "%d%%", pct_i);
        }
    }

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, std::max(8.0, std::min((double)h * 0.70, 12.0)));
    cairo_text_extents_t ext;
    cairo_text_extents(cr, text, &ext);

    const double tx = (w - ext.width) / 2.0 - ext.x_bearing;
    const double ty = (h - ext.height) / 2.0 - ext.y_bearing;

    // Shadow
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.55);
    cairo_move_to(cr, tx + 1.0, ty + 1.0);
    cairo_show_text(cr, text);

    // Foreground
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.92);
    cairo_move_to(cr, tx, ty);
    cairo_show_text(cr, text);

    return FALSE;
}

static void ensure_curl_global_init() {
    static std::once_flag once;
    std::call_once(once, []() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        panel_log("curl_global_init done");
    });
}

struct FetchThreadData {
    AppletState* state;
    RequestResult result;
    bool success;
    QuotaData quota_data;
    std::optional<AuthMethod> used_method;
    std::string error_message;
};

static void apply_width(AppletState* state, int width_px);

static gboolean on_fetch_complete(gpointer user_data) {
    FetchThreadData* data = (FetchThreadData*)user_data;
    AppletState* state = data->state;

    {
        std::lock_guard<std::mutex> lock(state->mu);
        state->fetching = false;
        if (data->success) {
            const time_t now = time(nullptr);

            // Detect 5h window boundary and clear delta history when it changes.
            time_t window_start_utc = 0;
            const bool have_window = compute_window_start_utc(data->quota_data.reset_time, &window_start_utc);
            const int64_t tol_s = 60;
            if (have_window) {
                if (state->last_window_start_utc != 0 && std::llabs((long long)window_start_utc - (long long)state->last_window_start_utc) > tol_s) {
                    delta_hist_clear(state);
                    state->last_window_reset_ts = now;
                }
                state->last_window_start_utc = window_start_utc;
            }

            const double new_pct = data->quota_data.percentage;
            const double prev_pct = state->have_last_good ? state->last_good_quota.percentage : new_pct;
            state->prev_good_pct = prev_pct;
            state->last_delta_pp = new_pct - prev_pct;

            // Heuristic: if reset_time is missing, but we see a large negative jump, treat it as a window reset.
            if (!have_window && state->have_last_good && state->last_delta_pp <= -10.0) {
                delta_hist_clear(state);
                state->last_window_reset_ts = now;
            }

            // Push delta into history (only after potential reset handling).
            delta_hist_push(state, state->last_delta_pp, now);

            state->current_quota = data->quota_data;
            state->have_quota = true;

            state->last_good_quota = data->quota_data;
            state->have_last_good = true;
            state->last_success_ts = now;
            state->consecutive_failures = 0;
            state->last_http_code = data->result.http_code;
            state->last_curl_code = data->result.curl_code;
            state->last_curl_error = data->result.curl_error;

            state->last_error.clear();
            if (data->used_method.has_value()) {
                state->preferred_auth_method = data->used_method;
            }
        } else {
            state->last_error = data->error_message;
            state->last_failure_ts = time(nullptr);
            state->consecutive_failures += 1;
            state->last_http_code = data->result.http_code;
            state->last_curl_code = data->result.curl_code;
            state->last_curl_error = data->result.curl_error;
        }
    }

    if (!state->destroy_requested.load(std::memory_order_relaxed) && state->drawing) {
        set_tooltip(state);
        gtk_widget_queue_draw(state->drawing);
    }

    delete data;
    state_unref(state);
    return G_SOURCE_REMOVE;
}

static void* fetch_quota_thread(void* arg) {
    FetchThreadData* data = (FetchThreadData*)arg;
    AppletState* state = data->state;

    data->success = false;

    if (state->api_key.empty()) {
        data->error_message = "Missing FIRMWARE_API_KEY";
        g_idle_add(on_fetch_complete, data);
        return nullptr;
    }

    data->result = try_auth_methods(
        state->api_key,
        state->token,
        state->preferred_auth_method,
        &data->used_method
    );

    if (data->result.curl_code != CURLE_OK) {
        data->error_message = std::string("Request failed: ") + curl_easy_strerror(data->result.curl_code);
        if (!data->result.curl_error.empty()) {
            data->error_message += " (" + data->result.curl_error + ")";
        }
        g_idle_add(on_fetch_complete, data);
        return nullptr;
    }

    if (!is_http_success(data->result.http_code)) {
        data->error_message = "HTTP error: " + std::to_string(data->result.http_code);
        if (!data->result.body.empty()) {
            data->error_message += ": " + truncate_for_display(data->result.body, 200);
        }
        g_idle_add(on_fetch_complete, data);
        return nullptr;
    }

    try {
        json j = json::parse(data->result.body);
        if (!j.contains("used") || j["used"].is_null()) {
            data->error_message = "Parse error: missing 'used'";
            g_idle_add(on_fetch_complete, data);
            return nullptr;
        }

        double used = j["used"].get<double>();
        std::string reset = (j.contains("reset") && !j["reset"].is_null()) ? j["reset"].get<std::string>() : "";

        data->quota_data.used = used;
        data->quota_data.percentage = used * 100.0;
        data->quota_data.reset_time = reset.empty() ? "N/A" : reset;
        data->quota_data.timestamp = time(nullptr);
        data->success = true;
    } catch (const std::exception& e) {
        data->error_message = std::string("Parse error: ") + e.what();
    }

    g_idle_add(on_fetch_complete, data);
    return nullptr;
}

static void start_fetch(AppletState* state) {
    if (!state) return;

    if (state->destroy_requested.load(std::memory_order_relaxed)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state->mu);
        if (state->fetching) {
            return;
        }
        state->fetching = true;
        state->last_error.clear();
    }

    FetchThreadData* data = new FetchThreadData();
    data->state = state;

    // Hold a ref until on_fetch_complete runs.
    state_ref(state);

    pthread_t thread;
    if (pthread_create(&thread, nullptr, fetch_quota_thread, data) == 0) {
        pthread_detach(thread);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state->mu);
        state->fetching = false;
    }
    delete data;
    state_unref(state);
}

static gboolean on_refresh_timer(gpointer user_data) {
    AppletState* state = (AppletState*)user_data;
    if (!state) return G_SOURCE_REMOVE;
    if (state->destroy_requested.load(std::memory_order_relaxed)) return G_SOURCE_REMOVE;

    state->next_refresh_us = g_get_monotonic_time() + (gint64)state->refresh_interval_s * 1000000;
    set_tooltip(state);
    start_fetch(state);
    return G_SOURCE_CONTINUE;
}

static gboolean on_ui_tick(gpointer user_data) {
    AppletState* state = (AppletState*)user_data;
    if (!state) return G_SOURCE_REMOVE;
    if (state->destroy_requested.load(std::memory_order_relaxed)) return G_SOURCE_REMOVE;
    set_tooltip(state);
    return G_SOURCE_CONTINUE;
}

static void change_refresh_rate(AppletState* state, int new_interval_s) {
    if (!state) return;
    if (new_interval_s < 5) new_interval_s = 5;

    state->refresh_interval_s = new_interval_s;
    state->next_refresh_us = g_get_monotonic_time() + (gint64)state->refresh_interval_s * 1000000;

    if (state->refresh_timer_id > 0) {
        g_source_remove(state->refresh_timer_id);
        state->refresh_timer_id = 0;
    }
    state->refresh_timer_id = g_timeout_add_seconds(state->refresh_interval_s, on_refresh_timer, state);

    set_tooltip(state);
}

static void on_action_refresh_now(GtkAction*, gpointer user_data) {
    AppletState* state = (AppletState*)user_data;
    if (!state) return;
    // Reset countdown to full interval after manual refresh.
    state->next_refresh_us = g_get_monotonic_time() + (gint64)state->refresh_interval_s * 1000000;
    set_tooltip(state);
    start_fetch(state);
}

static void on_action_rate(GtkAction* action, GtkRadioAction* current, gpointer user_data) {
    (void)action;
    AppletState* state = (AppletState*)user_data;
    if (!state) return;

    const int rate_s = gtk_radio_action_get_current_value(current);
    change_refresh_rate(state, rate_s);
}

static void on_action_width(GtkAction* action, GtkRadioAction* current, gpointer user_data) {
    (void)action;
    AppletState* state = (AppletState*)user_data;
    if (!state) return;

    const int w = gtk_radio_action_get_current_value(current);
    apply_width(state, w);
}

static void on_action_width_decrease(GtkAction*, gpointer user_data) {
    AppletState* state = (AppletState*)user_data;
    if (!state) return;
    apply_width(state, state->width_px - kAppletWidthStepPx);
}

static void on_action_width_increase(GtkAction*, gpointer user_data) {
    AppletState* state = (AppletState*)user_data;
    if (!state) return;
    apply_width(state, state->width_px + kAppletWidthStepPx);
}

static void on_action_width_decrease_100(GtkAction*, gpointer user_data) {
    AppletState* state = (AppletState*)user_data;
    if (!state) return;
    apply_width(state, state->width_px - 100);
}

static void on_action_width_increase_100(GtkAction*, gpointer user_data) {
    AppletState* state = (AppletState*)user_data;
    if (!state) return;
    apply_width(state, state->width_px + 100);
}

static void on_action_width_reset(GtkAction*, gpointer user_data) {
    AppletState* state = (AppletState*)user_data;
    if (!state) return;
    apply_width(state, kAppletDefaultWidthPx);
}

static void on_action_api_key_set(GtkAction*, gpointer user_data) {
    AppletState* state = (AppletState*)user_data;
    if (!state) return;
    if (state->destroy_requested.load(std::memory_order_relaxed)) return;

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Set Firmware API Key",
        nullptr,
        (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "Cancel",
        GTK_RESPONSE_CANCEL,
        "Save",
        GTK_RESPONSE_OK,
        nullptr
    );

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(content), vbox);

    GtkWidget* label = gtk_label_new(
        "Enter your Firmware API key (stored in ~/.config/firmware-quota/env with mode 600)."
    );
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    gtk_entry_set_invisible_char(GTK_ENTRY(entry), '*');
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "fw_api_... or token");
    gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);

    gtk_widget_show_all(dialog);

    const int resp = gtk_dialog_run(GTK_DIALOG(dialog));
    if (resp == GTK_RESPONSE_OK) {
        const char* text = gtk_entry_get_text(GTK_ENTRY(entry));
        std::string key = text ? text : "";

        if (!key.empty()) {
            if (write_api_key_env_file(key)) {
                {
                    std::lock_guard<std::mutex> lock(state->mu);
                    state->api_key = key;
                    state->token = extract_token(state->api_key);
                    state->last_error.clear();
                }
                // Trigger immediate fetch.
                start_fetch(state);
            } else {
                std::lock_guard<std::mutex> lock(state->mu);
                state->last_error = "Failed to write env file";
            }
        }
    }

    gtk_widget_destroy(dialog);

    if (!state->destroy_requested.load(std::memory_order_relaxed) && state->drawing) {
        set_tooltip(state);
        gtk_widget_queue_draw(state->drawing);
    }
}

static void on_action_api_key_clear(GtkAction*, gpointer user_data) {
    AppletState* state = (AppletState*)user_data;
    if (!state) return;
    if (state->destroy_requested.load(std::memory_order_relaxed)) return;

    delete_api_key_env_file();
    {
        std::lock_guard<std::mutex> lock(state->mu);
        state->api_key.clear();
        state->token.clear();
        state->have_quota = false;
        state->last_error = "Missing FIRMWARE_API_KEY";
    }

    if (state->drawing) {
        set_tooltip(state);
        gtk_widget_queue_draw(state->drawing);
    }
}

static void on_action_api_key_reload(GtkAction*, gpointer user_data) {
    AppletState* state = (AppletState*)user_data;
    if (!state) return;
    if (state->destroy_requested.load(std::memory_order_relaxed)) return;

    {
        std::lock_guard<std::mutex> lock(state->mu);
        reload_api_key(state);
        if (state->api_key.empty()) {
            state->last_error = "Missing FIRMWARE_API_KEY";
            state->have_quota = false;
        } else {
            state->last_error.clear();
        }
    }
    start_fetch(state);
}


static void on_change_size(MatePanelApplet* applet, guint size, gpointer user_data) {
    AppletState* state = (AppletState*)user_data;
    if (!state || !state->drawing) return;

    if (size == 0) {
        size = 24;
    }

    MatePanelAppletOrient orient = mate_panel_applet_get_orient(applet);
    if (orient == MATE_PANEL_APPLET_ORIENT_LEFT || orient == MATE_PANEL_APPLET_ORIENT_RIGHT) {
        // Vertical panel: fixed major height, minor width follows panel thickness.
        gtk_widget_set_size_request(GTK_WIDGET(applet), (int)size, state->width_px);
        gtk_widget_set_size_request(state->drawing, (int)size, state->width_px);
    } else {
        // Horizontal panel: fixed major width, minor height follows panel thickness.
        gtk_widget_set_size_request(GTK_WIDGET(applet), state->width_px, (int)size);
        gtk_widget_set_size_request(state->drawing, state->width_px, (int)size);
    }
    gtk_widget_queue_draw(state->drawing);
}

static void apply_width(AppletState* state, int width_px);

static void apply_width(AppletState* state, int width_px) {
    if (!state || !state->applet || !state->drawing) return;
    if (state->destroy_requested.load(std::memory_order_relaxed)) return;

    state->max_width_px = compute_dynamic_max_width_px(state);
    state->width_px = clamp_width(width_px, state->max_width_px);

    const int w = state->width_px;

    // Provide a full range of accepted major-axis sizes.
    // Some panel layouts cannot satisfy a large jump (e.g. +100px). If we only
    // advertise hints near the target width, mate-panel may keep the current size.
    // A full range lets it pick the best fit.
    std::vector<int> hints;
    hints.reserve((state->max_width_px - kAppletMinWidthPx) / kAppletWidthStepPx + 1);
    for (int v = kAppletMinWidthPx; v <= state->max_width_px; v += kAppletWidthStepPx) {
        hints.push_back(v);
    }
    // Ensure the exact requested width is included even if constants change.
    if (w % kAppletWidthStepPx != 0) {
        hints.push_back(w);
    }
    mate_panel_applet_set_size_hints(state->applet, hints.data(), (int)hints.size(), 0);

    guint size = mate_panel_applet_get_size(state->applet);
    if (size == 0) size = 24;
    on_change_size(state->applet, size, state);

    save_width_for_prefs_path(state->prefs_path, state->width_px);
}

static void setup_panel_menu(AppletState* state) {
    if (!state || !state->applet) return;

    GtkActionGroup* group = gtk_action_group_new("FirmwareQuotaActions");

    // Refresh now
    GtkActionEntry refresh_entries[] = {
        {"FirmwareQuotaRefreshNow", nullptr, "Refresh Now", nullptr, "Refresh immediately", G_CALLBACK(on_action_refresh_now)},
        {"FirmwareQuotaWidthDec", nullptr, "-10px", nullptr, "Decrease width", G_CALLBACK(on_action_width_decrease)},
        {"FirmwareQuotaWidthInc", nullptr, "+10px", nullptr, "Increase width", G_CALLBACK(on_action_width_increase)},
        {"FirmwareQuotaWidthDec100", nullptr, "-100px", nullptr, "Decrease width by 100px", G_CALLBACK(on_action_width_decrease_100)},
        {"FirmwareQuotaWidthInc100", nullptr, "+100px", nullptr, "Increase width by 100px", G_CALLBACK(on_action_width_increase_100)},
        {"FirmwareQuotaWidthReset", nullptr, "Reset (120px)", nullptr, "Reset width", G_CALLBACK(on_action_width_reset)},
        {"FirmwareQuotaApiSet", nullptr, "Set...", nullptr, "Store API key", G_CALLBACK(on_action_api_key_set)},
        {"FirmwareQuotaApiReload", nullptr, "Reload", nullptr, "Reload API key", G_CALLBACK(on_action_api_key_reload)},
        {"FirmwareQuotaApiClear", nullptr, "Clear Stored Key", nullptr, "Remove stored key", G_CALLBACK(on_action_api_key_clear)},
    };
    gtk_action_group_add_actions(group, refresh_entries, 9, state);

    // Refresh rate radio group
    GtkRadioActionEntry rate_entries[] = {
        {"FirmwareQuotaRate15", nullptr, "15s", nullptr, "Refresh every 15 seconds", 15},
        {"FirmwareQuotaRate30", nullptr, "30s", nullptr, "Refresh every 30 seconds", 30},
        {"FirmwareQuotaRate60", nullptr, "60s", nullptr, "Refresh every 60 seconds", 60},
        {"FirmwareQuotaRate120", nullptr, "120s", nullptr, "Refresh every 120 seconds", 120},
    };
    gtk_action_group_add_radio_actions(group, rate_entries, 4, state->refresh_interval_s, G_CALLBACK(on_action_rate), state);

    // Width radio group
    GtkRadioActionEntry width_entries[] = {
        {"FirmwareQuotaWidth80", nullptr, "80px", nullptr, "Applet width 80px", 80},
        {"FirmwareQuotaWidth100", nullptr, "100px", nullptr, "Applet width 100px", 100},
        {"FirmwareQuotaWidth120", nullptr, "120px", nullptr, "Applet width 120px", 120},
        {"FirmwareQuotaWidth160", nullptr, "160px", nullptr, "Applet width 160px", 160},
        {"FirmwareQuotaWidth200", nullptr, "200px", nullptr, "Applet width 200px", 200},
        {"FirmwareQuotaWidth300", nullptr, "300px", nullptr, "Applet width 300px", 300},
        {"FirmwareQuotaWidth400", nullptr, "400px", nullptr, "Applet width 400px", 400},
        {"FirmwareQuotaWidth500", nullptr, "500px", nullptr, "Applet width 500px", 500},
        {"FirmwareQuotaWidth600", nullptr, "600px", nullptr, "Applet width 600px", 600},
        {"FirmwareQuotaWidth800", nullptr, "800px", nullptr, "Applet width 800px", 800},
        {"FirmwareQuotaWidth1000", nullptr, "1000px", nullptr, "Applet width 1000px", 1000},
        {"FirmwareQuotaWidth1200", nullptr, "1200px", nullptr, "Applet width 1200px", 1200},
        {"FirmwareQuotaWidth1600", nullptr, "1600px", nullptr, "Applet width 1600px", 1600},
    };
    gtk_action_group_add_radio_actions(group, width_entries, 13, state->width_px, G_CALLBACK(on_action_width), state);

    // IMPORTANT: mate_panel_applet_setup_menu() internally wraps the provided XML into
    // its own <ui><popup name="MatePanelAppletPopup">... placeholder ...</popup></ui>.
    // Provide ONLY the fragment that should be inserted into the AppletItems placeholder.
    const char* xml =
        "<menuitem action='FirmwareQuotaRefreshNow'/>"
        "<separator/>"
        "<menu action='FirmwareQuotaApiMenu'>"
        "  <menuitem action='FirmwareQuotaApiSet'/>"
        "  <menuitem action='FirmwareQuotaApiReload'/>"
        "  <menuitem action='FirmwareQuotaApiClear'/>"
        "</menu>"
        "<menu action='FirmwareQuotaRateMenu'>"
        "  <menuitem action='FirmwareQuotaRate15'/>"
        "  <menuitem action='FirmwareQuotaRate30'/>"
        "  <menuitem action='FirmwareQuotaRate60'/>"
        "  <menuitem action='FirmwareQuotaRate120'/>"
        "</menu>"
        "<menu action='FirmwareQuotaWidthMenu'>"
        "  <menuitem action='FirmwareQuotaWidthDec'/>"
        "  <menuitem action='FirmwareQuotaWidthInc'/>"
        "  <menuitem action='FirmwareQuotaWidthDec100'/>"
        "  <menuitem action='FirmwareQuotaWidthInc100'/>"
        "  <menuitem action='FirmwareQuotaWidthReset'/>"
        "  <separator/>"
        "  <menuitem action='FirmwareQuotaWidth80'/>"
        "  <menuitem action='FirmwareQuotaWidth100'/>"
        "  <menuitem action='FirmwareQuotaWidth120'/>"
        "  <menuitem action='FirmwareQuotaWidth160'/>"
        "  <menuitem action='FirmwareQuotaWidth200'/>"
        "  <separator/>"
        "  <menuitem action='FirmwareQuotaWidth300'/>"
        "  <menuitem action='FirmwareQuotaWidth400'/>"
        "  <menuitem action='FirmwareQuotaWidth500'/>"
        "  <menuitem action='FirmwareQuotaWidth600'/>"
        "  <separator/>"
        "  <menuitem action='FirmwareQuotaWidth800'/>"
        "  <menuitem action='FirmwareQuotaWidth1000'/>"
        "  <menuitem action='FirmwareQuotaWidth1200'/>"
        "  <menuitem action='FirmwareQuotaWidth1600'/>"
        "</menu>";

    // Add submenu actions as regular GtkAction so the UI manager can label them.
    GtkActionEntry menu_entries[] = {
        {"FirmwareQuotaRateMenu", nullptr, "Refresh Rate", nullptr, nullptr, nullptr},
        {"FirmwareQuotaWidthMenu", nullptr, "Width", nullptr, nullptr, nullptr},
        {"FirmwareQuotaApiMenu", nullptr, "API Key", nullptr, nullptr, nullptr},
    };
    gtk_action_group_add_actions(group, menu_entries, 3, state);

    mate_panel_applet_setup_menu(state->applet, xml, group);

    // Keep action group alive for applet lifetime.
    if (state->action_group) {
        g_object_unref(state->action_group);
    }
    state->action_group = group;
}

static gboolean setup_panel_menu_idle(gpointer user_data) {
    AppletState* state = (AppletState*)user_data;
    if (!state) return G_SOURCE_REMOVE;
    setup_panel_menu(state);
    return G_SOURCE_REMOVE;
}

static void on_change_orient(MatePanelApplet*, MatePanelAppletOrient, gpointer user_data) {
    AppletState* state = (AppletState*)user_data;
    if (!state || !state->drawing) return;
    guint size = mate_panel_applet_get_size(state->applet);
    if (size == 0) size = 24;
    on_change_size(state->applet, size, state);
}

static void on_applet_destroy(GtkWidget*, gpointer user_data) {
    AppletState* state = (AppletState*)user_data;
    if (!state) return;

    state->destroy_requested.store(true, std::memory_order_relaxed);

    if (state->refresh_timer_id > 0) {
        g_source_remove(state->refresh_timer_id);
    }
    if (state->ui_tick_id > 0) {
        g_source_remove(state->ui_tick_id);
    }

    if (state->action_group) {
        g_object_unref(state->action_group);
        state->action_group = nullptr;
    }

    // Prevent any pending UI callback from touching destroyed widgets.
    state->drawing = nullptr;
    state->applet = nullptr;

    // Drop the applet's base reference. The state will be deleted once all
    // in-flight fetch callbacks have completed.
    state_unref(state);
}

static gboolean applet_fill(MatePanelApplet* applet, const gchar* iid, gpointer) {
    // mate-panel's D-Bus API passes an applet_id string. In practice this may be either:
    //   - "FirmwareQuotaApplet" (the section name), or
    //   - "FirmwareQuotaAppletFactory::FirmwareQuotaApplet" (factory::section)
    // Handle both.
    const char* id = iid;
    if (id) {
        const char* sep = strstr(id, "::");
        if (sep && sep[2] != '\0') {
            id = sep + 2;
        }
    }

    if (id && !g_strcmp0(id, kAppletId)) {
        ensure_curl_global_init();
        panel_log("applet_fill iid=%s", iid ? iid : "(null)");

        AppletState* state = new AppletState();
        state->applet = applet;

        // Identify this instance for persisted settings.
        {
            char* p = mate_panel_applet_get_preferences_path(applet);
            if (p) {
                state->prefs_path = p;
                g_free(p);
            }
        }

        load_api_key(state);

        // Fixed-width applet; don't request major-axis expansion.
        mate_panel_applet_set_flags(applet, (MatePanelAppletFlags)0);

        // Load per-instance width (default 120px).
        int stored_w = 0;
        if (load_width_for_prefs_path(state->prefs_path, &stored_w)) {
            state->max_width_px = compute_dynamic_max_width_px(state);
            state->width_px = clamp_width(stored_w, state->max_width_px);
        }
        apply_width(state, state->width_px);

        GtkWidget* drawing = gtk_drawing_area_new();
        state->drawing = drawing;
        // Ensure the child actually gets allocated space; otherwise the applet can
        // appear invisible even though it is present.
        gtk_widget_set_hexpand(drawing, TRUE);
        gtk_widget_set_vexpand(drawing, TRUE);

        // Initial size request (panel will call change-size later too).
        const guint size = mate_panel_applet_get_size(applet);
        on_change_size(applet, size, state);

        g_signal_connect(drawing, "draw", G_CALLBACK(on_draw), state);
        gtk_container_add(GTK_CONTAINER(applet), drawing);
        gtk_widget_show(drawing);

        g_signal_connect(applet, "change-size", G_CALLBACK(on_change_size), state);
        g_signal_connect(applet, "change-orient", G_CALLBACK(on_change_orient), state);
        g_signal_connect(applet, "destroy", G_CALLBACK(on_applet_destroy), state);

        // Integrate our menu items into the standard panel applet context menu.
        // Do this via idle callback to avoid UI manager timing issues.
        g_idle_add(setup_panel_menu_idle, state);

        // Initialize countdown + tooltip.
        state->next_refresh_us = g_get_monotonic_time() + (gint64)state->refresh_interval_s * 1000000;
        set_tooltip(state);

        // Initial fetch.
        start_fetch(state);

        // Refresh timer.
        state->refresh_timer_id = g_timeout_add_seconds(state->refresh_interval_s, on_refresh_timer, state);
        // Tooltip countdown updates.
        state->ui_tick_id = g_timeout_add_seconds(1, on_ui_tick, state);

        // In out-of-process mode, the toplevel is typically a GtkPlug.
        GtkWidget* toplevel = gtk_widget_get_toplevel(GTK_WIDGET(applet));
        if (GTK_IS_PLUG(toplevel)) {
            panel_log("toplevel is GtkPlug id=0x%x", (unsigned int)gtk_plug_get_id(GTK_PLUG(toplevel)));
        } else {
            panel_log("toplevel type=%s", G_OBJECT_TYPE_NAME(toplevel));
        }

        // Important: show the toplevel (plug) so the panel can embed/map it.
        gtk_widget_show(toplevel);
        gtk_widget_show(GTK_WIDGET(applet));

        panel_log("applet_show size=%u", size);
        return TRUE;
    }

    return FALSE;
}

static gboolean applet_fill_cb(MatePanelApplet* applet, const gchar* iid, gpointer user_data) {
    (void)user_data;
    return applet_fill(applet, iid, nullptr);
}

MATE_PANEL_APPLET_OUT_PROCESS_FACTORY(kFactoryId, PANEL_TYPE_APPLET, "FirmwareQuotaAppletFactory", applet_fill_cb, nullptr)
