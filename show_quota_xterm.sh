#!/usr/bin/env bash

set -euo pipefail

GEOMETRY="${SHOW_QUOTA_XTERM_GEOMETRY:-80x8}"
FONT_FACE="${SHOW_QUOTA_XTERM_FONT_FACE:-DejaVu Sans Mono}"
FONT_SIZE="${SHOW_QUOTA_XTERM_FONT_SIZE:-12}"
PIXEL_GEOMETRY="${SHOW_QUOTA_XTERM_PIXEL_GEOMETRY:-}"
TITLE_BASE="${SHOW_QUOTA_XTERM_TITLE:-Firmware Quota}"
TITLE="${TITLE_BASE} [$$-$RANDOM]"
BIN="${SHOW_QUOTA_BIN:-./show_quota}"

COMPACT_40=0
COMPACT=0
TINY=0
FONT_SIZE_SET=0

usage() {
  cat <<'EOF'
Usage: show_quota_xterm.sh [options] [--] [show_quota args...]

Options:
  --compact             80-col compact view (sets geometry and adds --compact)
  --compact-40          40-col compact view (sets geometry and adds --compact)
  --tiny                Extra small single-line view (sets geometry, adds --tiny, font size 20)
  --font-face FACE     XTerm font face (overrides SHOW_QUOTA_XTERM_FONT_FACE)
  --font-size SIZE     XTerm font size (overrides SHOW_QUOTA_XTERM_FONT_SIZE)
  -h, --help           Show this help

Notes:
  - Use `--` to stop option parsing and pass flags through to show_quota.
EOF
}

die() {
  echo "Error: $*" >&2
  exit 1
}

while [ $# -gt 0 ]; do
  case "$1" in
    --compact)
      COMPACT=1
      shift
      ;;
    --compact-40)
      COMPACT_40=1
      shift
      ;;
    --tiny)
      TINY=1
      shift
      ;;
    --font-face)
      [ $# -ge 2 ] || die "--font-face requires an argument"
      FONT_FACE="$2"
      shift 2
      ;;
    --font-size)
      [ $# -ge 2 ] || die "--font-size requires an argument"
      FONT_SIZE="$2"
      FONT_SIZE_SET=1
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    *)
      break
      ;;
  esac
done

if [ "$TINY" -eq 1 ]; then
  GEOMETRY="${SHOW_QUOTA_XTERM_GEOMETRY:-6x2}"
  set -- --tiny "$@"
  if [ -z "${SHOW_QUOTA_XTERM_FONT_SIZE:-}" ] && [ "$FONT_SIZE_SET" -eq 0 ]; then
    FONT_SIZE=20
  fi
elif [ "$COMPACT_40" -eq 1 ]; then
  GEOMETRY="${SHOW_QUOTA_XTERM_GEOMETRY:-40x3}"
  set -- --compact "$@"
elif [ "$COMPACT" -eq 1 ]; then
  GEOMETRY="${SHOW_QUOTA_XTERM_GEOMETRY:-80x3}"
  set -- --compact "$@"
fi

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
  -fa "$FONT_FACE" \
  -fs "$FONT_SIZE" \
  -geometry "${PIXEL_GEOMETRY:-$GEOMETRY}" \
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
