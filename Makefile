CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LDFLAGS = -lcurl
TARGET = show_quota
SOURCE = show_quota.cpp

# GTK3 GUI support (optional, auto-detected)
GUI_ENABLED = $(shell pkg-config --exists gtk+-3.0 ayatana-appindicator3-0.1 libnotify && echo yes)

ifeq ($(GUI_ENABLED),yes)
    GUI_CFLAGS = $(shell pkg-config --cflags gtk+-3.0 ayatana-appindicator3-0.1 libnotify)
    GUI_LDFLAGS = $(shell pkg-config --libs gtk+-3.0 ayatana-appindicator3-0.1 libnotify) -lpthread
    CXXFLAGS += -DGUI_MODE_ENABLED $(GUI_CFLAGS)
    LDFLAGS += $(GUI_LDFLAGS)
    $(info Building with GUI support)
else
    $(info Building without GUI support - install libgtk-3-dev, libayatana-appindicator3-dev, libnotify-dev to enable)
endif

all: $(TARGET)

# Generate PNG icon from SVG if inkscape is available (for better tray icon support)
firmware-icon.png: firmware-icon.svg
	@if command -v inkscape >/dev/null 2>&1; then \
		echo "Generating PNG icon from SVG..."; \
		inkscape firmware-icon.svg -o firmware-icon.png -w 48 -h 48 2>/dev/null || true; \
	fi

$(TARGET): $(SOURCE) firmware-icon.png
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCE) $(LDFLAGS)

clean:
	rm -f $(TARGET) .firmware_quota_gui.conf

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

# Install GUI dependencies (Debian/Ubuntu)
install-deps-gui:
	sudo apt-get install -y libgtk-3-dev libayatana-appindicator3-dev libnotify-dev

.PHONY: all clean install install-deps-gui
