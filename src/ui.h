/*
 * ui.h - menus, dialogs, drawing and input handling.
 */
#ifndef CEDIT_UI_H
#define CEDIT_UI_H

#include "editor.h"
#include "screen.h"
#include "cursor.h"
#include "config.h"

#define FIELD_MAX 1024

typedef struct Widget {
    int kind, id;
    int x, y, w;            /* relative to the dialog */
    char label[128];
    char text[FIELD_MAX];
    size_t len, cur;
    int checked;
} Widget;

typedef struct Dialog {
    int kind;
    int x, y, w, h;
    int at_bottom;
    char title[64];
    Widget wd[24];
    int n, focus, def_id;
    /* file dialog */
    char dir[4096];
    char **items;
    int nitems, sel, scroll;
    int list_x, list_y, list_w, list_h;
    char msg[256];
    char overwrite[4096];
} Dialog;

typedef struct App {
    Screen scr;
    Editor ed;
    int running;

    int menu;               /* open menu index or -1 */
    int menu_item;
    int alt_tap;

    Dialog dlg;
    int pending;            /* action waiting for "save changes?" */
    char pending_path[4096];

    char msg[256];
    unsigned long msg_until;

    int blink_on;
    unsigned long blink_next;
    int focused;

    int drag;               /* 1 selecting text, 2 scrollbar thumb */
    int drag_grab;
    int mouse_x, mouse_y;   /* in cells */
    unsigned long click_time;
    int click_count, click_x, click_y;

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
