# Makefile for WebView2 Framework (MinGW-w64 / GCC)
# -------------------------------------------------
# Requirements:
#   - MinGW-w64 (g++)
#   - WebView2 SDK headers + WebView2Loader.dll + import library
#
# Quick start:
#   1. Put WebView2.h into include/
#   2. Put WebView2Loader.dll into lib/x64/
#   3. make gen-import-lib   (creates the .a file)
#   4. make
#   5. make run

CXX       ?= g++
# Do NOT use -mwindows or -municode.
# This host is started by Node.js and must use int main() so stdin/stdout pipes work.
# -municode forces the linker to look for wWinMain and causes:
#   undefined reference to `wWinMain'
CXXFLAGS  ?= -std=c++17 -O2 -Wall -Wextra -Wno-unknown-pragmas
INCLUDES   = -Iinclude
LIBS       = -Llib/x64 -lWebView2Loader -lole32 -loleaut32 -luuid -lshell32 -lshlwapi -luser32 -lcomdlg32 -lgdi32 -ladvapi32 -lversion

SRC        = src/main.cpp
OUT_DIR    = bin
TARGET     = $(OUT_DIR)/webview2-host.exe

.PHONY: all clean run copy-dll check help gen-import-lib

all: $(TARGET) copy-dll

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(TARGET): $(SRC) | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(LIBS)

copy-dll:
	@if [ -f lib/x64/WebView2Loader.dll ]; then \
		cp -f lib/x64/WebView2Loader.dll $(OUT_DIR)/; \
		echo "Copied WebView2Loader.dll → bin/"; \
	else \
		echo "WARNING: lib/x64/WebView2Loader.dll not found."; \
		echo "         Place it there before running the program."; \
	fi

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)
	rm -f $(OUT_DIR)/WebView2Loader.dll

# Check that required files exist
check:
	@echo "Checking project files..."
	@test -f include/WebView2.h && echo "  [OK] include/WebView2.h" || echo "  [MISSING] include/WebView2.h"
	@test -f lib/x64/WebView2Loader.dll && echo "  [OK] lib/x64/WebView2Loader.dll" || echo "  [MISSING] lib/x64/WebView2Loader.dll"
	@test -f lib/x64/libWebView2Loader.a && echo "  [OK] lib/x64/libWebView2Loader.a" || echo "  [MISSING] lib/x64/libWebView2Loader.a  ← run: make gen-import-lib"
	@echo "Done."

# Generate the import library from the DLL (run this once)
gen-import-lib:
	@if [ ! -f lib/x64/WebView2Loader.dll ]; then \
		echo "ERROR: lib/x64/WebView2Loader.dll not found. Copy it first."; \
		exit 1; \
	fi
	cd lib/x64 && gendef WebView2Loader.dll
	cd lib/x64 && dlltool -d WebView2Loader.def -l libWebView2Loader.a -D WebView2Loader.dll
	@echo "Created lib/x64/libWebView2Loader.a"
	@echo "You can now run: make"

help:
	@echo "Targets:"
	@echo "  make              - build the demo"
	@echo "  make check        - verify headers + DLL + import lib exist"
	@echo "  make gen-import-lib - create libWebView2Loader.a from the DLL"
	@echo "  make run          - build and run"
	@echo "  make clean        - remove built files"
