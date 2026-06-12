## Windows

1) Open the solution in Visual Studio 2022
2) Compile as `Release`/`x64`
3) Set the startup project to the `UI` project and run

## Cross Compile (Linux/WSL → Windows)

You can cross-compile Mesen from Linux or WSL for Windows host.

### Prerequisites

- .NET 8 SDK
- MinGW-w64 (for C++ cross-compilation)
- SDL2 Windows development files

### Build UI Project (C#)

```bash
dotnet publish UI/UI.csproj -c Release -r win-x64 --self-contained true -p:PublishDir=bin/SelfContained/
```

### Build MesenCore (C++)

```bash
./build-core-windows.sh
```

The compiled `MesenCore.dll` will be in `build-windows-x64/bin/`. Copy it to the UI output directory.

## Linux

To build under Linux you need a version of Clang or GCC that supports C++17.  

Additionally, SDL2 and the [.NET 8 SDK](https://learn.microsoft.com/en-us/dotnet/core/install/linux) must also be installed.

Once SDL2 and the .NET 8 SDK are installed, run `make` to compile with Clang.  
To compile with GCC instead, use `USE_GCC=true make`.  
**Note:** Mesen usually runs faster when built with Clang instead of GCC.


## macOS

To build macOS, install SDL2 (i.e via Homebrew) and the [.NET 8 SDK](https://dotnet.microsoft.com/en-us/download/dotnet/8.0).  

Once SDL2 and the .NET 8 SDK are installed, run `make`.
