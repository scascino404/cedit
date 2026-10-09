# cedit - Classic Text Editor
#
# Everything is built into build/: the editor is build/cedit, the tests and
# review tools sit next to it, and object files go to build/obj/.

# clang builds this tree faster than gcc (see README.md, "Build").
CC       = clang
CFLAGS   = -O2 -g
WARN     = -std=c89 -pedantic -Wall -Wextra -Wno-overlength-strings
# _GNU_SOURCE (added by sdl2-config) is not needed and enables C23 macros
SDL_CFLAGS := $(filter-out -D_GNU_SOURCE%,$(shell sdl2-config --cflags))
SDL_LIBS   := $(shell sdl2-config --libs)
PREFIX   = /usr/local

BUILD = build
OBJ_DIR = $(BUILD)/obj

CORE = editor buffer undo utf8 util
APP  = main ui menu dialog theme screen font font8x8 font20x20 cursor config \
       syntax langs $(CORE)
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

$(BUILD)/test_syntax: $(OBJ_DIR)/tests/test_syntax.o $(OBJ_DIR)/syntax.o $(OBJ_DIR)/langs.o \
                      $(OBJ_DIR)/buffer.o $(OBJ_DIR)/util.o
	$(CC) $(CFLAGS) -o $@ $^

tests: $(BUILD)/test_buffer $(BUILD)/test_editor $(BUILD)/test_syntax

check: tests
	$(BUILD)/test_buffer
	$(BUILD)/test_editor
	$(BUILD)/test_syntax

# review tools: render the fonts / drive the UI headlessly into images
$(BUILD)/fontsheet: $(OBJ_DIR)/tests/fontsheet.o $(OBJ_DIR)/font.o \
                    $(OBJ_DIR)/font8x8.o $(OBJ_DIR)/font20x20.o $(OBJ_DIR)/utf8.o
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/uishot: $(OBJ_DIR)/tests/uishot.o $(LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(SDL_LIBS)

tools: $(BUILD)/fontsheet $(BUILD)/uishot

# Compilation database for clangd / VSCode IntelliSense, recorded by bear
# from a full rebuild of every target.
compdb:
	rm -rf $(OBJ_DIR)
	bear --output $(BUILD)/compile_commands.json -- $(MAKE) all tests tools

install: $(BUILD)/cedit
	install -Dm755 $(BUILD)/cedit $(DESTDIR)$(PREFIX)/bin/cedit

clean:
	rm -rf $(OBJ_DIR) $(BUILD)/cedit $(BUILD)/test_buffer $(BUILD)/test_editor \
	       $(BUILD)/test_syntax \
	       $(BUILD)/fontsheet $(BUILD)/uishot

.PHONY: all tests check tools compdb install clean
