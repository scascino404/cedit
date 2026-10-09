/*
 * ui.h - the application: commands, dialogs, drawing and input handling.
 */
#ifndef CEDIT_UI_H
#define CEDIT_UI_H

#include "dialog.h"
#include "editor.h"
#include "menu.h"
#include "screen.h"
#include "theme.h"
#include "window.h"

typedef struct App {
    Screen scr;
    Window *root;           /* the editor windows */
    Window *win;            /* the one with focus */
    EdOptions opt;          /* shared by all their views */
    const Theme *theme;
    int running;

    MenuBar menu;
    int alt_tap;            /* Alt pressed alone: toggles the menu on release */

    Dialog dlg;
    char dir[4096];         /* directory shown by the file dialogs, and the
                               top of the file menu */
    char overwrite[4096];   /* existing file the user agreed to replace */
    int pending;            /* action waiting for "save changes?" */
    char pending_path[4096];

    char msg[256];          /* message in the focused window's bottom
                               border, shown until msg_until */
    unsigned long msg_until;

    int blink_on;
    unsigned long blink_next;
    int focused;

    int drag;               /* 1 selecting text, 2 scrollbar thumb,
                               3 the border of split drag_split */
    int drag_grab;
    Window *drag_split;
    int mouse_x, mouse_y;   /* in cells */
    unsigned long click_time;
    int click_count, click_x, click_y;

    int highlight;          /* syntax highlighting is on */

    int show_lnum;
    int dark;
    char title[512];
} App;

int app_init(App *a, int argc, char **argv);
void app_quit(App *a);
void app_event(App *a, const SDL_Event *e);
void app_tick(App *a);
void app_draw(App *a);
/* Milliseconds until the app needs to wake up without input. */
int app_timeout(App *a);

#endif
