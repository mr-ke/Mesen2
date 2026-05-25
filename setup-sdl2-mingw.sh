#!/bin/bash

set -e

echo "========================================="
echo "SDL2 MinGW Setup for Cross-Compilation"
echo "========================================="
echo ""

SDL2_VERSION="2.28.5"
SDL2_URL="https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-mingw.tar.gz"

MINGW_PREFIX="/usr/x86_64-w64-mingw32"
TEMP_DIR="/tmp/sdl2-mingw-setup"

echo "Step 1: Checking MinGW-w64 installation..."
if ! command -v x86_64-w64-mingw32-g++-posix &> /dev/null; then
    echo "MinGW-w64 (POSIX threads) not found. Installing..."
    echo "Note: POSIX threading model is required for C++11 thread support."
    sudo apt-get update
    sudo apt-get install -y mingw-w64 g++-mingw-w64-x86-64-posix
else
    echo "✓ MinGW-w64 (POSIX threads) is already installed"
fi

echo ""
echo "Step 2: Creating temporary directory..."
mkdir -p "$TEMP_DIR"
cd "$TEMP_DIR"
echo "✓ Working in: $TEMP_DIR"

echo ""
echo "Step 3: Downloading SDL2 MinGW development files..."
echo "Version: $SDL2_VERSION"
echo "URL: $SDL2_URL"

if [ ! -f "SDL2-devel-${SDL2_VERSION}-mingw.tar.gz" ]; then
    wget -q --show-progress "$SDL2_URL" -O "SDL2-devel-${SDL2_VERSION}-mingw.tar.gz"
    echo "✓ Download complete"
else
    echo "✓ File already downloaded"
fi

echo ""
echo "Step 4: Extracting SDL2..."
tar -xzf "SDL2-devel-${SDL2_VERSION}-mingw.tar.gz"
echo "✓ Extraction complete"

echo ""
echo "Step 5: Installing SDL2 libraries to MinGW prefix..."
SDL2_DIR="SDL2-${SDL2_VERSION}"

echo "  - Copying x86_64 libraries..."
sudo cp -r "$SDL2_DIR/x86_64-w64-mingw32/lib/"* "$MINGW_PREFIX/lib/" 2>/dev/null || true

echo "  - Copying x86_64 headers..."
sudo cp -r "$SDL2_DIR/x86_64-w64-mingw32/include/"* "$MINGW_PREFIX/include/" 2>/dev/null || true

echo "  - Copying SDL2.dll..."
sudo cp "$SDL2_DIR/x86_64-w64-mingw32/bin/SDL2.dll" "$MINGW_PREFIX/bin/" 2>/dev/null || true

echo "✓ SDL2 installation complete"

echo ""
echo "Step 6: Verifying installation..."
if [ -f "$MINGW_PREFIX/lib/libSDL2.dll.a" ]; then
    echo "✓ SDL2 static library found: $MINGW_PREFIX/lib/libSDL2.dll.a"
else
    echo "✗ SDL2 static library not found"
    exit 1
fi

if [ -f "$MINGW_PREFIX/include/SDL2/SDL.h" ]; then
    echo "✓ SDL2 headers found: $MINGW_PREFIX/include/SDL2/SDL.h"
else
    echo "✗ SDL2 headers not found"
    exit 1
fi

if [ -f "$MINGW_PREFIX/bin/SDL2.dll" ]; then
    echo "✓ SDL2.dll found: $MINGW_PREFIX/bin/SDL2.dll"
else
    echo "✗ SDL2.dll not found"
    exit 1
fi

echo ""
echo "========================================="
echo "✓ SDL2 MinGW setup completed successfully!"
echo "========================================="
echo ""
echo "Installed files:"
echo "  - Library: $MINGW_PREFIX/lib/libSDL2.dll.a"
echo "  - Headers: $MINGW_PREFIX/include/SDL2/"
echo "  - DLL: $MINGW_PREFIX/bin/SDL2.dll"
echo ""
echo "You can now build MesenCore.dll for Windows using:"
echo "  ./build-core-windows.sh"
echo ""
