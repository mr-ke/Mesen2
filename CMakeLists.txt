cmake_minimum_required(VERSION 3.15)
project(MesenCore VERSION 2.1.1 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CROSSCOMPILING AND WIN32)
    message(STATUS "Cross-compiling for Windows from ${CMAKE_HOST_SYSTEM_NAME}")
endif()

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(MINGW_BUILD TRUE)
    message(STATUS "Detected MinGW build - using SDL instead of DirectX")
endif()

option(BUILD_SHARED_LIBS "Build shared library" ON)
option(USE_SDL "Use SDL for rendering (required for cross-compilation)" ON)

if(WIN32 AND NOT USE_SDL)
    message(FATAL_ERROR "Windows builds must use SDL for cross-compilation support")
endif()

file(GLOB_RECURSE CORE_SOURCES "Core/*.cpp")
file(GLOB_RECURSE CORE_HEADERS "Core/*.h")

file(GLOB_RECURSE UTILITIES_SOURCES "Utilities/*.cpp")
file(GLOB_RECURSE UTILITIES_C_SOURCES "Utilities/*.c")

file(GLOB_RECURSE SEVENZIP_SOURCES "SevenZip/*.c")

file(GLOB_RECURSE LUA_SOURCES "Lua/*.c")

if(WIN32)
    list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/unix\\.c$")
    list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/unixdgram\\.c$")
    list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/unixstream\\.c$")
    list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/serial\\.c$")
endif()

file(GLOB_RECURSE SDL_SOURCES "Sdl/*.cpp")

file(GLOB_RECURSE INTEROP_SOURCES "InteropDLL/*.cpp")

set(PLATFORM_SOURCES "")

if(USE_SDL)
    find_package(SDL2 REQUIRED)
    include_directories(${SDL2_INCLUDE_DIRS})
    list(APPEND PLATFORM_SOURCES ${SDL_SOURCES})
    add_definitions(-DUSE_SDL)
endif()

if(WIN32)
    if(NOT MINGW_BUILD)
        file(GLOB_RECURSE WINDOWS_SOURCES "Windows/*.cpp")
        list(APPEND PLATFORM_SOURCES ${WINDOWS_SOURCES})
    endif()
elseif(UNIX AND NOT APPLE)
    file(GLOB_RECURSE LINUX_SOURCES "Linux/*.cpp")
    file(GLOB_RECURSE LIBEVDEV_SOURCES "Linux/libevdev/*.c")
    list(APPEND PLATFORM_SOURCES ${LINUX_SOURCES} ${LIBEVDEV_SOURCES})
elseif(APPLE)
    file(GLOB_RECURSE MACOS_SOURCES "MacOS/*.mm")
    list(APPEND PLATFORM_SOURCES ${MACOS_SOURCES})
endif()

add_library(MesenCore SHARED
    ${CORE_SOURCES}
    ${UTILITIES_SOURCES}
    ${UTILITIES_C_SOURCES}
    ${SEVENZIP_SOURCES}
    ${LUA_SOURCES}
    ${INTEROP_SOURCES}
    ${PLATFORM_SOURCES}
)

target_include_directories(MesenCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/Utilities
    ${CMAKE_CURRENT_SOURCE_DIR}/Sdl
    ${CMAKE_CURRENT_SOURCE_DIR}/InteropDLL
)

if(WIN32)
    if(NOT MINGW_BUILD)
        target_include_directories(MesenCore PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Windows
        )
    endif()
elseif(UNIX AND NOT APPLE)
    target_include_directories(MesenCore PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Linux
    )
elseif(APPLE)
    target_include_directories(MesenCore PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/MacOS
    )
endif()

target_compile_definitions(MesenCore PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    $<$<PLATFORM_ID:Windows>:WIN32>
    $<$<PLATFORM_ID:Windows>:_WINDOWS>
    $<$<PLATFORM_ID:Windows>:_USRDLL>
    $<$<PLATFORM_ID:Windows>:INTEROPDLL_EXPORTS>
)

if(MSVC)
    target_compile_options(MesenCore PRIVATE
        /W3
        /MP
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
    )
    target_link_options(MesenCore PRIVATE
        $<$<CONFIG:Release>:/LTCG>
    )
else()
    target_compile_options(MesenCore PRIVATE
        -Wall
        -fPIC
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-flto>
    )
    # Static linking for MinGW to avoid runtime DLL dependencies
    if(MINGW_BUILD OR (WIN32 AND CMAKE_CROSSCOMPILING))
        target_link_options(MesenCore PRIVATE
            -static-libgcc
            -static-libstdc++
        )
    endif()
endif()

if(WIN32)
    target_link_libraries(MesenCore PRIVATE
        ${SDL2_LIBRARIES}
        ws2_32
        kernel32
        user32
        gdi32
        winspool
        comdlg32
        advapi32
        shell32
        ole32
        oleaut32
        uuid
        odbc32
        odbccp32
        winmm
    )
elseif(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(MesenCore PRIVATE
        ${SDL2_LIBRARIES}
        ${X11_LIBRARIES}
        stdc++fs
        pthread
        dl
    )
elseif(APPLE)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(GAMECONTROLLER_FRAMEWORK GameController)
    target_link_libraries(MesenCore PRIVATE
        ${SDL2_LIBRARIES}
        ${FOUNDATION_FRAMEWORK}
        ${COCOA_FRAMEWORK}
        ${GAMECONTROLLER_FRAMEWORK}
    )
endif()

set_target_properties(MesenCore PROPERTIES
    OUTPUT_NAME "MesenCore"
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

if(WIN32)
    set_target_properties(MesenCore PROPERTIES
        PREFIX ""
        SUFFIX ".dll"
    )
elseif(UNIX AND NOT APPLE)
    set_target_properties(MesenCore PROPERTIES
        PREFIX "lib"
        SUFFIX ".so"
    )
elseif(APPLE)
    set_target_properties(MesenCore PROPERTIES
        PREFIX "lib"
        SUFFIX ".dylib"
    )
endif()

install(TARGETS MesenCore
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

message(STATUS "Build configuration:")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Using SDL: ${USE_SDL}")
message(STATUS "  Cross-compiling: ${CMAKE_CROSSCOMPILING}")
