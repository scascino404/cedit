/*
 * menu.h - the menu bar and its pull-down menus.
 *
 * The menu tables in menu.c list every command with its label and shortcut.
 * One more menu, the context menu, is not on the bar but pops up anywhere.
 * The owner runs the commands and tells the menus which are enabled or
 * checked.
 */
#ifndef CEDIT_MENU_H
#define CEDIT_MENU_H

#include <SDL.h>
#include "screen.h"
#include "theme.h"

enum {
    CMD_NONE, CMD_NEW, CMD_OPEN, CMD_SAVE, CMD_SAVEAS, CMD_EXIT,
    CMD_UNDO, CMD_REDO, CMD_CUT, CMD_COPY, CMD_PASTE, CMD_DELETE, CMD_SELALL,
    CMD_OVERWRITE, CMD_FIND, CMD_FINDNEXT, CMD_FINDPREV, CMD_REPLACE, CMD_GOTO,
    CMD_SIZE_S, CMD_SIZE_N, CMD_SIZE_L, CMD_LINENUM, CMD_TAB4, CMD_TAB8,
    CMD_DARK, CMD_AUTOINDENT, CMD_HELP, CMD_ABOUT
};

/* Command state bits, from the owner's CmdState function. */
enum { CMD_ENABLED = 1, CMD_CHECKED = 2 };
typedef int (*CmdState)(void *ctx, int cmd);

typedef struct MenuBar {
    int open;               /* index of the open menu, or -1 */
    int item;               /* highlighted item, or -1 */
    int x, y;               /* top-left cell of the context menu */
} MenuBar;

/* Opens menu i, wrapping around at both ends. */
void menu_open(MenuBar *m, int i);
/* Opens the context menu with its corner at cell (cx, cy), flipped left or
 * up if it would not fit on a cols x rows screen. */
void menu_popup(MenuBar *m, int cx, int cy, int cols, int rows);
void menu_close(MenuBar *m);
/* Index of the menu whose title has hotkey sym, or -1. */
int menu_with_hotkey(SDL_Keycode sym);

void menu_draw(const MenuBar *m, Screen *s, const Theme *t, CmdState state,
               void *ctx);

/* Input. These return the command of the item chosen, or CMD_NONE. The
 * owner runs it, and closes the menu, if the command is enabled. */
int menu_key(MenuBar *m, SDL_Keycode sym);
int menu_click(MenuBar *m, int cx, int cy);
void menu_hover(MenuBar *m, int cx, int cy);

#endif
