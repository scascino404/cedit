# cedit - Classic Text Editor
#
#   cmake --preset default            configure (once): clang + Ninja, into build/
#   cmake --build --preset default    builds build/cedit, the tests and the tools
#   ctest --preset default            runs the tests
#
# The editor is build/cedit, the tests and review tools sit next to it. The
# presets debug and asan (AddressSanitizer and UBSan) build the same into
# build-debug/ and build-asan/, for debugging.
# Without the preset, cmake -B build uses the system's default compiler and
# generator; cmake --preset default -DCMAKE_C_COMPILER=gcc picks gcc instead.
#
# On Windows, with Visual Studio (see README.md):
#   cmake -B build
#   cmake --build build --config Release    builds build/Release/cedit.exe
#   ctest --test-dir build -C Release
#
# SDL2 is built from source, downloaded into the build directory the first
# time, and linked in, as is the C runtime on Windows: cedit is a single file
# to copy anywhere. On macOS it is built for the Mac's own processor;
# -DCMAKE_OSX_ARCHITECTURES=x86_64 (or arm64, or "arm64;x86_64" for both)
# picks another. On Linux it needs the C library at the version it was built
# with or newer, so build the ones to hand out on an older distribution.
cmake_minimum_required(VERSION 3.27)

# macOS: runs from macOS 11 on (set before project())
if(CMAKE_HOST_APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS to run on")
endif()

project(cedit C)

# a Release build unless told otherwise
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# for clangd / VSCode IntelliSense: build/compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Visual C++, or clang for its ABI (not MinGW)
if(WIN32 AND NOT MINGW)
    set(msvc_abi ON)
endif()

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT msvc_abi)
    # (clang's intrin.h, which SDL.h includes there, needs inline)
    set(CMAKE_C_EXTENSIONS OFF)
endif()

# linked into every target below
add_library(cedit_warnings INTERFACE)
if(MSVC)
    target_compile_options(cedit_warnings INTERFACE /W4)
else()
    target_compile_options(cedit_warnings INTERFACE
        $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-pedantic -Wall -Wextra -Wno-overlength-strings>)
endif()

if(WIN32)
    # each program's main is its entry point, not SDL2main's WinMain
    add_compile_definitions(SDL_MAIN_HANDLED)
endif()
if(msvc_abi)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# SDL2, built as a static library without the subsystems cedit doesn't use.
# It loads the window system's libraries (X11, Wayland, OpenGL...) itself
# when cedit starts, so only their development files are needed here.
include(FetchContent)
set(SDL2_VERSION 2.32.10)
FetchContent_Declare(SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-${SDL2_VERSION}.tar.gz
    URL_HASH SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165)
block()
    # SDL is C99, not C90
    unset(CMAKE_C_STANDARD)
    unset(CMAKE_C_EXTENSIONS)
    # SDL's options take these variables (its policies predate that)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(SDL_SHARED OFF)
    set(SDL_STATIC ON)
    set(SDL_TEST OFF)
    set(SDL2_DISABLE_SDL2MAIN ON)
    foreach(s AUDIO JOYSTICK HAPTIC HIDAPI POWER SENSOR LOCALE MISC)
        set(SDL_${s} OFF)
    endforeach()
    FetchContent_MakeAvailable(SDL2)

    # macOS: SDL links IOKit only for the subsystems above, but its Cocoa
    # video code uses it too
    if(APPLE)
        target_link_libraries(SDL2-static INTERFACE "-framework IOKit")
    endif()

    # SDL leaves out a window system whose development files are missing
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        get_directory_property(x11 DIRECTORY ${sdl2_SOURCE_DIR} DEFINITION HAVE_X11)
        get_directory_property(wayland DIRECTORY ${sdl2_SOURCE_DIR} DEFINITION HAVE_WAYLAND)
        if(NOT x11 AND NOT wayland)
            message(FATAL_ERROR "Neither X11's nor Wayland's development files "
                                "were found: install them (see README.md)")
        elseif(NOT wayland)
            message(WARNING "Wayland's development files weren't found: cedit "
                            "will run only under X11 (see README.md)")
        elseif(NOT x11)
            message(WARNING "X11's development files weren't found: cedit "
                            "will run only under Wayland (see README.md)")
        endif()
    endif()
endblock()
# (SDL2::SDL2 is the static library)

# what cedit needs from the system (src/sys.h)
if(WIN32)
    set(sys_src src/sys_win32.c)
else()
    set(sys_src src/sys_posix.c)
endif()

# The editing core links without SDL (its headers only need SDL's types),
# so the tests that use only it don't link SDL either.
add_library(cedit_core STATIC
    src/editor.c src/window.c src/buffer.c src/undo.c src/utf8.c src/util.c
    src/syntax.c src/langs.c ${sys_src})
target_link_libraries(cedit_core
    PUBLIC $<COMPILE_ONLY:SDL2::SDL2>
    PRIVATE cedit_warnings)

# everything but main(), shared by the editor, test_ui and the tools
add_library(cedit_ui STATIC
    src/ui.c src/menu.c src/dialog.c src/path.c src/theme.c src/screen.c
    src/font.c src/font8x8.c src/font12x12.c src/logo.c src/cursor.c
    src/config.c)
target_link_libraries(cedit_ui
    PUBLIC cedit_core SDL2::SDL2
    PRIVATE cedit_warnings)

add_executable(cedit WIN32 src/main.c)
target_link_libraries(cedit PRIVATE cedit_ui cedit_warnings)

install(TARGETS cedit)

if(WIN32)
    # every program takes and gives file names in UTF-8 (see the manifest)
    target_sources(cedit_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/cedit.manifest)
    if(msvc_abi)
        # main, not WinMain (MinGW calls main anyway)
        target_link_options(cedit PRIVATE "LINKER:/ENTRY:mainCRTStartup")
    endif()
endif()

# tests
enable_testing()
foreach(t test_buffer test_editor test_window test_syntax test_save)
    add_executable(${t} tests/${t}.c)
    target_link_libraries(${t} PRIVATE cedit_core cedit_warnings)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# drives the real UI headlessly (SDL's dummy video driver)
add_executable(test_ui tests/test_ui.c)
target_link_libraries(test_ui PRIVATE cedit_ui cedit_warnings)
add_test(NAME test_ui COMMAND test_ui)

# review tools: render the fonts / drive the UI headlessly into images
add_executable(fontsheet tests/fontsheet.c src/font.c src/font8x8.c
    src/font12x12.c src/font20x20.c src/utf8.c)
target_link_libraries(fontsheet PRIVATE cedit_warnings)

add_executable(uishot tests/uishot.c)
target_link_libraries(uishot PRIVATE cedit_ui cedit_warnings)

# benchmarks (see tests/bench.c), on files it generates into build/bench-data.
# The frame's GPU-side SDL calls are wrapped into no-ops, and screen_present
# is wrapped to time it. They need Linux: memory is read from /proc, and the
# wrapping is GNU ld's --wrap.
#   cmake --build --preset default --target run_bench   runs them all
#   build/bench -d build/bench-data -o base.tsv          saves a baseline,
#   build/bench -d build/bench-data -b base.tsv          compares with it
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(bench tests/bench.c)
    target_link_libraries(bench PRIVATE cedit_ui cedit_warnings)
    target_link_options(bench PRIVATE
        "LINKER:--wrap=screen_present,--wrap=SDL_RenderClear,--wrap=SDL_RenderCopy"
        "LINKER:--wrap=SDL_RenderFillRect,--wrap=SDL_RenderPresent")
    add_custom_target(run_bench
        COMMAND bench -d ${CMAKE_BINARY_DIR}/bench-data
        DEPENDS bench USES_TERMINAL)
endif()
