#!/bin/bash
# Double-click to build LiveRack. Output is saved to build.log next to this file.
cd "$(dirname "$0")"
exec > >(tee "$PWD/build.log") 2>&1
echo "=== LiveRack build $(date) ==="
sw_vers | tr '\n' ' '; uname -m

if ! xcode-select -p >/dev/null 2>&1; then
  echo "MISSING: Xcode command line tools - an installer will open. Run this again when it finishes."
  xcode-select --install
  echo "=== EXIT 10 ==="; exit 10
fi
echo "Developer dir: $(xcode-select -p)"
xcodebuild -version 2>/dev/null | head -1

CMAKE=$(command -v cmake || ls /opt/homebrew/bin/cmake /usr/local/bin/cmake /Applications/CMake.app/Contents/bin/cmake 2>/dev/null | head -1)
if [ -z "$CMAKE" ]; then
  BREW=$(command -v brew || ls /opt/homebrew/bin/brew /usr/local/bin/brew 2>/dev/null | head -1)
  if [ -n "$BREW" ]; then
    echo "Installing cmake with Homebrew..."
    "$BREW" install cmake && CMAKE="$("$BREW" --prefix)/bin/cmake"
  fi
fi
if [ -z "$CMAKE" ]; then echo "MISSING: cmake and Homebrew"; echo "=== EXIT 11 ==="; exit 11; fi
"$CMAKE" --version | head -1

if xcodebuild -version >/dev/null 2>&1; then
  "$CMAKE" -B build -G Xcode && "$CMAKE" --build build --config Release -- -quiet
else
  "$CMAKE" -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release && "$CMAKE" --build build -j 8
fi
STATUS=$?
echo "=== EXIT $STATUS ==="
[ $STATUS -eq 0 ] && find build -name LiveRack.app -maxdepth 5
