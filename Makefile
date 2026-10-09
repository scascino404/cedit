# cedit - Classic Text Editor
#
# Everything is built into build/: the editor is build/cedit, the tests and
# review tools sit next to it, and object files go to build/obj/.

# clang builds this tree about 1.5x faster than gcc; make CC=gcc works too.
CC       = clang
CFLAGS   = -O2 -g
WARN     = -std=c89 -pedantic -Wall -Wextra -Wno-overlength-strings
# _GNU_SOURCE (added by sdl2-config) is not needed and enables C23 macros
SDL_CFLAGS := $(filter-out -D_GNU_SOURCE%,$(shell sdl2-config --cflags))
SDL_LIBS   := $(shell sdl2-config --libs)
PREFIX   = /usr/local

BUILD = build
OBJ_DIR = $(BUILD)/obj

CORE = editor window buffer undo utf8 util
APP  = main ui menu dialog path theme screen font font8x8 font12x12 logo \
       cursor config syntax langs $(CORE)
OBJ  = $(APP:%=$(OBJ_DIR)/%.o)
LIB  = $(filter-out $(OBJ_DIR)/main.o,$(OBJ))

all: $(BUILD)/cedit

$(BUILD)/cedit: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(SDL_LIBS)

$(OBJ_DIR)/%.o: src/%.c src/*.h | $(OBJ_DIR)/tests
	$(CC) $(WARN) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

$(OBJ_DIR)/tests/%.o: tests/%.c src/*.h | $(OBJ_DIR)/tests
	$(CC) $(WARN) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

$(OBJ_DIR)/tests:
	mkdir -p $@

# tests
$(BUILD)/test_buffer: $(OBJ_DIR)/tests/test_buffer.o $(OBJ_DIR)/buffer.o $(OBJ_DIR)/undo.o \
                      $(OBJ_DIR)/util.o
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/test_editor: $(OBJ_DIR)/tests/test_editor.o $(CORE:%=$(OBJ_DIR)/%.o)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/test_window: $(OBJ_DIR)/tests/test_window.o $(CORE:%=$(OBJ_DIR)/%.o)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/test_syntax: $(OBJ_DIR)/tests/test_syntax.o $(OBJ_DIR)/syntax.o $(OBJ_DIR)/langs.o \
                      $(OBJ_DIR)/buffer.o $(OBJ_DIR)/util.o
	$(CC) $(CFLAGS) -o $@ $^

# drives the real UI headlessly (SDL's offscreen video driver)
$(BUILD)/test_ui: $(OBJ_DIR)/tests/test_ui.o $(LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(SDL_LIBS)

tests: $(BUILD)/test_buffer $(BUILD)/test_editor $(BUILD)/test_window $(BUILD)/test_syntax \
       $(BUILD)/test_ui

check: tests
	$(BUILD)/test_buffer
	$(BUILD)/test_editor
	$(BUILD)/test_window
	$(BUILD)/test_syntax
	$(BUILD)/test_ui

# review tools: render the fonts / drive the UI headlessly into images
$(BUILD)/fontsheet: $(OBJ_DIR)/tests/fontsheet.o $(OBJ_DIR)/font.o \
                    $(OBJ_DIR)/font8x8.o $(OBJ_DIR)/font12x12.o \
                    $(OBJ_DIR)/font20x20.o $(OBJ_DIR)/utf8.o
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/uishot: $(OBJ_DIR)/tests/uishot.o $(LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(SDL_LIBS)

tools: $(BUILD)/fontsheet $(BUILD)/uishot

# benchmarks (see tests/bench.c). The C and Markdown files are made from the
# sources of a pinned commit, so that they stay the same as the code changes.
# The frame's GPU-side SDL calls are wrapped into no-ops, and screen_present
# is wrapped to time it.
BENCH_REV  = ac8595c
BENCH_DATA = $(BUILD)/bench-data
BENCH_WRAP = -Wl,--wrap=screen_present,--wrap=SDL_RenderClear,--wrap=SDL_RenderCopy \
             -Wl,--wrap=SDL_RenderFillRect,--wrap=SDL_RenderPresent

$(BUILD)/bench: $(OBJ_DIR)/tests/bench.o $(LIB)
	$(CC) $(CFLAGS) $(BENCH_WRAP) -o $@ $^ $(SDL_LIBS)

$(BENCH_DATA)/seed:
	mkdir -p $@
	git archive $(BENCH_REV) src README.md | tar -x -C $@

# make bench BENCH_ARGS="-o base.tsv", then after a change
# make bench BENCH_ARGS="-b base.tsv" (see tests/bench.c for the options)
bench: $(BUILD)/bench $(BENCH_DATA)/seed
	$(BUILD)/bench -d $(BENCH_DATA) $(BENCH_ARGS)

# Compilation database for clangd / VSCode IntelliSense, recorded by bear
# from a full rebuild of every target.
compdb:
	rm -rf $(OBJ_DIR)
	bear --output $(BUILD)/compile_commands.json -- $(MAKE) all tests tools

install: $(BUILD)/cedit
	install -Dm755 $(BUILD)/cedit $(DESTDIR)$(PREFIX)/bin/cedit

clean:
	rm -rf $(OBJ_DIR) $(BUILD)/cedit $(BUILD)/test_buffer $(BUILD)/test_editor \
	       $(BUILD)/test_window $(BUILD)/test_syntax $(BUILD)/test_ui \
	       $(BUILD)/fontsheet $(BUILD)/uishot $(BUILD)/bench

.PHONY: all tests check tools bench compdb install clean
