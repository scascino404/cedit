# cedit - Classic Text Editor
#
#   cmake --preset default            configure (once): clang + Ninja, into build/
#   cmake --build --preset default    builds build/cedit, the tests and the tools
#   ctest --preset default            runs the tests
#
# The editor is build/cedit, the tests and review tools sit next to it.
# Without the preset, cmake -B build uses the system's default compiler and
# generator; cmake --preset default -DCMAKE_C_COMPILER=gcc picks gcc instead.
cmake_minimum_required(VERSION 3.27)

project(cedit C)

# -O2 -g unless told otherwise
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# for clangd / VSCode IntelliSense: build/compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# linked into every target below
add_library(cedit_warnings INTERFACE)
target_compile_options(cedit_warnings INTERFACE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-pedantic -Wall -Wextra -Wno-overlength-strings>)

find_package(SDL2 REQUIRED CONFIG)

# The editing core links without SDL (its headers only need SDL's types),
# so the tests that use only it don't link SDL either.
add_library(cedit_core STATIC
    src/editor.c src/window.c src/buffer.c src/undo.c src/utf8.c src/util.c
    src/syntax.c src/langs.c)
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

add_executable(cedit src/main.c)
target_link_libraries(cedit PRIVATE cedit_ui cedit_warnings)

install(TARGETS cedit)

# tests
enable_testing()
foreach(t test_buffer test_editor test_window test_syntax)
    add_executable(${t} tests/${t}.c)
    target_link_libraries(${t} PRIVATE cedit_core cedit_warnings)
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# drives the real UI headlessly (SDL's offscreen video driver)
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
