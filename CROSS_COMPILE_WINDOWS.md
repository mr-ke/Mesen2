# Cross-Compiling Mesen2 for Windows from WSL/Linux

This guide explains how to build MesenCore.dll for Windows from WSL or Linux using cross-compilation.

## Prerequisites

You need to install the MinGW-w64 cross-compiler and manually set up SDL2 Windows libraries:

### Step 1: Install MinGW-w64

```bash
sudo apt-get update
sudo apt-get install -y mingw-w64 g++-mingw-w64-x86-64-posix cmake
```

**Important**: Use the `posix` threading variant for C++11 threading support. The `win32` threading model does not support `std::thread`, `std::mutex`, and other C++11 threading features.

### Step 2: Setup SDL2 for MinGW

SDL2 MinGW libraries are not available in standard repositories. Use the automated setup script:

```bash
./setup-sdl2-mingw.sh
```

This script will:
- Download SDL2 MinGW development files from official SDL releases
- Install them to `/usr/x86_64-w64-mingw32/`
- Verify the installation

**Manual SDL2 Setup** (if you prefer to do it manually):
```bash
wget https://github.com/libsdl-org/SDL/releases/download/release-2.28.5/SDL2-devel-2.28.5-mingw.tar.gz
tar -xzf SDL2-devel-2.28.5-mingw.tar.gz
sudo cp -r SDL2-2.28.5/x86_64-w64-mingw32/lib/* /usr/x86_64-w64-mingw32/lib/
sudo cp -r SDL2-2.28.5/x86_64-w64-mingw32/include/* /usr/x86_64-w64-mingw32/include/
sudo cp SDL2-2.28.5/x86_64-w64-mingw32/bin/SDL2.dll /usr/x86_64-w64-mingw32/bin/
```

## Quick Build

Use the automated build script:

```bash
chmod +x build-core-windows.sh
./build-core-windows.sh
```

## Manual Build

If you prefer to build manually:

1. **Create a build directory:**
   ```bash
   mkdir build-windows-x64
   cd build-windows-x64
   ```

2. **Configure CMake:**
   ```bash
   cmake .. \
       -DCMAKE_TOOLCHAIN_FILE=../toolchain-mingw64.cmake \
       -DCMAKE_BUILD_TYPE=Release \
       -DUSE_SDL=ON \
       -DBUILD_SHARED_LIBS=ON
   ```

3. **Build:**
   ```bash
   make -j$(nproc)
   ```

4. **Copy SDL2.dll:**
   ```bash
   cp /usr/x86_64-w64-mingw32/bin/SDL2.dll bin/
   ```

## Output

The build produces:
- `build-windows-x64/bin/MesenCore.dll` - The main emulation core
- SDL2.dll needs to be copied alongside it

## Integration with .NET UI

Copy the built files to your Windows .NET UI:

```bash
cp build-windows-x64/bin/MesenCore.dll bin/win-x64/Release/win-x64/publish/
cp /usr/x86_64-w64-mingw32/bin/SDL2.dll bin/win-x64/Release/win-x64/publish/
```

## How It Works

The cross-compilation uses:
- **MinGW-w64**: GCC compiler for Windows
- **SDL2**: Cross-platform rendering (replaces DirectX)
- **CMake**: Build system that supports cross-compilation

The Windows-specific DirectX code in the `Windows/` directory is replaced with SDL2 code from the `Sdl/` directory, making cross-compilation possible.

## Troubleshooting

### Missing SDL2

If you get SDL2-related errors:
```bash
sudo apt-get install libsdl2-mingw-w64-dev
```

### Missing MinGW

If the compiler is not found:
```bash
sudo apt-get install mingw-w64 g++-mingw-w64-x86-64
```

### CMake Configuration Errors

Make sure you're using the correct toolchain file:
```bash
-DCMAKE_TOOLCHAIN_FILE=../toolchain-mingw64.cmake
```

## Building for Other Platforms

### Linux Native
```bash
mkdir build-linux && cd build-linux
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### macOS (requires macOS)
```bash
mkdir build-macos && cd build-macos
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)
```

## Notes

- The cross-compiled version uses SDL2 for rendering instead of DirectX
- All emulation features are identical to the native Windows build
- Performance should be comparable to the Visual Studio build
- The build is fully compatible with the .NET UI built for Windows
