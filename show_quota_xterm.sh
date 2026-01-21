#!/usr/bin/env bash

set -euo pipefail

GEOMETRY="${SHOW_QUOTA_XTERM_GEOMETRY:-80x8}"
TITLE_BASE="${SHOW_QUOTA_XTERM_TITLE:-Firmware Quota}"
TITLE="${TITLE_BASE} [$$-$RANDOM]"
BIN="${SHOW_QUOTA_BIN:-./show_quota}"

if ! command -v xterm >/dev/null 2>&1; then
  echo "Error: xterm not found in PATH" >&2
  exit 1
fi

if [ ! -x "$BIN" ]; then
  echo "Error: quota binary not executable: $BIN" >&2
  echo "Tip: run 'make' or set SHOW_QUOTA_BIN" >&2
  exit 1
fi

xterm \
  -geometry "$GEOMETRY" \
  -title "$TITLE" \
  -e bash -lc 'exec "$@"' bash "$BIN" "$@" &

XTERM_PID=$!

# Try to set the xterm window "always on top" (EWMH state: above).
# This is best-effort and depends on the window manager.
if command -v wmctrl >/dev/null 2>&1; then
  set +e
  for _ in $(seq 1 50); do
    wmctrl -r "$TITLE" -b add,above >/dev/null 2>&1
    if [ $? -eq 0 ]; then
      break
    fi
    sleep 0.1
  done
  set -e
fi

wait "$XTERM_PID"
