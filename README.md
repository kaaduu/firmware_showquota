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

Launch the GUI with system tray support in different sizes:

```bash
# Standard size (400x250 window)
./show_quota --gui

# Compact size (300x150 window, no frames)
./show_quota --gui-compact

# Tiny size (150x80 window, minimal UI)
./show_quota --gui-tiny
```

### GUI Presentation Modes

The GUI supports three presentation modes, similar to terminal modes:

**Standard Mode** (`--gui`):
- Window size: 400x250
- Shows framed sections for Usage and Reset
- Displays detailed timestamps
- Full-featured layout

**Compact Mode** (`--gui-compact`):
- Window size: 300x150
- No frames, streamlined layout
- Perfect for small dashboard windows
- Essential info only

**Bar Mode** (`--gui-bar`):
- Window size: 350×100
- Thick 30px progress bars
- Compact horizontal layout
- Highly visible, color-coded bars
- Perfect for prominent monitoring

**Mini Mode** (`--gui-mini`):
- Window size: 200×120
- Chunky 25px progress bars
- Small footprint with big bars
- Great visibility in small space

**Wide Mode** (`--gui-wide`):
- Window size: 400×80
- Large 28px bars in ultra-wide format
- Thin height, maximum width
- Ideal for top/bottom screen placement

**Tiny Mode** (`--gui-tiny`):
- Window size: 150×50
- Ultra-minimal layout
- Shows only usage bar and percentage (15px)
- No reset countdown bar
- Perfect for corner monitoring

The selected mode is automatically saved and restored on restart. Use a mode flag to override the saved preference. All modes can be switched on-the-fly from the tray menu.

### GUI Features

- **System Tray Icon**: Color-coded icon (green/yellow/red) based on quota usage
  - Green: <50% usage
  - Yellow: 50-80% usage
  - Red: ≥80% usage
- **Hover Tooltip**: Shows current percentage and time until reset
- **Main Window**: Displays progress bars, percentages, and timestamps
  - Click tray icon to show/hide window
  - Window position is saved and restored on restart
- **Desktop Notifications**: Alerts for quota resets and high usage events
- **Right-Click Menu**:
  - Show / Hide window
  - **Window Style** submenu to switch between Standard, Compact, and Tiny modes on the fly
  - Quit
- **Mode Switching**: Change window size/style anytime from tray menu
- **All terminal features**: Logging, event detection, auth methods still work

### GUI Screenshots

The main window displays:
- Quota Usage progress bar with percentage
- Reset Countdown progress bar with time remaining
- Last updated timestamp
- Reset timestamp

The system tray provides:
- Quick access via panel icon
- Tooltip with current status
- Context menu for window management

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
