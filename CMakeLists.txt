# cedit - Classic Text Editor
#
# The Windows build (on Linux and macOS, the Makefile is the build, though
# this one works there too):
#
#   cmake -B build
#   cmake --build build --config Release    # build/Release/cedit.exe
#   ctest --test-dir build -C Release       # the tests
#
# SDL2 is the one CMake finds (point SDL2_DIR at its cmake folder to choose
# one) or else, on Windows, the official development package for Visual
# C++, downloaded into the build directory. SDL2.dll is copied next to the
# programs, and is all cedit.exe needs to run: the C runtime is linked in.

cmake_minimum_required(VERSION 3.21)
project(cedit C)

set(SDL2_VERSION 2.32.10)
set(SDL2_SHA256 af347939395a58b365846aaea27391e69f9ec9d4dd650d6ac40802159b418a6e)

# Visual C++, or clang for its ABI (not MinGW)
if(WIN32 AND NOT MINGW)
    set(msvc_abi ON)
endif()

find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
    if(NOT msvc_abi)
        message(FATAL_ERROR "SDL2 not found: install its development files "
                            "(see README.md), or set SDL2_DIR")
    endif()
    set(sdl_dir ${CMAKE_BINARY_DIR}/_deps/SDL2-${SDL2_VERSION})
    if(NOT EXISTS ${sdl_dir}/cmake/sdl2-config.cmake)
        set(sdl_zip ${CMAKE_BINARY_DIR}/_deps/SDL2-devel-${SDL2_VERSION}-VC.zip)
        message(STATUS "Downloading SDL2 ${SDL2_VERSION}")
        file(DOWNLOAD
             https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-VC.zip
             ${sdl_zip} EXPECTED_HASH SHA256=${SDL2_SHA256})
        file(ARCHIVE_EXTRACT INPUT ${sdl_zip} DESTINATION ${CMAKE_BINARY_DIR}/_deps)
        file(REMOVE ${sdl_zip})
    endif()
    find_package(SDL2 CONFIG REQUIRED PATHS ${sdl_dir} NO_DEFAULT_PATH)
endif()

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT msvc_abi)
    # (clang's intrin.h, which SDL.h includes there, needs inline)
    set(CMAKE_C_EXTENSIONS OFF)
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-pedantic -Wall -Wextra -Wno-overlength-strings)
endif()
if(msvc_abi)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(WIN32)
    # each program's main is its entry point (see cedit_program)
    add_compile_definitions(SDL_MAIN_HANDLED)
endif()

# the editor without SDL, the fonts, and the rest of the app but main
add_library(cedit_core STATIC
    src/editor.c src/window.c src/buffer.c src/undo.c src/utf8.c src/util.c
    src/syntax.c src/langs.c)
add_library(cedit_fonts STATIC
    src/font.c src/font8x8.c src/font12x12.c src/font20x20.c)
add_library(cedit_ui STATIC
    src/ui.c src/menu.c src/dialog.c src/path.c src/theme.c src/screen.c
    src/logo.c src/cursor.c src/config.c)
target_link_libraries(cedit_ui PUBLIC cedit_fonts cedit_core SDL2::SDL2)
get_target_property(sdl_type SDL2::SDL2 TYPE)
if(sdl_type STREQUAL "SHARED_LIBRARY")
    set(sdl_shared ON)
endif()

# A program of the build, linked with libs. On Windows it gets the
# manifest, and SDL2.dll beside it if it uses SDL.
function(cedit_program name source)
    cmake_parse_arguments(arg "WIN32;EXCLUDE_FROM_ALL" "" "LIBS" ${ARGN})
    set(flags)
    if(arg_WIN32)
        list(APPEND flags WIN32)
    endif()
    if(arg_EXCLUDE_FROM_ALL)
        list(APPEND flags EXCLUDE_FROM_ALL)
    endif()
    add_executable(${name} ${flags} ${source})
    target_link_libraries(${name} PRIVATE ${arg_LIBS})
    if(WIN32)
        target_sources(${name} PRIVATE src/cedit.manifest)
        if(msvc_abi AND arg_WIN32)
            # main, not WinMain (MinGW calls main anyway)
            target_link_options(${name} PRIVATE "LINKER:/ENTRY:mainCRTStartup")
        endif()
        if(cedit_ui IN_LIST arg_LIBS AND sdl_shared)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:${name}>)
        endif()
    endif()
endfunction()

cedit_program(cedit src/main.c WIN32 LIBS cedit_ui)

option(CEDIT_TESTS "Build the tests" ON)
if(CEDIT_TESTS)
    enable_testing()
    foreach(t buffer editor window syntax)
        cedit_program(test_${t} tests/test_${t}.c LIBS cedit_core)
        add_test(NAME ${t} COMMAND test_${t})
    endforeach()
    # drives the real UI headlessly (SDL's offscreen video driver)
    cedit_program(test_ui tests/test_ui.c LIBS cedit_ui)
    add_test(NAME ui COMMAND test_ui)
endif()

# review tools: render the fonts / drive the UI headlessly into images
# (cmake --build build --target tools)
cedit_program(fontsheet tests/fontsheet.c EXCLUDE_FROM_ALL LIBS cedit_fonts cedit_core)
cedit_program(uishot tests/uishot.c EXCLUDE_FROM_ALL LIBS cedit_ui)
add_custom_target(tools DEPENDS fontsheet uishot)

install(TARGETS cedit)
if(WIN32 AND sdl_shared)
    install(FILES $<TARGET_FILE:SDL2::SDL2> TYPE BIN)
endif()
