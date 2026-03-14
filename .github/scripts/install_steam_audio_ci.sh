#!/usr/bin/env bash
# Download and extract Steam Audio SDK for CI.
# Skips download if already present (cache hit).
# Version is read from scripts/install_steam_audio.py to stay in sync.
set -euo pipefail

DEST="src/lib/steamaudio"

if [ -d "$DEST/lib" ]; then
  echo "Steam Audio SDK already present (cache hit). Skipping download."
  exit 0
fi

# Extract version from the canonical install script
PYTHON="${PYTHON:-python3}"
VERSION=$($PYTHON -c "import re; print(re.search(r'STEAM_AUDIO_VERSION\s*=\s*\"(.+?)\"', open('scripts/install_steam_audio.py').read()).group(1))")
URL="https://github.com/ValveSoftware/steam-audio/releases/download/v${VERSION}/steamaudio_${VERSION}.zip"

echo "Downloading Steam Audio v${VERSION}..."
TMPZIP="$(mktemp)"
curl -fsSL -o "$TMPZIP" "$URL"
unzip -qo "$TMPZIP" -d src/lib/
rm -f "$TMPZIP"

# The zip may extract to steamaudio_<version>/ — rename to steamaudio/
if [ -d "src/lib/steamaudio_${VERSION}" ]; then
  rm -rf "$DEST"
  mv "src/lib/steamaudio_${VERSION}" "$DEST"
fi

echo "Done. Steam Audio binaries are in ${DEST}/lib/"
