/*
 * menu.h - the menu bar and its pull-down menus.
 *
 * The menu tables in menu.c list every command with its label and shortcut.
 * Two more menus are not on the bar but pop up anywhere: the context menu,
 * and the list menu, a tree of items that the owner supplies (such as
 * folders and files).
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
    CMD_SIZE_S, CMD_SIZE_M, CMD_SIZE_L, CMD_LINENUM, CMD_WRAP, CMD_TAB4, CMD_TAB8,
    CMD_DARK, CMD_AUTOINDENT, CMD_SPACES, CMD_HIGHLIGHT, CMD_FILES,
    CMD_SPLIT_V, CMD_SPLIT_H, CMD_CLOSEWIN, CMD_NEXTWIN, CMD_PREVWIN,
    CMD_HELP, CMD_ABOUT,
    CMD_LIST                /* CMD_LIST + i: item i of the list menu */
};

/* Command state bits, from the owner's CmdState function. */
enum { CMD_ENABLED = 1, CMD_CHECKED = 2 };
typedef int (*CmdState)(void *ctx, int cmd);

/* An item of the list menu. A folder's items follow it, one level deeper,
 * while it is open. */
enum { LI_FOLDER = 1, LI_OPEN = 2, LI_CHECKED = 4 };
typedef struct ListItem {
    char *label;
    char *keys;             /* shown in the right column */
    int depth;
    int flags;              /* LI_* */
} ListItem;

typedef struct MenuBar {
    int open;               /* index of the open menu, or -1 */
    int item;               /* highlighted item, or -1 */
    int ax, ay;             /* the cell a popup was opened at */
    int x, y;               /* its top-left cell, placed to fit */
    int top, rows;          /* first item shown, and how many fit */
    int cols, lines;        /* the area menus must fit in */

    ListItem *list;         /* the list menu's items */
    int nlist;
    int list_lw, list_kw;   /* their widest label (with its indent) and
                               right column */
    int tick_item, tick_pos;    /* the label scrolling news-ticker style,
                                   and how far it has moved */
    unsigned long tick_next;    /* when it moves next */
} MenuBar;

/* Fits the menus to the top-left cols x rows cells of the screen, which
 * the owner calls before menus open and whenever the screen changes size.
 * A menu too tall for them scrolls. */
void menu_layout(MenuBar *m, int cols, int rows);
/* Opens menu i, wrapping around at both ends. */
void menu_open(MenuBar *m, int i);
/* Opens the context menu with its corner at cell (cx, cy), flipped left or
 * up where it would not fit. */
void menu_popup(MenuBar *m, int cx, int cy);
/* The same for the list menu, with item highlighted (or -1). */
void menu_popup_list(MenuBar *m, int cx, int cy, int item);
void menu_close(MenuBar *m);
/* Index of the menu whose title has hotkey sym, or -1. */
int menu_with_hotkey(SDL_Keycode sym);
/* Columns the menu titles take on the bar. */
int menu_bar_width(void);

/* The list menu's items. Labels have no hotkeys; typing a letter moves to
 * the next item that starts with it. Choosing a closed folder (or Right on
 * it) runs its command too, and the owner inserts its items after it. */
void menu_list_clear(MenuBar *m);
/* Inserts an item before item i, or appends it with i = nlist. */
void menu_list_insert(MenuBar *m, int i, const char *label, const char *keys,
                      int depth, int flags);
/* Closes open folder i, removing its items. */
void menu_list_collapse(MenuBar *m, int i);
/* The folder that item i is in, or -1. */
int menu_list_parent(const MenuBar *m, int i);
/* Moves a highlighted label that is too long for its row. While one moves,
 * tick_item is its item and tick_next when it moves next. */
void menu_tick(MenuBar *m, unsigned long now);

void menu_draw(const MenuBar *m, Screen *s, const Theme *t, CmdState state,
               void *ctx);

/* Input. These return the command of the item chosen, or CMD_NONE. The
 * owner runs it, and closes the menu, if the command is enabled. */
int menu_key(MenuBar *m, SDL_Keycode sym);
int menu_click(MenuBar *m, int cx, int cy);
void menu_hover(MenuBar *m, int cx, int cy);
void menu_wheel(MenuBar *m, int lines);

#endif
