#!/bin/bash
# =============================================================================
# show_quota_wrapper.sh - Smart launcher for Firmware Quota Viewer
# =============================================================================
# Automatically selects the best available executable based on:
# 1. Explicit user preference (--use-text, --use-gui, --use-mixed)
# 2. Available executables
# 3. Whether DISPLAY is set (for GUI detection)
#
# Priority (when auto-selecting): text > mixed > gui
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Executables (check both local and system-wide)
find_executable() {
    local name="$1"
    if [[ -x "$SCRIPT_DIR/$name" ]]; then
        echo "$SCRIPT_DIR/$name"
    elif command -v "$name" &>/dev/null; then
        command -v "$name"
    else
        echo ""
    fi
}

TEXT_EXE=$(find_executable "show_quota_text")
GUI_EXE=$(find_executable "show_quota_gui")
MIXED_EXE=$(find_executable "show_quota")

# Parse wrapper-specific arguments
MODE=""
PASSTHROUGH_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --use-text)
            MODE="text"
            shift
            ;;
        --use-gui)
            MODE="gui"
            shift
            ;;
        --use-mixed)
            MODE="mixed"
            shift
            ;;
        --wrapper-help)
            echo "Firmware Quota Viewer Wrapper"
            echo "=============================="
            echo ""
            echo "Wrapper options (must come first):"
            echo "  --use-text    Force text-only version"
            echo "  --use-gui     Force GUI-only version"
            echo "  --use-mixed   Force mixed version"
            echo "  --wrapper-help Show this help"
            echo ""
            echo "All other arguments are passed to the selected executable."
            echo ""
            echo "Available executables:"
            [[ -n "$TEXT_EXE" ]] && echo "  Text:  $TEXT_EXE" || echo "  Text:  (not found)"
            [[ -n "$GUI_EXE" ]] && echo "  GUI:   $GUI_EXE" || echo "  GUI:   (not found)"
            [[ -n "$MIXED_EXE" ]] && echo "  Mixed: $MIXED_EXE" || echo "  Mixed: (not found)"
            echo ""
            echo "Auto-selection priority: text > mixed > gui"
            echo "If DISPLAY is not set, GUI-only version will not be used."
            exit 0
            ;;
        *)
            PASSTHROUGH_ARGS+=("$1")
            shift
            ;;
    esac
done

# Select executable based on mode
select_executable() {
    case "$MODE" in
        text)
            if [[ -n "$TEXT_EXE" ]]; then
                echo "$TEXT_EXE"
            else
                echo "Error: Text version (show_quota_text) not found." >&2
                echo "Build it with: make text" >&2
                exit 1
            fi
            ;;
        gui)
            if [[ -n "$GUI_EXE" ]]; then
                if [[ -z "$DISPLAY" ]]; then
                    echo "Warning: DISPLAY not set, GUI may not work." >&2
                fi
                echo "$GUI_EXE"
            else
                echo "Error: GUI version (show_quota_gui) not found." >&2
                echo "Build it with: make gui" >&2
                exit 1
            fi
            ;;
        mixed)
            if [[ -n "$MIXED_EXE" ]]; then
                echo "$MIXED_EXE"
            else
                echo "Error: Mixed version (show_quota) not found." >&2
                echo "Build it with: make mixed" >&2
                exit 1
            fi
            ;;
        *)
            # Auto-select based on priority: text > mixed > gui
            # Rationale: text is lightest, always works, no extra deps
            
            if [[ -n "$TEXT_EXE" ]]; then
                echo "$TEXT_EXE"
            elif [[ -n "$MIXED_EXE" ]]; then
                echo "$MIXED_EXE"
            elif [[ -n "$GUI_EXE" && -n "$DISPLAY" ]]; then
                echo "$GUI_EXE"
            else
                echo "Error: No executable found." >&2
                echo "Build one with: make text (or make gui, make mixed)" >&2
                exit 1
            fi
            ;;
    esac
}

SELECTED_EXE=$(select_executable)

# Execute with passthrough arguments
exec "$SELECTED_EXE" "${PASSTHROUGH_ARGS[@]}"
