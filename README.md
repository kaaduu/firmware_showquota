# firmware_showquota

Quota viewer for the Firmware API with both GUI and terminal modes. Shows current quota usage and a live 5-hour reset countdown.

API reference (quota endpoint): https://docs.firmware.ai/api-reference/quota

## Requirements

### Terminal Mode
- Linux/macOS (works best in a real TTY)
- C++17 compiler (e.g. `g++`)
- libcurl development headers (`libcurl4-openssl-dev` on Debian/Ubuntu)
- nlohmann/json headers

### GUI Mode (Optional)
- GTK3 development libraries (`libgtk-3-dev`)
- libayatana-appindicator3 (`libayatana-appindicator3-dev`)
- libnotify (`libnotify-dev`)

Install GUI dependencies on Debian/Ubuntu:
```bash
sudo apt-get install libgtk-3-dev libayatana-appindicator3-dev libnotify-dev
```

## Build

The Makefile automatically detects GUI dependencies and enables GUI support if available:

```bash
make
```

If GUI libraries are installed, you'll see:
```
Building with GUI support
```

Otherwise:
```
Building without GUI support
```

You can also install GUI dependencies with:
```bash
make install-deps-gui
```

Or compile manually for terminal-only:

```bash
g++ -std=c++17 -O2 -Wall -Wextra show_quota.cpp -o show_quota -lcurl
```

## API key

You can pass the API key as an argument, or set it in an environment variable.

Export via environment variable (recommended):

```bash
export FIRMWARE_API_KEY="fw_api_xxx"
```

Then run:

```bash
./show_quota
```

## GUI Mode

### Launching GUI Mode

Launch the GUI with system tray support:

```bash
# Tiny (fixed size)
./show_quota --gui-tiny

# Tiny style but resizable horizontally
./show_quota --gui-resizable

# Alias for tiny
./show_quota --gui
```

### GUI Command-Line Options

All standard options work with GUI mode:

```bash
# GUI with custom refresh interval
./show_quota --gui-tiny --refresh 120

# GUI without logging
./show_quota --gui-tiny --no-log

# GUI with custom log file
./show_quota --gui-tiny --log /var/log/quota.log
```

**Note**: The `-1` (single run) option doesn't apply to GUI mode - the GUI runs continuously until you quit from the tray menu.

### GUI Presentation Modes

The GUI supports two modes (switchable from the tray menu under "Window Style"):

**Tiny (fixed)** (`--gui-tiny`):
- Fixed 150x50 window
- Intended for corner monitoring

**Resizable (width)** (`--gui-resizable`):
- Same layout as Tiny, but the window can be resized horizontally
- Progress bar length follows the window width
- Remembers last width

The selected mode, window position, bar height, and refresh interval are saved and restored on restart.

### GUI Features

- **System Tray Integration**:
  - Custom Firmware logo icon displayed in system tray
  - Persistent tray presence - application runs in background even when window is hidden
  - Click tray icon to toggle window visibility
  - Right-click for context menu with full controls

- **Color-Coded Visual Feedback**:
  - Progress bars change color based on quota usage
  - Green: <50% usage (healthy)
  - Yellow: 50-80% usage (moderate)
  - Red: ≥80% usage (high/critical)

- **Interactive Window**:
  - Displays real-time progress bars for quota usage and reset countdown
  - Shows percentages, timestamps, and time remaining
- Window position is saved and restored (multi-monitor safe)
  - Close button hides window to tray instead of quitting

- **Desktop Notifications**:
  - Automatic alerts for quota reset events
  - High usage warnings when quota increases significantly
  - Non-intrusive desktop notifications (10-second timeout)

- **System Tray Context Menu**:
  - **Show Window** / **Hide Window**: Toggle main window visibility
  - **Window Style**: `Tiny (fixed)` / `Resizable (width)`
  - **Progress Bar Height**: Adjust thickness (1x–4x)
  - **Reset Window Position**: Move window back to primary monitor
  - **Auto-start on Login**: Toggle autostart via `~/.config/autostart/show_quota.desktop` (MATE)
  - **Show Title Bar**: Toggle window decorations; when disabled you can drag the window from anywhere; double-click the window to toggle
  - **Dark Mode**: Toggle dark/light appearance
  - **Quit**: Exit application completely

- **Hover Tooltip**: Live status display showing current percentage and time until reset

- **All Terminal Features**: Full compatibility with logging, event detection, custom refresh intervals, and authentication methods

### GUI Technical Details

**Icon**: The application uses `firmware-icon.svg` for the system tray. The icon should be in the same directory as the executable, or in the system icon theme paths.

**Configuration**: GUI state (window position, visibility, and selected mode) is automatically saved to `~/.firmware_quota_gui.conf` and restored on startup.

**Autostart (MATE)**: The tray `Auto-start on Login` toggle writes `~/.config/autostart/show_quota.desktop` to start `show_quota --gui` on session login.

If a GUI config exists, `--gui` restores the last used GUI style and window position. Use `--gui-tiny` or `--gui-resizable` to override the saved style.

**Threading**: Quota fetching runs in a background thread to keep the GUI responsive. Updates are displayed as soon as data is received.

**Main Window Components**:
- Quota Usage progress bar with percentage and exact value
- Reset Countdown progress bar with time remaining (not shown in Tiny mode)
- Timestamp labels showing last update and reset time (Standard mode only)
- Color-coded bars that update dynamically based on thresholds

**System Tray Components**:
- Persistent tray icon for quick access
- Live tooltip with current quota status
- Context menu for full application control

## Usage

Default behavior:

- Runs forever, refreshes every 60 seconds
- Logs to `./show_quota.log` by default
- Stop with Ctrl+C

Help:

```bash
./show_quota --help
```

Common examples:

```bash
# GUI mode with system tray
./show_quota --gui

# GUI mode with custom refresh interval
./show_quota --gui --refresh 120

# GUI mode without logging
./show_quota --gui --no-log

# Default terminal live view (60s refresh)
./show_quota

# Single run (no refresh loop)
./show_quota -1

# Refresh every 60 seconds (minimum supported)
./show_quota --refresh 60

# Disable logging
./show_quota --no-log

# Log to a custom file
./show_quota --log quota.csv

# Pure text output (no progress bars)
./show_quota --text

# Compact bar layout for ~40-column terminals
./show_quota --compact

# Tiny single-line layout
./show_quota --tiny

# Run inside a fixed-size xterm (default 80x8)
./show_quota_xterm.sh

# Compact xterm preset (80x3)
./show_quota_xterm.sh --compact

# Compact 40-column xterm preset (40x3)
./show_quota_xterm.sh --compact-40

# Tiny xterm preset (6x2, font size 20)
./show_quota_xterm.sh --tiny
```

## Run in xterm (80x8)

If you want a consistent layout for screenshots or a tiny dashboard window, run it inside xterm:

```bash
./show_quota_xterm.sh
```

By default the launcher uses a larger font for readability. You can override it:

```bash
SHOW_QUOTA_XTERM_FONT_FACE="DejaVu Sans Mono" SHOW_QUOTA_XTERM_FONT_SIZE=16 ./show_quota_xterm.sh
```

Pass any `show_quota` args through:

```bash
./show_quota_xterm.sh --refresh 60 --no-log

# Compact preset (80x3)
./show_quota_xterm.sh --compact

# Compact 40-column preset (40x3)
./show_quota_xterm.sh --compact-40

# Tiny preset (6x2, font size 20)
./show_quota_xterm.sh --tiny
```

If `wmctrl` is installed, the xterm will also be set to "always on top" (best-effort; depends on your window manager).

## What the output means

- `Usage` bar: quota usage percentage reported by the API.
- `Reset` bar: time remaining until the next reset.
  - The quota window is treated as a fixed 5 hours.
  - The bar drains toward the reset time.
  - Colors shift as reset approaches (green -> yellow -> red).

## Sneak peek (TTY colored)

Exact widths/colors depend on your terminal, but it looks like this:

```text
Firmware API Quota Details:
==========================
Usage: [██████████████░░░░░░░░░░░░░░] 63.20%
Reset: [████░░░░░░░░░░░░░░░░░░░░░░░] 12m 08s left (of 5h)
Resets at: 2026-01-21 18:05:00 CET

Refreshing every 60 seconds (Ctrl+C to stop)...
```

Notes:

- If stdout is not a TTY (piped to a file), colors are automatically disabled.
- If your locale is UTF-8, the bars use block characters; otherwise they fall back to ASCII.

### Compact mode example

```text
U:[██████████░░░░░░] 63%
R:[████░░░░░░░░░░░░] 12m8s
```

### Tiny mode example

```text
63%
```
