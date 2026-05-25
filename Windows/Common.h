#pragma once

#ifndef WINVER
#define WINVER 0x0A00
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#ifndef _WIN32_IE
#define _WIN32_IE 0x0A00
#endif

#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#undef min
#undef max

#pragma comment(lib, "winmm.lib")

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <stdio.h>

#ifndef __MINGW32__
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <directxmath.h>
#include <directxcolors.h>
#include <dsound.h>
#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "dxguid.lib")
#endif

#include <io.h>
#include <fcntl.h>

#include <list>
#include <vector>

#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <thread>

using std::list;
using std::vector;
using std::shared_ptr;
using std::unique_ptr;
using std::string;
using std::unordered_map;
using std::unordered_set;
using std::thread;
using namespace std::literals::string_literals;
