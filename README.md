# firmware_showquota

Terminal quota viewer for the Firmware API. Shows current quota usage and a live 5-hour reset countdown in a graphical (ANSI) terminal UI.

API reference (quota endpoint): https://docs.firmware.ai/api-reference/quota

## Requirements

- Linux/macOS (works best in a real TTY)
- C++17 compiler (e.g. `g++`)
- libcurl development headers (`libcurl4-openssl-dev` on Debian/Ubuntu)
- nlohmann/json headers

## Build

Use the provided Makefile:

```bash
make
```

Or compile manually:

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
# Default live view (60s refresh)
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

## Sneak peek (ANSI mode)

Exact widths/colors depend on your terminal, but it looks like this:

```text
Firmware API Quota Details:
==========================
Usage: [\x1b[33m██████████████░░░░░░░░░░░░░░\x1b[0m] 63.20%
Reset: [\x1b[31m████░░░░░░░░░░░░░░░░░░░░░░░\x1b[0m] 12m 08s left (of 5h)
Resets at: 2026-01-21 18:05:00 CET

Refreshing every 60 seconds (Ctrl+C to stop)...
```

Notes:

- If stdout is not a TTY (piped to a file), colors are automatically disabled.
- If your locale is UTF-8, the bars use block characters; otherwise they fall back to ASCII.

### Compact mode example (ANSI)

```text
U:[\x1b[33m██████████░░░░░░\x1b[0m] 63%
R:[\x1b[31m████░░░░░░░░░░░░\x1b[0m] 12m8s
```

### Tiny mode example (ANSI)

```text
\x1b[33m63%\x1b[0m
```
