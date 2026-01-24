CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LDFLAGS = -lcurl

# Targets
TARGET_TEXT = show_quota_text
TARGET_GUI = show_quota_gui
TARGET_MIXED = show_quota

# Sources
SOURCE_TEXT = show_quota_text.cpp
SOURCE_GUI = show_quota_gui.cpp
SOURCE_MIXED = show_quota_mixed.cpp
SOURCE_COMMON = quota_common.cpp

# GTK3 GUI support (optional, auto-detected)
GUI_AVAILABLE = $(shell pkg-config --exists gtk+-3.0 ayatana-appindicator3-0.1 libnotify 2>/dev/null && echo yes)

ifeq ($(GUI_AVAILABLE),yes)
    GUI_CFLAGS = $(shell pkg-config --cflags gtk+-3.0 ayatana-appindicator3-0.1 libnotify)
    GUI_LDFLAGS = $(shell pkg-config --libs gtk+-3.0 ayatana-appindicator3-0.1 libnotify) -lpthread
endif

# Default target: build what's available
.PHONY: all text gui mixed clean install install-deps-gui help

all: text mixed-auto
	@echo ""
	@echo "Build complete!"
	@echo "  - $(TARGET_TEXT): Text-only version (no GUI dependencies)"
ifeq ($(GUI_AVAILABLE),yes)
	@echo "  - $(TARGET_MIXED): Mixed version (GUI support enabled)"
	@echo ""
	@echo "To build GUI-only version: make gui"
else
	@echo "  - $(TARGET_MIXED): Mixed version (GUI support disabled - text only)"
	@echo ""
	@echo "GUI libraries not found. To enable GUI support:"
	@echo "  sudo apt-get install libgtk-3-dev libayatana-appindicator3-dev libnotify-dev"
	@echo "  make clean && make"
endif

# ============================================================================
# Text-only version (no GUI dependencies)
# ============================================================================
text: $(TARGET_TEXT)
	@echo "Built $(TARGET_TEXT) (text-only, no GUI dependencies)"

$(TARGET_TEXT): $(SOURCE_TEXT) $(SOURCE_COMMON) quota_common.h
	$(CXX) $(CXXFLAGS) -o $(TARGET_TEXT) $(SOURCE_TEXT) $(SOURCE_COMMON) $(LDFLAGS)

# ============================================================================
# GUI-only version (requires GTK3)
# ============================================================================
gui: check-gui $(TARGET_GUI)
	@echo "Built $(TARGET_GUI) (GUI-only)"

$(TARGET_GUI): $(SOURCE_GUI) $(SOURCE_COMMON) quota_common.h
ifeq ($(GUI_AVAILABLE),yes)
	$(CXX) $(CXXFLAGS) $(GUI_CFLAGS) -o $(TARGET_GUI) $(SOURCE_GUI) $(SOURCE_COMMON) $(LDFLAGS) $(GUI_LDFLAGS)
else
	@echo "Error: GUI libraries not available. Install them first:"
	@echo "  sudo apt-get install libgtk-3-dev libayatana-appindicator3-dev libnotify-dev"
	@exit 1
endif

check-gui:
ifeq ($(GUI_AVAILABLE),)
	@echo "Error: GUI libraries not available. Install them first:"
	@echo "  sudo apt-get install libgtk-3-dev libayatana-appindicator3-dev libnotify-dev"
	@exit 1
endif

# ============================================================================
# Mixed version (text + optional GUI based on availability)
# ============================================================================
mixed: mixed-auto
	@echo "Built $(TARGET_MIXED) (mixed version)"

mixed-auto: firmware-icon.png $(TARGET_MIXED)

# Force build with GUI support (will fail if GTK not available)
mixed-gui: check-gui firmware-icon.png
	$(CXX) $(CXXFLAGS) -DGUI_MODE_ENABLED $(GUI_CFLAGS) -o $(TARGET_MIXED) $(SOURCE_MIXED) $(LDFLAGS) $(GUI_LDFLAGS)
	@echo "Built $(TARGET_MIXED) with GUI support enabled"

# Force build without GUI support
mixed-text:
	$(CXX) $(CXXFLAGS) -o $(TARGET_MIXED) $(SOURCE_MIXED) $(LDFLAGS)
	@echo "Built $(TARGET_MIXED) without GUI support"

$(TARGET_MIXED): $(SOURCE_MIXED)
ifeq ($(GUI_AVAILABLE),yes)
	@echo "Building $(TARGET_MIXED) with GUI support"
	$(CXX) $(CXXFLAGS) -DGUI_MODE_ENABLED $(GUI_CFLAGS) -o $(TARGET_MIXED) $(SOURCE_MIXED) $(LDFLAGS) $(GUI_LDFLAGS)
else
	@echo "Building $(TARGET_MIXED) without GUI support (GUI libraries not found)"
	$(CXX) $(CXXFLAGS) -o $(TARGET_MIXED) $(SOURCE_MIXED) $(LDFLAGS)
endif

# ============================================================================
# Build all three versions (requires GUI support)
# ============================================================================
all-versions: check-gui text gui mixed-gui
	@echo ""
	@echo "Built all three versions:"
	@echo "  - $(TARGET_TEXT): Text-only"
	@echo "  - $(TARGET_GUI): GUI-only"
	@echo "  - $(TARGET_MIXED): Mixed (text + GUI)"

# ============================================================================
# Icon generation
# ============================================================================
firmware-icon.png: firmware-icon.svg
	@if command -v inkscape >/dev/null 2>&1; then \
		echo "Generating PNG icon from SVG..."; \
		inkscape firmware-icon.svg -o firmware-icon.png -w 48 -h 48 2>/dev/null || true; \
	fi

# ============================================================================
# Clean
# ============================================================================
clean:
	rm -f $(TARGET_TEXT) $(TARGET_GUI) $(TARGET_MIXED) .firmware_quota_gui.conf

# ============================================================================
# Install
# ============================================================================
install: all
	@echo "Installing executables to /usr/local/bin/"
	@test -f $(TARGET_TEXT) && install -m 755 $(TARGET_TEXT) /usr/local/bin/ || true
	@test -f $(TARGET_GUI) && install -m 755 $(TARGET_GUI) /usr/local/bin/ || true
	@test -f $(TARGET_MIXED) && install -m 755 $(TARGET_MIXED) /usr/local/bin/ || true
	@echo "Done."

install-text: text
	install -m 755 $(TARGET_TEXT) /usr/local/bin/

install-gui: gui
	install -m 755 $(TARGET_GUI) /usr/local/bin/

install-mixed: mixed
	install -m 755 $(TARGET_MIXED) /usr/local/bin/

# Install GUI dependencies (Debian/Ubuntu)
install-deps-gui:
	sudo apt-get install -y libgtk-3-dev libayatana-appindicator3-dev libnotify-dev

# ============================================================================
# Help
# ============================================================================
help:
	@echo "Firmware Quota Viewer - Build Targets"
	@echo "======================================"
	@echo ""
	@echo "Main targets:"
	@echo "  make              - Build text + mixed versions (auto-detect GUI)"
	@echo "  make text         - Build text-only version (no GUI dependencies)"
	@echo "  make gui          - Build GUI-only version (requires GTK3)"
	@echo "  make mixed        - Build mixed version (auto-detect GUI)"
	@echo "  make all-versions - Build all three versions (requires GTK3)"
	@echo ""
	@echo "Explicit mixed builds:"
	@echo "  make mixed-gui    - Build mixed with GUI support (fails if GTK not found)"
	@echo "  make mixed-text   - Build mixed without GUI support"
	@echo ""
	@echo "Installation:"
	@echo "  make install      - Install all built executables to /usr/local/bin"
	@echo "  make install-text - Install text-only version"
	@echo "  make install-gui  - Install GUI-only version"
	@echo "  make install-mixed- Install mixed version"
	@echo ""
	@echo "Dependencies:"
	@echo "  make install-deps-gui - Install GTK3 dependencies (Debian/Ubuntu)"
	@echo ""
	@echo "Utilities:"
	@echo "  make clean        - Remove built executables"
	@echo "  make help         - Show this help"
	@echo ""
	@echo "Output files:"
	@echo "  $(TARGET_TEXT)   - Text-only (requires: libcurl)"
	@echo "  $(TARGET_GUI)    - GUI-only (requires: libcurl, GTK3, appindicator, libnotify)"
	@echo "  $(TARGET_MIXED)  - Mixed (requires: libcurl, optionally GTK3+)"
	@echo ""
ifeq ($(GUI_AVAILABLE),yes)
	@echo "GUI support: AVAILABLE"
else
	@echo "GUI support: NOT AVAILABLE"
	@echo "  Run 'make install-deps-gui' to install GTK3 dependencies"
endif
