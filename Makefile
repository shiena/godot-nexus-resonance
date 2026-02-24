# Nexus Resonance - Cross-platform build
# Requires: Python 3, SCons 4+, platform toolchains (MSVC/gcc/clang, Android NDK for Android)

.PHONY: install-steam-audio build build-windows build-linux build-macos build-android release help

# Install Steam Audio SDK (download from GitHub releases)
install-steam-audio:
	$(if $(wildcard src/lib/steamaudio/lib/windows-x64),\
		@echo "Steam Audio lib already present. Skipping.",\
		python scripts/install_steam_audio.py)

# Build for current/default platform
build: install-steam-audio
	scons

# Platform-specific builds
build-windows: install-steam-audio
	scons platform=windows target=template_debug
	scons platform=windows target=template_release

build-linux: install-steam-audio
	scons platform=linux target=template_debug
	scons platform=linux target=template_release

build-macos: install-steam-audio
	scons platform=macos target=template_debug
	scons platform=macos target=template_release

build-android: install-steam-audio
	scons platform=android arch=arm64 target=template_debug
	scons platform=android arch=arm64 target=template_release
	scons platform=android arch=x86_64 target=template_debug
	scons platform=android arch=x86_64 target=template_release

# Build all desktop platforms (Windows, Linux, macOS)
release: build-windows build-linux build-macos

help:
	@echo "Nexus Resonance Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  install-steam-audio  - Download Steam Audio SDK binaries"
	@echo "  build               - Build for current platform"
	@echo "  build-windows        - Build Windows x64 (debug + release)"
	@echo "  build-linux          - Build Linux x64 (debug + release)"
	@echo "  build-macos          - Build macOS (debug + release)"
	@echo "  build-android        - Build Android arm64 + x86_64"
	@echo "  release             - Build all desktop platforms"
	@echo ""
	@echo "Manual SCons examples:"
	@echo "  scons platform=windows"
	@echo "  scons platform=linux"
	@echo "  scons platform=macos arch=arm64"
	@echo "  scons platform=android arch=arm64 target=template_release"
