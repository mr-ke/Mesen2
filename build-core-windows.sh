#!/bin/bash

set -e

echo "========================================="
echo "Mesen2 Windows Cross-Compilation Builder"
echo "========================================="
echo ""

if [ "$EUID" -eq 0 ]; then
  echo "Error: Do not run this script as root/sudo"
  exit 1
fi

echo "Step 1: Checking for MinGW-w64 cross-compiler..."
if ! command -v x86_64-w64-mingw32-g++-posix &> /dev/null; then
    echo "MinGW-w64 (POSIX threads) not found. Installing..."
    echo "Note: POSIX threading model is required for C++11 thread support."
    echo "This requires sudo access. Please enter your password if prompted."
    sudo apt-get update
    sudo apt-get install -y mingw-w64 g++-mingw-w64-x86-64-posix
else
    echo "✓ MinGW-w64 (POSIX threads) is already installed"
fi

echo ""
echo "Step 2: Checking for SDL2 Windows libraries..."
SDL2_LIB="/usr/x86_64-w64-mingw32/lib/libSDL2.dll.a"
if [ ! -f "$SDL2_LIB" ]; then
    echo "SDL2 Windows libraries not found."
    echo ""
    echo "Running SDL2 MinGW setup script..."
    if [ -f "./setup-sdl2-mingw.sh" ]; then
        ./setup-sdl2-mingw.sh
    else
        echo "Error: setup-sdl2-mingw.sh not found!"
        echo "Please run: ./setup-sdl2-mingw.sh"
        exit 1
    fi
else
    echo "✓ SDL2 Windows libraries are already installed"
fi

echo ""
echo "Step 3: Checking for CMake..."
if ! command -v cmake &> /dev/null; then
    echo "CMake not found. Installing..."
    echo "This requires sudo access. Please enter your password if prompted."
    sudo apt-get install -y cmake
else
    echo "✓ CMake is already installed"
fi

echo ""
echo "Step 4: Creating build directory..."
BUILD_DIR="build-windows-x64"
mkdir -p "$BUILD_DIR"
echo "✓ Build directory created: $BUILD_DIR"

echo ""
echo "Step 5: Configuring CMake for Windows cross-compilation..."
cd "$BUILD_DIR"
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=../toolchain-mingw64.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_SDL=ON \
    -DBUILD_SHARED_LIBS=ON

if [ $? -ne 0 ]; then
    echo "✗ CMake configuration failed"
    exit 1
fi

echo ""
echo "Step 6: Building MesenCore.dll..."
make -j$(nproc)

if [ $? -ne 0 ]; then
    echo "✗ Build failed"
    exit 1
fi

echo ""
echo "========================================="
echo "✓ Build completed successfully!"
echo "========================================="
echo ""
echo "Output files:"
echo "  - $(pwd)/bin/MesenCore.dll"
echo ""
echo "Step 7: Copying required DLL files..."

# MinGW runtime DLLs
cp /usr/x86_64-w64-mingw32/bin/SDL2.dll bin/ 2>/dev/null || cp /usr/x86_64-w64-mingw32/lib/libSDL2.dll.a bin/SDL2.dll 2>/dev/null || echo "Warning: SDL2.dll not found"
cp /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll bin/ 2>/dev/null || echo "Warning: libwinpthread-1.dll not found"

# Copy librashader.dll and create alias for librashader_capi.dll
if [ -f "../3rdParty/librashader-dist/lib/librashader.dll" ]; then
    cp ../3rdParty/librashader-dist/lib/librashader.dll bin/
    # Create copy with expected name
    cp ../3rdParty/librashader-dist/lib/librashader.dll bin/librashader_capi.dll
    echo "✓ librashader.dll copied"
else
    echo "Warning: librashader.dll not found at 3rdParty/librashader-dist/lib/"
fi

echo ""
echo "Required DLL files copied to bin/:"
ls -lh bin/*.dll 2>/dev/null || echo "No DLL files found"
echo ""
echo "Next steps:"
echo "1. Copy all files from bin/ directory to your Windows .NET UI build:"
echo "   cp bin/*.dll ../bin/win-x64/Release/win-x64/publish/"
echo ""
echo "2. Run Mesen on Windows:"
echo "   The application should now work with the cross-compiled MesenCore.dll"
echo ""
