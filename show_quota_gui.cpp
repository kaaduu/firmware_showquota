// =============================================================================
// show_quota_gui.cpp - GUI-only version of Firmware API Quota Viewer
// =============================================================================
// This version requires GTK3 and related libraries
// Build: g++ -std=c++17 -O2 -o show_quota_gui show_quota_gui.cpp quota_common.cpp \
//        $(pkg-config --cflags --libs gtk+-3.0 ayatana-appindicator3-0.1 libnotify) -lcurl -lpthread
// =============================================================================

#include "quota_common.h"
#include <algorithm>
#include <libgen.h>
#include <linux/limits.h>

extern "C" {
#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>
#include <libnotify/notify.h>
}
#include <pthread.h>

// ============================================================================
// GUI Types and Structures
// ============================================================================

// Single resizable window mode with 150px default width (140px minimum).

// Structure to hold GUI state
struct GUIState {
    // GTK Widgets
    GtkWidget* window;
    GtkWidget* root_container;
    GtkWidget* usage_progress;
    GtkWidget* reset_progress;
    GtkWidget* usage_label;
    GtkWidget* reset_label;
    GtkWidget* timestamp_label;
    GtkWidget* gauge_drawing_area;

    // CSS providers
    GtkCssProvider* theme_provider;

    // System Tray
    AppIndicator* indicator;
    GtkWidget* tray_menu;
    GtkWidget* refresh_15_item;
    GtkWidget* refresh_30_item;
    GtkWidget* refresh_60_item;
    GtkWidget* refresh_120_item;
    GtkWidget* autostart_item;
    GtkWidget* titlebar_item;
    GtkWidget* darkmode_item;
    GtkWidget* barwidth_1x_item;
    GtkWidget* barwidth_2x_item;
    GtkWidget* barwidth_3x_item;
    GtkWidget* barwidth_4x_item;

    // Application State
    std::string api_key;
    std::string token;
    std::string log_file;
    bool logging_enabled;
    int refresh_interval;
    int bar_height_multiplier;  // Progress bar height multiplier (1x, 2x, 3x, 4x)
    std::optional<AuthMethod> preferred_auth_method;

    // Current Data
    QuotaData current_quota;
    double prev_percentage;
    bool have_prev_percentage;
    std::string event_type;

    // Update Timer
    guint timer_id;

    // Window State
    int window_x;
    int window_y;
    int window_w;
    bool window_visible;
    bool always_on_top;
    bool window_decorated;
    bool dark_mode;

    // Restore state (needed because some WMs emit an initial configure-event at
    // 0,0 while mapping, which would otherwise clobber the saved position).
    int restore_x;
    int restore_y;
    int restore_w;
    bool have_restore_pos;
    bool have_restore_size;
    bool restoring;

    // Constructor with defaults
    GUIState() : window(nullptr), root_container(nullptr), usage_progress(nullptr), reset_progress(nullptr),
                 usage_label(nullptr), reset_label(nullptr), timestamp_label(nullptr),
                 gauge_drawing_area(nullptr), theme_provider(nullptr),
                 indicator(nullptr), tray_menu(nullptr),
                 refresh_15_item(nullptr), refresh_30_item(nullptr),
                 refresh_60_item(nullptr), refresh_120_item(nullptr),
                 autostart_item(nullptr), titlebar_item(nullptr), darkmode_item(nullptr),
                 barwidth_1x_item(nullptr), barwidth_2x_item(nullptr),
                 barwidth_3x_item(nullptr), barwidth_4x_item(nullptr),
                 logging_enabled(true), refresh_interval(15), bar_height_multiplier(1),
                 timer_id(0), window_x(-1), window_y(-1), window_w(-1), window_visible(true),
                 always_on_top(false), window_decorated(true), dark_mode(false),
                 restore_x(-1), restore_y(-1), restore_w(-1),
                 have_restore_pos(false), have_restore_size(false), restoring(false) {
        current_quota.used = 0.0;
        current_quota.percentage = 0.0;
        current_quota.reset_time = "";
        current_quota.timestamp = 0;

        prev_percentage = 0.0;
        have_prev_percentage = false;
    }
};

static double clamp_pct(double v) {
    if (v < 0.0) return 0.0;
    if (v > 100.0) return 100.0;
    return v;
}

static void color_for_usage_pct(double pct, double* r, double* g, double* b) {
    // Match terminal thresholds.
    if (pct < 50.0) {
        // #4caf50
        *r = 0x4c / 255.0;
        *g = 0xaf / 255.0;
        *b = 0x50 / 255.0;
        return;
    }
    if (pct < 80.0) {
        // #ff9800
        *r = 0xff / 255.0;
        *g = 0x98 / 255.0;
        *b = 0x00 / 255.0;
        return;
    }
    // #f44336
    *r = 0xf4 / 255.0;
    *g = 0x43 / 255.0;
    *b = 0x36 / 255.0;
}

static gboolean on_usage_bar_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    GUIState* state = (GUIState*)user_data;
    if (!state) return FALSE;

    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);
    const int w = a.width;
    const int h = a.height;
    if (w <= 0 || h <= 0) return FALSE;

    const double pct = clamp_pct(state->current_quota.percentage);
    const double prev = state->have_prev_percentage ? clamp_pct(state->prev_percentage) : pct;
    const double delta = (pct > prev) ? (pct - prev) : 0.0;

    // Colors
    double fill_r = 0.0, fill_g = 0.0, fill_b = 0.0;
    color_for_usage_pct(pct, &fill_r, &fill_g, &fill_b);

    // Delta cap (accent): #03a9f4
    const double delta_r = 0x03 / 255.0;
    const double delta_g = 0xa9 / 255.0;
    const double delta_b = 0xf4 / 255.0;

    // Trough and border depend on theme.
    double trough_r, trough_g, trough_b;
    double border_r, border_g, border_b;
    if (state->dark_mode) {
        // #2b2d31 / #3a3d44
        trough_r = 0x2b / 255.0;
        trough_g = 0x2d / 255.0;
        trough_b = 0x31 / 255.0;
        border_r = 0x3a / 255.0;
        border_g = 0x3d / 255.0;
        border_b = 0x44 / 255.0;
    } else {
        // #e5e7eb / #cbd5e1
        trough_r = 0xe5 / 255.0;
        trough_g = 0xe7 / 255.0;
        trough_b = 0xeb / 255.0;
        border_r = 0xcb / 255.0;
        border_g = 0xd5 / 255.0;
        border_b = 0xe1 / 255.0;
    }

    // Geometry
    const double pad = 1.0;
    const double x0 = pad;
    const double y0 = pad;
    const double bw = std::max(0.0, (double)w - 2.0 * pad);
    const double bh = std::max(0.0, (double)h - 2.0 * pad);

    // Clear
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    // Trough
    cairo_set_source_rgb(cr, trough_r, trough_g, trough_b);
    cairo_rectangle(cr, x0, y0, bw, bh);
    cairo_fill(cr);

    // Main fill
    const double fill_w = bw * (pct / 100.0);
    if (fill_w > 0.0) {
        cairo_set_source_rgb(cr, fill_r, fill_g, fill_b);
        cairo_rectangle(cr, x0, y0, fill_w, bh);
        cairo_fill(cr);
    }

    // Delta overlay (only newly-added segment since last refresh)
    if (delta > 0.0 && bw > 0.0) {
        double start_px = bw * (prev / 100.0);
        double end_px = fill_w;
        if (end_px > start_px) {
            const double min_px = 2.0;
            if (end_px - start_px < min_px) {
                start_px = std::max(0.0, end_px - min_px);
            }

            cairo_set_source_rgb(cr, delta_r, delta_g, delta_b);
            cairo_rectangle(cr, x0 + start_px, y0, end_px - start_px, bh);
            cairo_fill(cr);
        }
    }

    // Border
    cairo_set_source_rgb(cr, border_r, border_g, border_b);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, x0 + 0.5, y0 + 0.5, std::max(0.0, bw - 1.0), std::max(0.0, bh - 1.0));
    cairo_stroke(cr);

    return FALSE;
}

// ============================================================================
// Forward Declarations
// ============================================================================

static GtkWidget* create_main_window(GUIState* state);
static void update_gui_widgets(GUIState* state, const QuotaData* data);
static void update_tray_display(GUIState* state, const QuotaData* data);
static void show_desktop_notification(const std::string& event, double percentage);
static void save_gui_state(const GUIState* state);
static gboolean on_timer_update(gpointer user_data);
static void on_tray_reset_position(GtkMenuItem* item, gpointer user_data);
static gboolean on_window_map(GtkWidget* widget, GdkEvent* event, gpointer user_data);
static void on_toggle_autostart(GtkCheckMenuItem* item, gpointer user_data);
static void on_toggle_titlebar(GtkCheckMenuItem* item, gpointer user_data);
static gboolean on_window_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
static void on_toggle_dark_mode(GtkCheckMenuItem* item, gpointer user_data);

static bool get_primary_monitor_workarea(GdkRectangle* out_workarea);
static void move_window_to_primary_monitor(GUIState* state);

static std::string get_executable_dir();
static std::string get_wrapper_path();
static std::string get_autostart_desktop_path();
static bool is_autostart_enabled();
static bool set_autostart_enabled(bool enabled);

static void apply_window_theme(GUIState* state);

// ============================================================================
// Utility Functions
// ============================================================================

static int clamp_saved_width(int w) {
    // Minimum usable width
    w = std::max(w, 140);

    // Best-effort cap to primary monitor workarea width
    GdkRectangle wa;
    if (get_primary_monitor_workarea(&wa) && wa.width > 80) {
        w = std::min(w, wa.width - 40);
    } else {
        // Fallback cap
        w = std::min(w, 2000);
    }

    return w;
}

// ============================================================================
// CSS Styling
// ============================================================================

// Apply CSS styling for color-coded progress bars
static void apply_css_styling() {
    // Legacy: progressbar styling. The usage bar is now custom-drawn.
}

static void apply_window_theme(GUIState* state) {
    if (!state || !state->window) return;

    // Create provider lazily; it follows the window across recreations.
    if (!state->theme_provider) {
        state->theme_provider = gtk_css_provider_new();
    }

    // Apply to the window/root container so we can theme background and border.
    GtkStyleContext* wctx = gtk_widget_get_style_context(state->window);
    gtk_style_context_add_provider(
        wctx,
        GTK_STYLE_PROVIDER(state->theme_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    if (state->root_container) {
        GtkStyleContext* rctx = gtk_widget_get_style_context(state->root_container);
        gtk_style_context_add_provider(
            rctx,
            GTK_STYLE_PROVIDER(state->theme_provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    }

    // Use style classes rather than global selectors.
    gtk_style_context_remove_class(wctx, "quota-dark");
    gtk_style_context_remove_class(wctx, "quota-light");
    gtk_style_context_remove_class(wctx, "quota-borderless");

    if (state->dark_mode) {
        gtk_style_context_add_class(wctx, "quota-dark");
    } else {
        gtk_style_context_add_class(wctx, "quota-light");
    }
    if (!state->window_decorated) {
        gtk_style_context_add_class(wctx, "quota-borderless");
    }

    const char* css_light =
        "window.quota-light { background-color: #ffffff; color: #111111; } "
        "window.quota-light label { color: #111111; } "
        "window.quota-light progressbar text { color: #111111; } "
        "window.quota-light.quota-borderless { border: 2px solid #000000; border-radius: 8px; } ";

    const char* css_dark =
        "window.quota-dark { background-color: #1e1f22; color: #e6e6e6; } "
        "window.quota-dark label { color: #e6e6e6; } "
        "window.quota-dark progressbar text { color: #e6e6e6; } "
        "window.quota-dark progressbar trough { background-color: #2b2d31; } "
        "window.quota-dark.quota-borderless { border: 2px solid #000000; border-radius: 8px; } ";

    // Load combined CSS; class selection controls which applies.
    std::string css = std::string(css_light) + css_dark;
    gtk_css_provider_load_from_data(state->theme_provider, css.c_str(), -1, nullptr);
}

// Update widget colors based on percentage threshold
[[maybe_unused]] static void update_widget_colors(GtkWidget* progress, double percentage) {
    // Custom usage bar handles its own coloring.
    if (!GTK_IS_PROGRESS_BAR(progress)) {
        return;
    }
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

// NOTE: left for backwards compatibility if we ever reintroduce GtkProgressBar.

// ============================================================================
// Window Event Handlers
// ============================================================================

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
    if (state->restoring) {
        return FALSE;
    }
    state->window_x = event->x;
    state->window_y = event->y;
    // Track width for resizable mode.
    state->window_w = event->width;
    return FALSE;
}

struct MoveWindowData {
    GUIState* state;
    int x;
    int y;
    int w;
};

static gboolean move_window_to_saved_position_idle(gpointer user_data) {
    MoveWindowData* d = (MoveWindowData*)user_data;
    if (!d || !d->state || !d->state->window) {
        delete d;
        return G_SOURCE_REMOVE;
    }

    // Apply saved width before move so the off-screen check uses correct geometry
    if (d->w > 0) {
        const int w = clamp_saved_width(d->w);
        gtk_window_resize(GTK_WINDOW(d->state->window), w, 50);
    }

    gtk_window_move(GTK_WINDOW(d->state->window), d->x, d->y);

    // Validate against current monitor layout - check if window intersects ANY monitor
    GdkDisplay* display = gdk_display_get_default();
    if (display) {
        int w = 0;
        int h = 0;
        gtk_window_get_size(GTK_WINDOW(d->state->window), &w, &h);
        
        GdkRectangle window_rect = {d->x, d->y, w, h};
        bool on_some_monitor = false;
        int n_monitors = gdk_display_get_n_monitors(display);
        
        for (int i = 0; i < n_monitors && !on_some_monitor; i++) {
            GdkMonitor* mon = gdk_display_get_monitor(display, i);
            if (!mon) continue;
            
            GdkRectangle mon_rect;
            gdk_monitor_get_geometry(mon, &mon_rect);
            
            // Check if window intersects with this monitor
            if (gdk_rectangle_intersect(&window_rect, &mon_rect, nullptr)) {
                on_some_monitor = true;
            }
        }
        
        if (!on_some_monitor) {
            move_window_to_primary_monitor(d->state);
            d->state->restoring = false;
            save_gui_state(d->state);
            delete d;
            return G_SOURCE_REMOVE;
        }
    }

    d->state->window_x = d->x;
    d->state->window_y = d->y;
    if (d->w > 0) {
        d->state->window_w = d->w;
    }
    d->state->restoring = false;

    delete d;
    return G_SOURCE_REMOVE;
}

// Data for delayed titlebar toggle restoration
struct TitlebarToggleData {
    GUIState* state;
    int target_x;
    int target_y;
    int target_w;
    int target_h;
    int retry_count;
};

// Idle callback to finalize titlebar toggle - ensures size is correct after WM processes decoration change
static gboolean finalize_titlebar_toggle_idle(gpointer user_data) {
    TitlebarToggleData* d = (TitlebarToggleData*)user_data;
    if (!d || !d->state || !d->state->window) {
        delete d;
        return G_SOURCE_REMOVE;
    }

    // Check current size and position
    int current_w = 0, current_h = 0;
    int current_x = 0, current_y = 0;
    gtk_window_get_size(GTK_WINDOW(d->state->window), &current_w, &current_h);
    gtk_window_get_position(GTK_WINDOW(d->state->window), &current_x, &current_y);

    bool size_mismatch = (current_w != d->target_w);
    bool pos_mismatch = (current_x != d->target_x || current_y != d->target_y);

    // If either doesn't match, correct and possibly retry
    if (size_mismatch || pos_mismatch) {
        if (size_mismatch) {
            gtk_window_resize(GTK_WINDOW(d->state->window), d->target_w, d->target_h);
        }
        if (pos_mismatch) {
            gtk_window_move(GTK_WINDOW(d->state->window), d->target_x, d->target_y);
        }

        // Retry with delay to give WM time to settle (some WMs are slow)
        if (d->retry_count < 10) {
            d->retry_count++;
            // Reschedule with delay instead of immediate continue
            g_timeout_add(30, finalize_titlebar_toggle_idle, d);  // 30ms between retries
            return G_SOURCE_REMOVE;  // Remove this instance, new one scheduled
        }
    }

    // Update state with our target values (what we want, not what WM gave us)
    d->state->window_x = d->target_x;
    d->state->window_y = d->target_y;
    d->state->window_w = d->target_w;

    // Unblock configure events
    d->state->restoring = false;

    // Save state with correct values
    save_gui_state(d->state);

    delete d;
    return G_SOURCE_REMOVE;
}

static gboolean on_window_map(GtkWidget* widget, GdkEvent* event, gpointer user_data) {
    (void)widget;
    (void)event;
    GUIState* state = (GUIState*)user_data;
    if (!state || !state->window) return FALSE;

    // Don't interfere during titlebar toggle or other restoration operations
    if (state->restoring) return FALSE;

    if (!state->have_restore_pos) {
        return FALSE;
    }

    // On Marco (MATE) the WM may ignore pre-map gtk_window_move(). Apply the
    // saved position in an idle callback after the map.
    // Set restoring flag to block configure events from clobbering position during restore
    state->restoring = true;
    
    MoveWindowData* d = new MoveWindowData();
    d->state = state;
    d->x = state->restore_x;
    d->y = state->restore_y;
    d->w = state->have_restore_size ? state->restore_w : -1;
    g_idle_add(move_window_to_saved_position_idle, d);
    return FALSE;
}

// ============================================================================
// Tray Menu Callbacks
// ============================================================================

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
    // Best-effort persist latest position/mode before exit.
    save_gui_state((GUIState*)user_data);
    gtk_main_quit();
}

static bool get_primary_monitor_workarea(GdkRectangle* out_workarea) {
    if (!out_workarea) return false;
    std::memset(out_workarea, 0, sizeof(*out_workarea));

    GdkDisplay* display = gdk_display_get_default();
    if (!display) return false;

    GdkMonitor* monitor = gdk_display_get_primary_monitor(display);
    if (!monitor) {
        monitor = gdk_display_get_monitor(display, 0);
    }
    if (!monitor) return false;

    gdk_monitor_get_workarea(monitor, out_workarea);
    return out_workarea->width > 0 && out_workarea->height > 0;
}

static std::string get_executable_dir() {
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) {
        return ".";
    }
    buf[len] = '\0';
    // Get directory part
    char* dir = dirname(buf);
    return dir ? std::string(dir) : ".";
}

static std::string get_wrapper_path() {
    std::string dir = get_executable_dir();
    return dir + "/show_quota_wrapper.sh";
}

static std::string get_autostart_desktop_path() {
    const char* home = getenv("HOME");
    if (!home) return std::string();
    // Unified autostart file for all versions
    return std::string(home) + "/.config/autostart/firmware_quota.desktop";
}

static bool is_autostart_enabled() {
    std::string path = get_autostart_desktop_path();
    if (path.empty()) return false;

    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("X-GNOME-Autostart-enabled=", 0) == 0) {
            std::string v = line.substr(std::strlen("X-GNOME-Autostart-enabled="));
            return (v == "true" || v == "True" || v == "1");
        }
    }

    // If the file exists but the key is missing, treat as enabled.
    return true;
}

static bool ensure_autostart_dir_exists() {
    const char* home = getenv("HOME");
    if (!home) return false;
    std::string dir = std::string(home) + "/.config/autostart";

    struct stat st;
    if (stat(dir.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    if (mkdir(dir.c_str(), 0755) != 0) {
        return false;
    }
    return true;
}

static bool set_autostart_enabled(bool enabled) {
    std::string path = get_autostart_desktop_path();
    if (path.empty()) return false;
    if (!ensure_autostart_dir_exists()) return false;

    std::string wrapper_path = get_wrapper_path();

    // Always write a small deterministic .desktop file.
    // Use the wrapper script with --use-gui to ensure GUI mode on autostart.
    std::ofstream f(path);
    if (!f.is_open()) return false;

    f << "[Desktop Entry]\n";
    f << "Type=Application\n";
    f << "Name=Firmware Quota\n";
    f << "Comment=Firmware API Quota Monitor\n";
    f << "Exec=" << wrapper_path << " --use-gui\n";
    f << "Terminal=false\n";
    f << "X-GNOME-Autostart-enabled=" << (enabled ? "true" : "false") << "\n";
    f.close();
    return true;
}

static void on_toggle_autostart(GtkCheckMenuItem* item, gpointer user_data) {
    GUIState* state = (GUIState*)user_data;
    if (!state) return;
    const bool enabled = gtk_check_menu_item_get_active(item);

    // Best-effort: if it fails, revert the checkbox.
    if (!set_autostart_enabled(enabled)) {
        g_signal_handlers_block_by_func(item, (void*)on_toggle_autostart, state);
        gtk_check_menu_item_set_active(item, !enabled);
        g_signal_handlers_unblock_by_func(item, (void*)on_toggle_autostart, state);
    }
}

static void on_toggle_titlebar(GtkCheckMenuItem* item, gpointer user_data) {
    GUIState* state = (GUIState*)user_data;
    if (!state) return;
    state->window_decorated = gtk_check_menu_item_get_active(item);
    if (state->window) {
        // Save current position AND size BEFORE toggling decoration (WM may move/resize window)
        int saved_x, saved_y, saved_w, saved_h;
        gtk_window_get_position(GTK_WINDOW(state->window), &saved_x, &saved_y);
        gtk_window_get_size(GTK_WINDOW(state->window), &saved_w, &saved_h);

        // Block configure events from recording WM-initiated position/size changes
        // Will be unblocked by the idle callback after WM finishes
        state->restoring = true;

        gtk_window_set_decorated(GTK_WINDOW(state->window), state->window_decorated ? TRUE : FALSE);

        // Initial restore attempt (may be overridden by WM)
        gtk_window_move(GTK_WINDOW(state->window), saved_x, saved_y);
        gtk_window_resize(GTK_WINDOW(state->window), saved_w, saved_h);

        apply_window_theme(state);

        // Schedule delayed callback to finalize the toggle after WM processes the change
        // Use timeout instead of idle to give WM time to finish decoration change
        TitlebarToggleData* d = new TitlebarToggleData();
        d->state = state;
        d->target_x = saved_x;
        d->target_y = saved_y;
        d->target_w = saved_w;
        d->target_h = saved_h;
        d->retry_count = 0;
        g_timeout_add(50, finalize_titlebar_toggle_idle, d);  // 50ms delay
    } else {
        apply_window_theme(state);
        save_gui_state(state);
    }
}

static void on_toggle_dark_mode(GtkCheckMenuItem* item, gpointer user_data) {
    GUIState* state = (GUIState*)user_data;
    if (!state) return;
    state->dark_mode = gtk_check_menu_item_get_active(item);
    apply_window_theme(state);
    save_gui_state(state);
}

// Allow dragging the window by clicking anywhere when decorations are disabled.
// Also handles right-click to show context menu.
static gboolean on_window_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    (void)widget;
    GUIState* state = (GUIState*)user_data;
    if (!state || !state->window) return FALSE;

    if (!event) return FALSE;

    // Right-click shows context menu (same as tray menu)
    if (event->button == 3 && event->type == GDK_BUTTON_PRESS) {
        if (state->tray_menu) {
            gtk_menu_popup_at_pointer(GTK_MENU(state->tray_menu), (GdkEvent*)event);
            return TRUE;
        }
        return FALSE;
    }

    // Left-click handling
    if (event->button != 1) return FALSE;

    // Double-click toggles title bar on/off.
    if (event->type == GDK_2BUTTON_PRESS) {
        // Save current position AND size BEFORE toggling decoration (WM may move/resize window)
        int saved_x, saved_y, saved_w, saved_h;
        gtk_window_get_position(GTK_WINDOW(state->window), &saved_x, &saved_y);
        gtk_window_get_size(GTK_WINDOW(state->window), &saved_w, &saved_h);

        // Block configure events from recording WM-initiated position/size changes
        // Will be unblocked by the idle callback after WM finishes
        state->restoring = true;

        state->window_decorated = !state->window_decorated;
        gtk_window_set_decorated(GTK_WINDOW(state->window), state->window_decorated ? TRUE : FALSE);

        // Initial restore attempt (may be overridden by WM)
        gtk_window_move(GTK_WINDOW(state->window), saved_x, saved_y);
        gtk_window_resize(GTK_WINDOW(state->window), saved_w, saved_h);

        apply_window_theme(state);

        // Update menu checkbox
        if (state->titlebar_item) {
            g_signal_handlers_block_by_func(state->titlebar_item, (void*)on_toggle_titlebar, state);
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->titlebar_item), state->window_decorated ? TRUE : FALSE);
            g_signal_handlers_unblock_by_func(state->titlebar_item, (void*)on_toggle_titlebar, state);
        }

        // Schedule delayed callback to finalize the toggle after WM processes the change
        // Use timeout instead of idle to give WM time to finish decoration change
        TitlebarToggleData* d = new TitlebarToggleData();
        d->state = state;
        d->target_x = saved_x;
        d->target_y = saved_y;
        d->target_w = saved_w;
        d->target_h = saved_h;
        d->retry_count = 0;
        g_timeout_add(50, finalize_titlebar_toggle_idle, d);  // 50ms delay

        return TRUE;
    }

    // Single click-drag moves the window when decorations are disabled.
    if (state->window_decorated) return FALSE;
    if (event->type != GDK_BUTTON_PRESS) return FALSE;
    gtk_window_begin_move_drag(GTK_WINDOW(state->window),
                               (gint)event->button,
                               (gint)event->x_root,
                               (gint)event->y_root,
                               (guint32)event->time);
    return TRUE;
}

static void move_window_to_primary_monitor(GUIState* state) {
    if (!state || !state->window) return;

    GdkRectangle wa;
    if (!get_primary_monitor_workarea(&wa)) {
        // Fallback: let WM place it.
        gtk_window_present(GTK_WINDOW(state->window));
        return;
    }

    const int x = wa.x + 20;
    const int y = wa.y + 20;
    gtk_window_move(GTK_WINDOW(state->window), x, y);

    state->window_x = x;
    state->window_y = y;
}

static void on_tray_reset_position(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    GUIState* state = (GUIState*)user_data;
    if (!state || !state->window) return;

    state->window_visible = true;
    gtk_widget_show_all(state->window);
    move_window_to_primary_monitor(state);
    gtk_window_present(GTK_WINDOW(state->window));
    save_gui_state(state);
}

static void on_save_position(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    GUIState* state = (GUIState*)user_data;
    if (!state) return;
    save_gui_state(state);
}

// ============================================================================
// Refresh Rate Management
// ============================================================================

// Refresh rate change callbacks
static void change_refresh_rate(GUIState* state, int new_interval) {
    state->refresh_interval = new_interval;

    // Update checkmarks on menu items
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->refresh_15_item), new_interval == 15);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->refresh_30_item), new_interval == 30);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->refresh_60_item), new_interval == 60);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->refresh_120_item), new_interval == 120);

    // Remove old timer
    if (state->timer_id > 0) {
        g_source_remove(state->timer_id);
    }

    // Create new timer with new interval
    state->timer_id = g_timeout_add(
        new_interval * 1000,
        on_timer_update,
        state
    );

    // Save preference
    save_gui_state(state);

    // Trigger immediate update
    on_timer_update(state);
}

static void on_refresh_15s(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    change_refresh_rate((GUIState*)user_data, 15);
}

static void on_refresh_30s(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    change_refresh_rate((GUIState*)user_data, 30);
}

static void on_refresh_60s(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    change_refresh_rate((GUIState*)user_data, 60);
}

static void on_refresh_120s(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    change_refresh_rate((GUIState*)user_data, 120);
}

// ============================================================================
// Progress Bar Height Management
// ============================================================================

// Forward declarations for bar width callbacks
static void on_barwidth_1x(GtkMenuItem* item, gpointer user_data);
static void on_barwidth_2x(GtkMenuItem* item, gpointer user_data);
static void on_barwidth_3x(GtkMenuItem* item, gpointer user_data);
static void on_barwidth_4x(GtkMenuItem* item, gpointer user_data);

// Progress bar height change callbacks
static void change_bar_height(GUIState* state, int multiplier) {
    state->bar_height_multiplier = multiplier;

    // Block signals to prevent recursive callbacks
    g_signal_handlers_block_by_func(state->barwidth_1x_item, (void*)on_barwidth_1x, state);
    g_signal_handlers_block_by_func(state->barwidth_2x_item, (void*)on_barwidth_2x, state);
    g_signal_handlers_block_by_func(state->barwidth_3x_item, (void*)on_barwidth_3x, state);
    g_signal_handlers_block_by_func(state->barwidth_4x_item, (void*)on_barwidth_4x, state);

    // Update checkmarks on menu items
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->barwidth_1x_item), multiplier == 1);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->barwidth_2x_item), multiplier == 2);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->barwidth_3x_item), multiplier == 3);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->barwidth_4x_item), multiplier == 4);

    // Unblock signals
    g_signal_handlers_unblock_by_func(state->barwidth_1x_item, (void*)on_barwidth_1x, state);
    g_signal_handlers_unblock_by_func(state->barwidth_2x_item, (void*)on_barwidth_2x, state);
    g_signal_handlers_unblock_by_func(state->barwidth_3x_item, (void*)on_barwidth_3x, state);
    g_signal_handlers_unblock_by_func(state->barwidth_4x_item, (void*)on_barwidth_4x, state);

    // Recreate window so the new CSS min-height is picked up reliably.
    // Preserve the exact window position and visibility.
    bool was_visible = state->window_visible;

    int current_x = state->window_x;
    int current_y = state->window_y;
    if (state->window) {
        gtk_window_get_position(GTK_WINDOW(state->window), &current_x, &current_y);
        state->window_x = current_x;
        state->window_y = current_y;
        gtk_widget_destroy(state->window);
        state->window = nullptr;
    }

    state->window = create_main_window(state);

    if (current_x >= 0 && current_y >= 0) {
        gtk_window_move(GTK_WINDOW(state->window), current_x, current_y);
    }

    if (state->always_on_top) {
        gtk_window_set_keep_above(GTK_WINDOW(state->window), TRUE);
    }

    if (was_visible) {
        gtk_widget_show_all(state->window);
        state->window_visible = true;
        // Some WMs ignore pre-map moves; apply again after show.
        if (current_x >= 0 && current_y >= 0) {
            gtk_window_move(GTK_WINDOW(state->window), current_x, current_y);
        }
        gtk_window_present(GTK_WINDOW(state->window));
    } else {
        gtk_widget_realize(state->window);
        state->window_visible = false;
        if (current_x >= 0 && current_y >= 0) {
            gtk_window_move(GTK_WINDOW(state->window), current_x, current_y);
        }
    }

    // Update with current data
    if (state->current_quota.timestamp > 0) {
        update_gui_widgets(state, &state->current_quota);
        update_tray_display(state, &state->current_quota);
    }

    // Save preference
    save_gui_state(state);
}

static void on_barwidth_1x(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    change_bar_height((GUIState*)user_data, 1);
}

static void on_barwidth_2x(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    change_bar_height((GUIState*)user_data, 2);
}

static void on_barwidth_3x(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    change_bar_height((GUIState*)user_data, 3);
}

static void on_barwidth_4x(GtkMenuItem* item, gpointer user_data) {
    (void)item;
    change_bar_height((GUIState*)user_data, 4);
}

// Always on top toggle callback
static void on_toggle_always_on_top(GtkCheckMenuItem* item, gpointer user_data) {
    GUIState* state = (GUIState*)user_data;
    state->always_on_top = gtk_check_menu_item_get_active(item);

    // Apply to current window
    if (state->window) {
        gtk_window_set_keep_above(GTK_WINDOW(state->window), state->always_on_top);
    }

    // Save preference
    save_gui_state(state);
}

// ============================================================================
// Window Creation
// ============================================================================

// Create main window with widgets
static GtkWidget* create_main_window(GUIState* state) {
    // Create window
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    // Window configuration - resizable with 150px default width
    const int width = 150;
    const int height = 50;
    const int border = 5;
    const int spacing = 3;
    const int bar_height = 10 * state->bar_height_multiplier;
    const int bar_width = -1;  // Follow window width
    const char* title = "Quota";
    const bool show_frames = false;
    const bool show_timestamp = false;
    const bool width_follows_window = true;

    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_window_set_default_size(GTK_WINDOW(window), width, height);
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(window), border);
    gtk_window_set_decorated(GTK_WINDOW(window), state->window_decorated ? TRUE : FALSE);

    // Restrict to horizontal resizing only
    GdkGeometry geom;
    std::memset(&geom, 0, sizeof(geom));
    geom.min_width = 140;
    geom.max_width = G_MAXINT;
    geom.min_height = height;
    geom.max_height = height;
    gtk_window_set_geometry_hints(GTK_WINDOW(window), window, &geom,
                                 (GdkWindowHints)(GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE));

    // Create vertical box layout
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing);
    state->root_container = vbox;
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // Apply theme after root container exists (for borderless outline + dark mode)
    apply_window_theme(state);

    // Usage section
    if (show_frames) {
        GtkWidget* usage_frame = gtk_frame_new("Quota Usage");
        gtk_box_pack_start(GTK_BOX(vbox), usage_frame, FALSE, FALSE, 0);
        GtkWidget* usage_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_set_border_width(GTK_CONTAINER(usage_vbox), 10);
        gtk_container_add(GTK_CONTAINER(usage_frame), usage_vbox);

        state->usage_progress = gtk_drawing_area_new();
        if (!width_follows_window && bar_width > 0) {
            gtk_widget_set_size_request(state->usage_progress, bar_width, bar_height);
        } else {
            gtk_widget_set_size_request(state->usage_progress, -1, bar_height);
            gtk_widget_set_hexpand(state->usage_progress, TRUE);
        }
        g_signal_connect(state->usage_progress, "draw", G_CALLBACK(on_usage_bar_draw), state);

        gtk_box_pack_start(GTK_BOX(usage_vbox), state->usage_progress, FALSE, FALSE, 0);

        state->usage_label = gtk_label_new("Initializing...");
        gtk_label_set_xalign(GTK_LABEL(state->usage_label), 0.0);
        gtk_box_pack_start(GTK_BOX(usage_vbox), state->usage_label, FALSE, FALSE, 0);
    } else {
        // No-frame layout (compact mode)
        state->usage_progress = gtk_drawing_area_new();
        if (!width_follows_window && bar_width > 0) {
            gtk_widget_set_size_request(state->usage_progress, bar_width, bar_height);
        } else {
            gtk_widget_set_size_request(state->usage_progress, -1, bar_height);
            gtk_widget_set_hexpand(state->usage_progress, TRUE);
        }
        g_signal_connect(state->usage_progress, "draw", G_CALLBACK(on_usage_bar_draw), state);

        gtk_box_pack_start(GTK_BOX(vbox), state->usage_progress, FALSE, FALSE, 0);

        state->usage_label = gtk_label_new("Initializing...");
        gtk_label_set_xalign(GTK_LABEL(state->usage_label), 0.0);
        gtk_box_pack_start(GTK_BOX(vbox), state->usage_label, FALSE, FALSE, 0);
    }

    // Reset countdown removed - time is now shown in usage label
    state->reset_progress = nullptr;
    state->reset_label = nullptr;

    // Timestamp label (only shown in framed mode)
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
    g_signal_connect(window, "map-event", G_CALLBACK(on_window_map), state);
    g_signal_connect(window, "button-press-event", G_CALLBACK(on_window_button_press), state);
    gtk_widget_add_events(window, GDK_BUTTON_PRESS_MASK);

    return window;
}

// ============================================================================
// System Tray
// ============================================================================

// Create system tray icon with context menu
static AppIndicator* create_system_tray(GUIState* state) {
    // Get the directory where the executable is located
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    std::string icon_theme_path = ".";  // Default to current directory

    if (len != -1) {
        exe_path[len] = '\0';
        char* exe_dir = dirname(exe_path);
        if (exe_dir) {
            icon_theme_path = exe_dir;
        }
    }

    // Try to use custom Firmware icon
    // AppIndicator will look for firmware-icon.svg or firmware-icon.png in the theme path
    const char* icon_name = "firmware-icon";

    AppIndicator* indicator = app_indicator_new(
        "firmware-quota-indicator",
        icon_name,
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS
    );

    // Set icon theme path to executable's directory so it finds firmware-icon.{svg,png}
    app_indicator_set_icon_theme_path(indicator, icon_theme_path.c_str());

    // Try to set icon explicitly using full path as fallback
    // This helps when AppIndicator's icon theme lookup fails
    std::string icon_full_path = icon_theme_path + "/firmware-icon";
    struct stat buffer;
    if (stat((icon_full_path + ".png").c_str(), &buffer) == 0) {
        app_indicator_set_icon_full(indicator, (icon_full_path + ".png").c_str(), "Firmware Quota");
    } else if (stat((icon_full_path + ".svg").c_str(), &buffer) == 0) {
        app_indicator_set_icon_full(indicator, (icon_full_path + ".svg").c_str(), "Firmware Quota");
    }
    // If neither exists, AppIndicator will use a default icon

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

    GtkWidget* save_pos_item = gtk_menu_item_new_with_label("Save Position");
    g_signal_connect(save_pos_item, "activate", G_CALLBACK(on_save_position), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), save_pos_item);

    GtkWidget* reset_pos_item = gtk_menu_item_new_with_label("Reset Position");
    g_signal_connect(reset_pos_item, "activate", G_CALLBACK(on_tray_reset_position), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), reset_pos_item);

    // Autostart toggle (MATE honors ~/.config/autostart/*.desktop)
    state->autostart_item = gtk_check_menu_item_new_with_label("Auto-start on Login");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->autostart_item), is_autostart_enabled());
    g_signal_connect(state->autostart_item, "toggled", G_CALLBACK(on_toggle_autostart), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), state->autostart_item);

    // Title bar toggle
    state->titlebar_item = gtk_check_menu_item_new_with_label("Show Title Bar");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->titlebar_item), state->window_decorated);
    g_signal_connect(state->titlebar_item, "toggled", G_CALLBACK(on_toggle_titlebar), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), state->titlebar_item);

    // Dark mode toggle
    state->darkmode_item = gtk_check_menu_item_new_with_label("Dark Mode");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->darkmode_item), state->dark_mode);
    g_signal_connect(state->darkmode_item, "toggled", G_CALLBACK(on_toggle_dark_mode), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), state->darkmode_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    // Refresh Rate submenu
    GtkWidget* refresh_item = gtk_menu_item_new_with_label("Refresh Rate");
    GtkWidget* refresh_submenu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(refresh_item), refresh_submenu);

    // Create radio group for refresh rate items
    GSList* refresh_group = nullptr;

    state->refresh_15_item = gtk_radio_menu_item_new_with_label(refresh_group, "15 seconds");
    refresh_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(state->refresh_15_item));
    g_signal_connect(state->refresh_15_item, "activate", G_CALLBACK(on_refresh_15s), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(refresh_submenu), state->refresh_15_item);

    state->refresh_30_item = gtk_radio_menu_item_new_with_label(refresh_group, "30 seconds");
    refresh_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(state->refresh_30_item));
    g_signal_connect(state->refresh_30_item, "activate", G_CALLBACK(on_refresh_30s), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(refresh_submenu), state->refresh_30_item);

    state->refresh_60_item = gtk_radio_menu_item_new_with_label(refresh_group, "60 seconds");
    refresh_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(state->refresh_60_item));
    g_signal_connect(state->refresh_60_item, "activate", G_CALLBACK(on_refresh_60s), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(refresh_submenu), state->refresh_60_item);

    state->refresh_120_item = gtk_radio_menu_item_new_with_label(refresh_group, "120 seconds");
    g_signal_connect(state->refresh_120_item, "activate", G_CALLBACK(on_refresh_120s), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(refresh_submenu), state->refresh_120_item);

    // Set initial active state based on current refresh_interval
    if (state->refresh_interval == 15) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->refresh_15_item), TRUE);
    } else if (state->refresh_interval == 30) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->refresh_30_item), TRUE);
    } else if (state->refresh_interval == 60) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->refresh_60_item), TRUE);
    } else if (state->refresh_interval == 120) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->refresh_120_item), TRUE);
    }

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), refresh_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    // Progress Bar Height submenu
    GtkWidget* barwidth_item = gtk_menu_item_new_with_label("Progress Bar Height");
    GtkWidget* barwidth_submenu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(barwidth_item), barwidth_submenu);

    // Create radio group for bar width items
    GSList* barwidth_group = nullptr;

    state->barwidth_1x_item = gtk_radio_menu_item_new_with_label(barwidth_group, "1x (Default)");
    barwidth_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(state->barwidth_1x_item));
    g_signal_connect(state->barwidth_1x_item, "activate", G_CALLBACK(on_barwidth_1x), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(barwidth_submenu), state->barwidth_1x_item);

    state->barwidth_2x_item = gtk_radio_menu_item_new_with_label(barwidth_group, "2x (Taller)");
    barwidth_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(state->barwidth_2x_item));
    g_signal_connect(state->barwidth_2x_item, "activate", G_CALLBACK(on_barwidth_2x), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(barwidth_submenu), state->barwidth_2x_item);

    state->barwidth_3x_item = gtk_radio_menu_item_new_with_label(barwidth_group, "3x (Tallest)");
    barwidth_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(state->barwidth_3x_item));
    g_signal_connect(state->barwidth_3x_item, "activate", G_CALLBACK(on_barwidth_3x), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(barwidth_submenu), state->barwidth_3x_item);

    state->barwidth_4x_item = gtk_radio_menu_item_new_with_label(barwidth_group, "4x (Extra Tall)");
    g_signal_connect(state->barwidth_4x_item, "activate", G_CALLBACK(on_barwidth_4x), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(barwidth_submenu), state->barwidth_4x_item);

    // Set initial active state based on current bar_height_multiplier
    if (state->bar_height_multiplier == 1) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->barwidth_1x_item), TRUE);
    } else if (state->bar_height_multiplier == 2) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->barwidth_2x_item), TRUE);
    } else if (state->bar_height_multiplier == 3) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->barwidth_3x_item), TRUE);
    } else if (state->bar_height_multiplier == 4) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(state->barwidth_4x_item), TRUE);
    }

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), barwidth_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    // Always on Top checkbox
    GtkWidget* always_on_top_item = gtk_check_menu_item_new_with_label("Always on Top");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(always_on_top_item), state->always_on_top);
    g_signal_connect(always_on_top_item, "toggled", G_CALLBACK(on_toggle_always_on_top), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), always_on_top_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    GtkWidget* quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_tray_quit), state);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

    gtk_widget_show_all(menu);
    app_indicator_set_menu(indicator, GTK_MENU(menu));

    state->tray_menu = menu;

    return indicator;
}

// ============================================================================
// Notifications
// ============================================================================

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

// ============================================================================
// GUI Update Functions
// ============================================================================

// Update GUI widgets with quota data
static void update_gui_widgets(GUIState* state, const QuotaData* data) {
    // Usage bar is a custom drawn widget; redraw after updating state below.

    // Update usage label with time remaining
    char usage_text[256];
    if (data->reset_time != "N/A" && !data->reset_time.empty()) {
        time_t reset_utc;
        if (parse_iso8601_utc_to_time_t(data->reset_time, &reset_utc)) {
            time_t now = time(nullptr);
            int64_t remaining = static_cast<int64_t>(difftime(reset_utc, now));
            if (remaining < 0) remaining = 0;

            std::string duration_str = format_duration_compact(remaining);
            snprintf(usage_text, sizeof(usage_text), "%.2f%% (%.4f used) - Reset in %s",
                     data->percentage, data->used, duration_str.c_str());
        } else {
            snprintf(usage_text, sizeof(usage_text), "%.2f%% (%.4f used)",
                     data->percentage, data->used);
        }
    } else {
        snprintf(usage_text, sizeof(usage_text), "%.2f%% (%.4f used) - No active window (quota not used recently)",
                 data->percentage, data->used);
    }
    gtk_label_set_text(GTK_LABEL(state->usage_label), usage_text);

    // Color coding is handled by the custom bar draw callback.

    // Update timestamp (only if exists - not in compact mode)
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

    if (state->usage_progress) {
        gtk_widget_queue_draw(state->usage_progress);
    }
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
                     "Firmware Quota: %.1f%%\nReset: %s\nRefresh: %ds",
                     data->percentage,
                     duration_str.c_str(),
                     state->refresh_interval);
        } else {
            snprintf(tooltip, sizeof(tooltip),
                     "Firmware Quota: %.1f%%\nRefresh: %ds",
                     data->percentage,
                     state->refresh_interval);
        }
    } else {
        snprintf(tooltip, sizeof(tooltip),
                 "Firmware Quota: %.1f%%\nNo active window\nRefresh: %ds",
                 data->percentage,
                 state->refresh_interval);
    }

    app_indicator_set_title(state->indicator, tooltip);
}

// Show error in GUI
static void show_error_in_gui(GUIState* state, const char* message) {
    gtk_label_set_text(GTK_LABEL(state->usage_label), "Error fetching data");

    // Only set reset label if it exists (may be nullptr in compact mode)
    if (state->reset_label != nullptr) {
        gtk_label_set_text(GTK_LABEL(state->reset_label), message);
    }

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

// ============================================================================
// Background Fetch Thread
// ============================================================================

// Structure for passing data between threads
struct FetchThreadData {
    GUIState* state;
    RequestResult result;
    bool success;
    QuotaData quota_data;
    std::string event;
    std::optional<AuthMethod> used_method;
    std::string error_message;
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

    if (data->result.curl_code != CURLE_OK) {
        data->success = false;
        data->error_message = std::string("Request failed: ") + curl_easy_strerror(data->result.curl_code);
        if (!data->result.curl_error.empty()) {
            data->error_message += " (" + data->result.curl_error + ")";
        }
        g_idle_add(on_fetch_complete, data);
        return nullptr;
    }

    if (!is_http_success(data->result.http_code)) {
        data->success = false;
        data->error_message = "HTTP error: " + std::to_string(data->result.http_code);
        if (!data->result.body.empty()) {
            data->error_message += "\n" + truncate_for_display(data->result.body, 300);
        }
        g_idle_add(on_fetch_complete, data);
        return nullptr;
    }

    // Parse JSON (reuse existing code)
    try {
        json j = json::parse(data->result.body);

        if (!j.contains("used") || j["used"].is_null()) {
            data->success = false;
            data->error_message = "Failed to parse response (missing 'used').\n" + truncate_for_display(data->result.body, 300);
            g_idle_add(on_fetch_complete, data);
            return nullptr;
        }

        double used = j["used"].get<double>();
        std::string reset = (j.contains("reset") && !j["reset"].is_null()) ? j["reset"].get<std::string>() : "";

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
    } catch (const json::parse_error& e) {
        data->success = false;
        data->error_message = std::string("Failed to parse JSON: ") + e.what() + "\n" + truncate_for_display(data->result.body, 300);
    } catch (const std::exception& e) {
        data->success = false;
        data->error_message = std::string("Failed to parse response: ") + e.what() + "\n" + truncate_for_display(data->result.body, 300);
    } catch (...) {
        data->success = false;
        data->error_message = "Failed to parse response.\n" + truncate_for_display(data->result.body, 300);
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

        // Capture previous value so the bar can highlight the increase.
        if (data->state->current_quota.timestamp > 0) {
            data->state->prev_percentage = data->state->current_quota.percentage;
            data->state->have_prev_percentage = true;
        } else {
            data->state->prev_percentage = data->quota_data.percentage;
            data->state->have_prev_percentage = false;
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
        const char* msg = data->error_message.empty() ? "Failed to fetch quota data" : data->error_message.c_str();
        show_error_in_gui(data->state, msg);
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

// ============================================================================
// State Persistence
// ============================================================================

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
        } else if (key == "window_w") {
            state->window_w = std::atoi(value.c_str());
            if (state->window_w < 1) {
                state->window_w = -1;
            } else {
                state->window_w = clamp_saved_width(state->window_w);
            }
        } else if (key == "window_visible") {
            state->window_visible = (value == "1");
        } else if (key == "always_on_top") {
            state->always_on_top = (value == "1");
        } else if (key == "window_decorated") {
            state->window_decorated = (value == "1");
        } else if (key == "dark_mode") {
            state->dark_mode = (value == "1");
        } else if (key == "refresh_interval") {
            state->refresh_interval = std::atoi(value.c_str());
            if (state->refresh_interval < 1) {
                state->refresh_interval = 15;  // Sanity check
            }
        } else if (key == "bar_height_multiplier") {
            state->bar_height_multiplier = std::atoi(value.c_str());
            if (state->bar_height_multiplier < 1 || state->bar_height_multiplier > 4) {
                state->bar_height_multiplier = 2;  // Sanity check
            }
        }
        // Note: legacy gui_mode and mode_* keys are ignored for backwards compatibility
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

    // Persist the actual current window position (not just last configure-event).
    int saved_x = state->window_x;
    int saved_y = state->window_y;
    int saved_w = state->window_w;
    if (state->window) {
        gtk_window_get_position(GTK_WINDOW(state->window), &saved_x, &saved_y);
        int w = 0;
        int h = 0;
        gtk_window_get_size(GTK_WINDOW(state->window), &w, &h);
        saved_w = clamp_saved_width(w);
    }

    file << "window_x=" << saved_x << "\n";
    file << "window_y=" << saved_y << "\n";
    file << "window_w=" << saved_w << "\n";
    file << "window_visible=" << (state->window_visible ? "1" : "0") << "\n";
    file << "always_on_top=" << (state->always_on_top ? "1" : "0") << "\n";
    file << "window_decorated=" << (state->window_decorated ? "1" : "0") << "\n";
    file << "dark_mode=" << (state->dark_mode ? "1" : "0") << "\n";
    file << "refresh_interval=" << state->refresh_interval << "\n";
    file << "bar_height_multiplier=" << state->bar_height_multiplier << "\n";

    file.close();
}

// Restore window position and visibility
static void restore_window_position(GUIState* state) {
    // Apply always on top setting
    if (state->always_on_top) {
        gtk_window_set_keep_above(GTK_WINDOW(state->window), TRUE);
    }

    // Apply window decorations preference
    gtk_window_set_decorated(GTK_WINDOW(state->window), state->window_decorated ? TRUE : FALSE);

    apply_window_theme(state);

    if (state->window_visible) {
        gtk_widget_show_all(state->window);
    } else {
        // Start hidden, only tray icon visible
        gtk_widget_realize(state->window);
    }
}

// ============================================================================
// Print Usage
// ============================================================================

static void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [OPTIONS] [API_KEY]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "GUI-only version - requires GTK3 and related libraries." << std::endl;
    std::cerr << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --refresh <seconds>  Initial refresh interval (default: 15)" << std::endl;
    std::cerr << "  --log <file>         Log quota changes to CSV file (default: ./show_quota.log)" << std::endl;
    std::cerr << "  --no-log             Disable logging" << std::endl;
    std::cerr << "  --help               Show this help message" << std::endl;
    std::cerr << std::endl;
    std::cerr << "API Key:" << std::endl;
    std::cerr << "  Can be passed as argument or set FIRMWARE_API_KEY environment variable" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Examples:" << std::endl;
    std::cerr << "  " << program_name << " fw_api_xxx" << std::endl;
    std::cerr << "  " << program_name << " --refresh 60 --log quota.csv" << std::endl;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    std::string api_key;
    int refresh_interval = 15;
    std::string log_file = "show_quota.log";
    bool logging_enabled = true;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--tiny" || arg == "--resizable") {
            // Legacy options - ignored (single resizable mode now)
        } else if (arg == "--refresh" || arg == "-r") {
            if (i + 1 < argc) {
                refresh_interval = std::atoi(argv[++i]);
            } else {
                refresh_interval = 15;
            }
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

    // Initialize curl globally
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Initialize GTK
    if (!gtk_init_check(&argc, &argv)) {
        std::cerr << "Failed to initialize GTK. Install libgtk-3-dev." << std::endl;
        curl_global_cleanup();
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

    // Load saved state
    load_gui_state(state);

    // Preserve the loaded position for restore; some WMs will emit a configure
    // event at 0,0 while mapping which would otherwise clobber state->window_x/y.
    state->have_restore_pos = (state->window_x != -1 && state->window_y != -1);
    state->restore_x = state->window_x;
    state->restore_y = state->window_y;

    state->have_restore_size = (state->window_w != -1);
    state->restore_w = state->window_w;
    // Note: restoring flag will be set by on_window_map when it starts restore

    // Always show a window when launching GUI.
    state->window_visible = true;

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
        state->refresh_interval * 1000,
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

    curl_global_cleanup();

    return 0;
}
