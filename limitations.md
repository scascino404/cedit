# Limitations

What cedit v1 does **not** do yet that a user might expect from a modern text
editor. The most noticeable gaps come first; the rest are grouped by area.

## Most likely to be noticed

- **No horizontal scrollbar.** Without word wrap, long lines scroll
  horizontally with only the mouse wheel with `Shift` and the cursor.
- **No tabs or file tree.** Windows can be split to show several files, or
  one file in several places, but a file is open only while a window shows
  it: opening a file replaces the one in the focused window (after asking to
  save, unless another window shows it too).
- **No regex or whole-word search, and only one match is highlighted at a
  time.** Search is plain substring search, optionally case-sensitive.
  There's no incremental (search-as-you-type) find, and a search can't span
  lines.
- **No crash recovery or autosave.** Saves are atomic, so the file on disk is
  never half-written, but unsaved edits are lost if cedit or the machine
  crashes. There is no swap or backup file.
- **Characters outside the hand-drawn set show as a red box.** The fonts cover
  ASCII, Latin-1, typographic quotes and dashes, `€`, arrows, and box-drawing
  and block characters. CJK, emoji, Cyrillic, Greek and others show as a
  placeholder, as does a UTF-8 byte-order mark at the start of a file.

## Files

- Only UTF-8 (or plain ASCII) is supported. There is no encoding detection or
  conversion (UTF-16, Latin-1, Shift-JIS…), and BOMs are not handled.
- Line endings are kept as found (LF or CRLF), but there's no command to
  convert between them. Old Mac files (bare CR) show `^M` and are one long
  line.
- No reload or warning when another program changes the file on disk. If
  another program **truncates** the file while cedit has it open, cedit can
  crash (`SIGBUS`, a side effect of memory-mapping).
- No file locking, so two cedit instances can overwrite each other's saves.
- Saving replaces the file via rename. That breaks hard links and can't keep
  the owner when you edit someone else's file (permission bits are kept).
- No recent-files list, no remembered cursor position, and no session
  restore.
- The Open dialog hides dotfiles (type the name to open one). It has no path
  autocompletion and no file-type filter.
- The file menu (`Ctrl+P`) starts at the current file's directory and can't
  go up to its parent. It leaves out dotfiles, and has no filtering beyond
  jumping to the next name that starts with a typed letter.
- No printing and no export.

## Editing

- No multiple cursors, and no column (block) selection.
- No drag-and-drop of selected text. Dropping a *file* onto the window does
  open it.
- No middle-click paste of the X11/Wayland primary selection.
- `Tab` always inserts a tab character. There is no "insert spaces" option,
  and the tab width (4 or 8) only affects display.
- No auto-closing of brackets or quotes, no comment toggling, no
  duplicate/move line, no case conversion, no sort, and no
  trailing-whitespace trimming.
- Undo is linear, limited to 1000 steps (or 512 MiB of edits), and lost on
  close. There is no persistent undo and no undo tree.
- No spell checking.

## Syntax highlighting

- Highlighting is by lexing, not parsing: it knows comments, strings,
  keywords and a few types, not function names, variables or scopes.
- Not handled: shell here-documents, JavaScript regular expression literals
  (a quote inside `/'/` colors the rest of that line as a string), `${...}`
  inside JavaScript template strings (all of it is string), C++ raw strings
  with a delimiter (`R"x(...)x"`; `R"(...)"` works), and Markdown emphasis,
  links and indented code blocks.
- The language can't be chosen by hand, and there are no per-language
  settings. A file whose name isn't recognized (and has no `#!` line) is
  shown without highlighting. Languages are compiled in; there are no
  syntax definition files.
- Right after jumping far into a huge file, text inside a very long block
  comment or string can show the wrong colors for a moment (a few seconds
  per GB) until the background pass reaches it.

## Display

- No bracket matching, current-line highlight, visible whitespace, indent
  guides or minimap.
- Double-width (CJK) and combining characters each take one cell, so
  alignment is off for such text. No right-to-left or bidirectional text.
- One built-in bitmap font in three sizes (Small 12×12, Medium 8×8 doubled,
  Large 8×8 tripled). There are no other fonts and no fractional sizes.
- Two color themes (light and dark) on the fixed 16-color VGA palette. Colors
  can't be customized.
- Very long single lines (tens of MB with no newline) get slow to edit: each
  edit lexes the whole line again for highlighting and, with word wrap,
  breaks it into rows again, and the screen scans from the line start to
  the visible column.
- Word wrap breaks only at spaces and tabs (or anywhere in a word too long
  for a row), with no hyphenation and no indent for the continued rows.
  `Home` and `End` go to the line's ends, not the row's, and the scrollbar
  counts lines, not rows.

## Input and platform

- IME composition (e.g. for Chinese or Japanese input) is not displayed while
  composing; only the committed text arrives.
- Shortcuts are fixed (no key remapping), and macOS `Cmd` shortcuts are not
  mapped. `Ctrl` is used everywhere.
- POSIX only (Linux, BSD, macOS). Windows would need a small file layer to
  replace `mmap`, `realpath` and `rename`. Only Linux has been tested.
- No screen-reader or other accessibility support. cedit draws its own
  mouse pointer, so OS pointer size and locate-pointer settings don't apply
  inside its window.
  The pointer also shows up in screenshots and recordings, and can lag
  slightly over remote desktop.
- Settings (size, dark mode, auto-indent, line numbers, tab width) are only
  changed from the menus and saved to `~/.config/cedit/cedit.conf`. There is
  no settings dialog and no per-file or per-project setting.

## Testing

- The text store, undo and editing commands have randomized and unit tests
  (`make check`). The UI is only checked through the headless screenshot tool
  (`build/uishot`), not through automated assertions.
