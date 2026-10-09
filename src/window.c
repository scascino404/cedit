/*
 * window.c - the tree of editor windows: splitting, closing, layout and
 * finding windows by position.
 */
#include "window.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

static Window *alloc_window(void)
{
    Window *w = (Window *)xmalloc(sizeof *w);
    memset(w, 0, sizeof *w);
    return w;
}

Window *win_new(EdOptions *opt)
{
    Window *w = alloc_window();
    ed_init(&w->ed, opt);
    return w;
}

void win_free(Window *w)
{
    if (w->a) {
        win_free(w->a);
        win_free(w->b);
    } else {
        ed_free(&w->ed);
    }
    free(w);
}

/* Puts n where old was in the tree. */
static void replace(Window **root, Window *old, Window *n)
{
    Window *p = old->parent;
    n->parent = p;
    if (!p)
        *root = n;
    else if (p->a == old)
        p->a = n;
    else
        p->b = n;
}

Window *win_split(Window **root, Window *w, int vertical)
{
    Window *s, *n;

    if (vertical ? w->w < 2 * WIN_MIN_W : w->h < 2 * WIN_MIN_H)
        return NULL;
    s = alloc_window();
    n = alloc_window();
    ed_init_view(&n->ed, &w->ed);
    replace(root, w, s);
    s->a = w;
    s->b = n;
    s->vertical = vertical;
    s->ratio = 0.5;
    s->x = w->x;
    s->y = w->y;
    s->w = w->w;
    s->h = w->h;
    w->parent = n->parent = s;
    return n;
}

Window *win_close(Window **root, Window *w)
{
    Window *p = w->parent, *sib = p->a == w ? p->b : p->a;
    Window *near = p->a == w ? win_first(sib) : win_last(sib);

    replace(root, p, sib);
    ed_free(&w->ed);
    free(w);
    free(p);
    return near;
}

/* The smallest width (vertical) or height the windows under w fit in. */
static int min_size(const Window *w, int vertical)
{
    int a, b;
    if (!w->a)
        return vertical ? WIN_MIN_W : WIN_MIN_H;
    a = min_size(w->a, vertical);
    b = min_size(w->b, vertical);
    if (w->vertical == vertical)
        return a + b;
    return a > b ? a : b;
}

/* How many of total cells split s gives its a side for a wanted size. */
static int side_a(const Window *s, int total, int want)
{
    int ma = min_size(s->a, s->vertical), mb = min_size(s->b, s->vertical);
    if (total < ma + mb)                /* too small: share in proportion */
        return total * ma / (ma + mb);
    if (want < ma)
        return ma;
    if (want > total - mb)
        return total - mb;
    return want;
}

void win_layout(Window *w, int x, int y, int width, int height)
{
    int n;
    w->x = x;
    w->y = y;
    w->w = width;
    w->h = height;
    if (!w->a)
        return;
    if (w->vertical) {
        n = side_a(w, width, (int)(w->ratio * width + 0.5));
        win_layout(w->a, x, y, n, height);
        win_layout(w->b, x + n, y, width - n, height);
    } else {
        n = side_a(w, height, (int)(w->ratio * height + 0.5));
        win_layout(w->a, x, y, width, n);
        win_layout(w->b, x, y + n, width, height - n);
    }
}

void win_resize(Window *s, int size)
{
    int total = s->vertical ? s->w : s->h;
    if (total > 0)
        s->ratio = (double)side_a(s, total, size) / total;
}

static int contains(const Window *w, int x, int y)
{
    return x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h;
}

Window *win_at(Window *w, int x, int y)
{
    Window *r;
    if (!contains(w, x, y))
        return NULL;
    if (!w->a)
        return w;
    r = win_at(w->a, x, y);
    return r ? r : win_at(w->b, x, y);
}

Window *win_border_at(Window *w, int x, int y)
{
    Window *r;
    if (!w->a || !contains(w, x, y))
        return NULL;
    if (w->vertical ? x == w->b->x : y == w->b->y || y == w->b->y - 1)
        return w;
    r = win_border_at(w->a, x, y);
    return r ? r : win_border_at(w->b, x, y);
}

Window *win_neighbor(Window *root, Window *w, int dx, int dy, int px, int py)
{
    if (dx)
        return win_at(root, dx > 0 ? w->x + w->w : w->x - 1, py);
    return win_at(root, px, dy > 0 ? w->y + w->h : w->y - 1);
}

Window *win_first(Window *w)
{
    while (w->a)
        w = w->a;
    return w;
}

Window *win_last(Window *w)
{
    while (w->b)
        w = w->b;
    return w;
}

Window *win_next(Window *w)
{
    while (w->parent && w->parent->b == w)
        w = w->parent;
    return w->parent ? win_first(w->parent->b) : NULL;
}

Window *win_prev(Window *w)
{
    while (w->parent && w->parent->a == w)
        w = w->parent;
    return w->parent ? win_last(w->parent->a) : NULL;
}
