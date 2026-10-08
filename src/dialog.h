/*
 * dialog.h - text-mode dialog boxes: labels, input fields, checkboxes,
 * buttons and an optional list box.
 *
 * The owner builds a dialog with dlg_begin() and dlg_add(), feeds it input,
 * and acts on the button ids that the input functions return. Dialog kinds
 * and widget ids other than the ones below are up to the owner.
 */
#ifndef CEDIT_DIALOG_H
#define CEDIT_DIALOG_H

#include <SDL.h>
#include "screen.h"
#include "theme.h"

#define FIELD_MAX 1024

enum { DLG_NONE };                          /* kind of a closed dialog */
enum { ID_NONE, ID_OK, ID_CANCEL, ID_USER };  /* Escape returns ID_CANCEL */

enum {
    W_LABEL,
    W_PATH,         /* dim label caption, then the tail of text */
    W_FIELD,
    W_CHECK,
    W_BUTTON
};

typedef struct Widget {
    int kind, id;
    int x, y, w;            /* relative to the dialog */
    char label[128];
    char text[FIELD_MAX];
    size_t len, cur;
    int checked;
    int stretch;            /* widens and narrows with the dialog */
} Widget;

typedef struct Dialog {
    int kind;
    int x, y, w, h;         /* placed and sized by dlg_draw() */
    int max_w, max_h;       /* the size it was built at, and the smallest it */
    int min_w, min_h;       /* shrinks to on a small screen (the same unless
                               the owner lowers them) */
    int at_bottom;          /* sit above the bottom border, not centered */
    char title[64];
    Widget wd[24];
    int n, focus;
    int def_id;             /* the button Enter presses */
    char msg[256];          /* error line above the buttons */
    int dirty;              /* set when the user edits a field or the list
                               selection moves; the owner clears it */

    /* List box rows, when list_h > 0. Items ending in '/' are drawn as
     * directories. With sel < 0 the list only scrolls. The list grows and
     * shrinks with the dialog, and so does the row of buttons' position,
     * which stays centered at the bottom. */
    int list_x, list_y, list_w, list_h, list_frame;
    int list_field;         /* id of a field that mirrors the selection */
    char **items;
    int nitems, sel, scroll;
} Dialog;

/* Starts a dialog laid out for w x h cells, at most. */
void dlg_begin(Dialog *d, int kind, const char *title, int w, int h);
void dlg_close(Dialog *d);
Widget *dlg_add(Dialog *d, int kind, int id, int x, int y, int w,
                const char *label);
/* Adds a centered row of buttons, which stays on the dialog's bottom row
 * of widgets (h - 3) when it is resized. */
void dlg_buttons(Dialog *d, int y, const int *ids, const char *const *labels,
                 int n);
Widget *dlg_find(Dialog *d, int id);

/* The list box: its rows' area, items (copied) and selection. */
void dlg_list(Dialog *d, int x, int y, int w, int h, int framed, int sel);
void dlg_list_add(Dialog *d, const char *item);
void dlg_list_clear(Dialog *d);

void field_set(Widget *f, const char *s);

void dlg_draw(Dialog *d, Screen *s, const Theme *t, int blink_on);

/* Input. These return the id of the button pressed, or ID_NONE. */
int dlg_key(Dialog *d, const SDL_KeyboardEvent *k);
void dlg_text(Dialog *d, const char *text);
int dlg_click(Dialog *d, int cx, int cy, int clicks);
void dlg_wheel(Dialog *d, int lines);

#endif
