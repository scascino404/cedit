<p align="center">
  <img src="docs/logo.svg" alt="cedit" width="474">
</p>

<p align="center"><b>Classic Text Editor</b></p>

cedit is a small, fast text editor that looks and feels like the text-mode
editors of the 80s and 90s.

<p>
  <img src="docs/screenshot.png" alt="cedit" width="49%">
  <img src="docs/screenshot-dark.png" alt="cedit in dark mode" width="49%">
</p>

cedit borrows from two places:

- **[TempleOS](https://templeos.org)**, Terry A. Davis's operating system.
  Its font, black ink on white paper, blue frames and thin arrow pointer
  are where cedit's look comes from.
- **MS-DOS EDIT**, the editor that came with DOS. A menu bar you can drive
  from the keyboard, simple dialog boxes, and no modes to learn: you open
  it and type.

## Features

- Plain typing with the usual `Ctrl` shortcuts.
- Huge files open at once and never freeze the editor.
- Split windows, showing different files or the same file in two places.
- Syntax highlighting for C, C++, Python, Shell, Makefile, JavaScript,
  TypeScript, JSON, Go, Rust, Lua and Markdown.
- Light and dark themes, three text sizes, word wrap and line numbers.

## Build

You need a C compiler, `make`, and the SDL2 development files:

| System | Install |
|---|---|
| Debian, Ubuntu | `sudo apt install build-essential clang libsdl2-dev` |
| Fedora | `sudo dnf install make clang SDL2-devel` |
| Arch | `sudo pacman -S base-devel clang sdl2` |
| macOS | `brew install sdl2` |

Then build it, and optionally install it:

```sh
make -j                 # builds build/cedit
sudo make install       # copies it to /usr/local/bin
```

The Makefile uses clang; run `make CC=gcc` to build with gcc instead. cedit
is developed and tested on Linux. Other Unix-like systems, macOS included,
should work but haven't been tried.

## Run

```sh
cedit [file]
```

If the file doesn't exist yet, cedit creates it when you first save. You can
also drop a file onto the window to open it.

A few keys to get started:

| Keys | Action |
|---|---|
| `F10` or `Alt` | Open the menu bar |
| `F1` | Show every keyboard shortcut |
| `Ctrl+O` / `Ctrl+S` | Open / save |
| `Ctrl+F` / `Ctrl+H` | Find / replace |
| `Ctrl+P` | Open a file from the current folder |
| `Ctrl+\` | Split the window |
| `Ctrl+-` / `Ctrl+=` | Smaller / larger text |
| `Ctrl+Q` | Quit |

Settings such as dark mode and text size are changed from the View and Edit
menus, and cedit remembers them in `~/.config/cedit/cedit.conf`.

## License

cedit is released under the [MIT License](LICENSE).
