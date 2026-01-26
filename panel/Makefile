NAME := firmware-quota-panel-applet

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

APPLET_BIN := firmware-quota-applet
APPLET_SRC := firmware_quota_applet.cpp

COMMON_SRC := quota_common.cpp

CURL_LIBS := -lcurl

MATE_CFLAGS := $(shell pkg-config --cflags libmatepanelapplet-4.0)
MATE_LIBS := $(shell pkg-config --libs libmatepanelapplet-4.0)

.PHONY: all clean help

all: $(APPLET_BIN)
	@echo "Built: $(APPLET_BIN)"

$(APPLET_BIN): $(APPLET_SRC) $(COMMON_SRC) quota_common.h
	$(CXX) $(CXXFLAGS) $(MATE_CFLAGS) -o $@ $(APPLET_SRC) $(COMMON_SRC) $(CURL_LIBS) $(MATE_LIBS)

clean:
	rm -f $(APPLET_BIN)

help:
	@echo "$(NAME)"
	@echo "Targets:"
	@echo "  make        Build applet"
	@echo "  make clean  Remove built applet"
