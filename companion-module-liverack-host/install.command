#!/bin/bash
# Builds the LiveRack Companion module into a package you can import into Companion 5.
cd "$(dirname "$0")"

if ! command -v node >/dev/null 2>&1; then
  echo "Node.js isn't installed."
  if command -v brew >/dev/null 2>&1; then
    echo "Installing it with Homebrew..."
    brew install node || exit 1
  else
    echo "Install Node.js 22 or newer from https://nodejs.org, then run this again."
    read -n 1 -s -r -p "Press any key to close."
    exit 1
  fi
fi

echo "Node $(node --version)"
echo "Installing the module's libraries..."
npm install || { echo "npm install failed"; read -n 1 -s -r -p "Press any key to close."; exit 1; }

echo "Building the package..."
npm run package || { echo "Build failed"; read -n 1 -s -r -p "Press any key to close."; exit 1; }

PKG=$(ls -t *.tgz 2>/dev/null | head -1)
echo
if [ -n "$PKG" ]; then
  echo "Done: $(pwd)/$PKG"
  echo "In Companion: Modules > Import module package, and pick that file."
  open -R "$PKG"
else
  echo "Done. If no .tgz appeared, use Companion's developer modules folder instead (see README.md)."
fi
read -n 1 -s -r -p "Press any key to close."
