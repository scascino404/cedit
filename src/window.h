/*
 * window.h - the editor windows: a tree of splits whose leaves are windows,
 * each a view (Editor) in its own frame.
 *
 * A split divides its area between two sides, side by side (vertical: a is
 * on the left) or stacked (a on top). The frames of neighbors touch, with
 * no gap between them. Knows nothing about SDL; the UI draws the windows.
 */
#ifndef CEDIT_WINDOW_H
#define CEDIT_WINDOW_H

#include "editor.h"

/* The smallest frame a window is given while there is room. */
#define WIN_MIN_W 12
#define WIN_MIN_H 5

typedef struct Window Window;
struct Window {
    Window *parent;
    Window *a, *b;          /* a split's sides; NULL in a window */
    int vertical;           /* split: side by side */
    double ratio;           /* split: a's share of the area */
    int x, y, w, h;         /* frame, in cells, set by win_layout */
    Editor ed;              /* window: its view */
};

/* A window on a new empty document. */
Window *win_new(EdOptions *opt);
/* Frees the tree under w, with its views. */
void win_free(Window *w);

/* Splits window w in two, side by side (vertical) or stacked, and returns
 * the new window, on the right or bottom: another view of w's document.
 * Returns NULL when w's frame is too small for two windows. *root is
 * updated when w is the root. Call win_layout after. */
Window *win_split(Window **root, Window *w, int vertical);
/* Closes window w, which must not be the root, giving its area to its
 * sibling, and returns the window nearest it. */
Window *win_close(Window **root, Window *w);

/* Places the tree in the given frame area. */
void win_layout(Window *root, int x, int y, int w, int h);
/* Sets split s so that its a side is size cells wide (or tall), as far as
 * the windows' smallest sizes allow. Call win_layout after. */
void win_resize(Window *s, int size);

/* The window at cell (x, y), or NULL. */
Window *win_at(Window *root, int x, int y);
/* The split whose border is at cell (x, y), or NULL: the left frame column
 * of a vertical split's b side, or the rows where a stacked split's sides
 * meet. */
Window *win_border_at(Window *root, int x, int y);
/* The window next to w in direction (dx, dy), across from cell (px, py)
 * in w, or NULL. */
Window *win_neighbor(Window *root, Window *w, int dx, int dy, int px, int py);

/* The windows in order: left to right, top to bottom. */
Window *win_first(Window *root);
Window *win_last(Window *root);
Window *win_next(Window *w);
Window *win_prev(Window *w);

#endif
