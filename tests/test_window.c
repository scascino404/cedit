/*
 * test_window.c - the window tree: splitting, layout, resizing, finding
 * windows, closing.
 */
#include "../src/window.h"

#include <stdio.h>

static int failures;

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static int rect(const Window *w, int x, int y, int width, int height)
{
    return w->x == x && w->y == y && w->w == width && w->h == height;
}

int main(void)
{
    EdOptions opt;
    Window *root, *a, *b, *c, *w;

    ed_options_init(&opt);
    root = a = win_new(&opt);
    win_layout(root, 0, 1, 80, 24);
    CHECK(rect(a, 0, 1, 80, 24));

    /* a vertical split: a on the left, the new window on the right */
    b = win_split(&root, a, 1);
    CHECK(b && root != a && root->a == a && root->b == b);
    CHECK(b->ed.doc == a->ed.doc && ed_views(&a->ed) == 2);
    win_layout(root, 0, 1, 80, 24);
    CHECK(rect(a, 0, 1, 40, 24) && rect(b, 40, 1, 40, 24));

    /* then b stacked: b on top, c below */
    c = win_split(&root, b, 0);
    win_layout(root, 0, 1, 80, 24);
    CHECK(rect(b, 40, 1, 40, 12) && rect(c, 40, 13, 40, 12));

    /* order */
    CHECK(win_first(root) == a && win_next(a) == b && win_next(b) == c);
    CHECK(win_next(c) == NULL && win_prev(c) == b && win_prev(b) == a);
    CHECK(win_last(root) == c && win_prev(a) == NULL);

    /* finding windows and borders */
    CHECK(win_at(root, 0, 1) == a && win_at(root, 79, 24) == c);
    CHECK(win_at(root, 50, 0) == NULL);
    CHECK(win_border_at(root, 40, 5) == root);
    CHECK(win_border_at(root, 39, 5) == NULL);      /* a's scrollbar */
    CHECK(win_border_at(root, 50, 12) == b->parent);
    CHECK(win_border_at(root, 50, 13) == b->parent);
    CHECK(win_border_at(root, 50, 14) == NULL);
    CHECK(win_neighbor(root, a, 1, 0, 5, 20) == c);
    CHECK(win_neighbor(root, a, 1, 0, 5, 3) == b);
    CHECK(win_neighbor(root, c, 0, -1, 50, 20) == b);
    CHECK(win_neighbor(root, a, -1, 0, 5, 3) == NULL);

    /* resizing stops at the smallest window size */
    win_resize(root, 60);
    win_layout(root, 0, 1, 80, 24);
    CHECK(a->w == 60 && b->w == 20);
    win_resize(root, 75);
    win_layout(root, 0, 1, 80, 24);
    CHECK(a->w == 80 - WIN_MIN_W);
    win_resize(root, 2);
    win_layout(root, 0, 1, 80, 24);
    CHECK(a->w == WIN_MIN_W);
    /* the share is kept when the screen changes size */
    win_resize(root, 40);
    win_layout(root, 0, 1, 120, 30);
    CHECK(a->w == 60 && b->w == 60 && c->h == 15);

    /* too small to split */
    win_layout(root, 0, 1, 30, 24);
    CHECK(win_split(&root, a, 1) == NULL);

    /* closing gives the area to the sibling, and focus to the nearest */
    w = win_close(&root, b);
    CHECK(w == c && root->b == c && c->parent == root);
    win_layout(root, 0, 1, 80, 24);
    CHECK(rect(c, 40, 1, 40, 24));
    w = win_close(&root, a);
    CHECK(w == c && root == c && c->parent == NULL);
    CHECK(ed_views(&c->ed) == 1);
    win_free(root);

    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("all window tests passed\n");
    return 0;
}
