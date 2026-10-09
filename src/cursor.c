/*
 * cursor.c - cedit's mouse pointer and text cursors, drawn by hand.
 *
 * Pointer art: '#' black, 'o' white, ' ' transparent; '+' marks the hot spot
 * (drawn black). The pointer is drawn by screen.c, not by the OS.
 */
#include "cursor.h"
#include "screen.h"

#include <string.h>

/* The TempleOS pointer: a thin diagonal arrow, 1 px black with a white
 * outline. */
static const char *const arrow[] = {
    "oooooo     ",
    "o+####o    ",
    "o##oooo    ",
    "o#o#o      ",
    "o#oo#o     ",
    "o#o o#o    ",
    " oo  o#o   ",
    "      o#o  ",
    "       o#o ",
    "        o#o",
    "         oo",
    NULL
};

Uint32 *pointer_pixels(int *w, int *h, int *hx, int *hy)
{
    Uint32 *px;
    int x, y;

    *w = (int)strlen(arrow[0]);
    for (*h = 0; arrow[*h]; (*h)++)
        ;
    *hx = *hy = 0;
    px = (Uint32 *)SDL_malloc((size_t)*w * *h * sizeof(Uint32));
    if (!px)
        return NULL;
    for (y = 0; y < *h; y++)
        for (x = 0; x < *w; x++) {
            char p = arrow[y][x];
            px[y * *w + x] = p == 'o' ? 0xFFFFFFFFu : p == ' ' ? 0 : 0xFF000000u;
            if (p == '+') {
                *hx = x;
                *hy = y;
            }
        }
    return px;
}

/* Text cursors. Insert mode is the solid TempleOS block; overwrite mode is a
 * hollow box, so the character being replaced stays readable. Its lines are
 * as thick as the font's strokes: 1 px at 8 px, 2 at 12 and 3 at 20. */
unsigned long text_cursor_row(int shape, int h, int y)
{
    int t = (h + 4) / 8, x;
    unsigned long bits = 0;

    if (shape == TCUR_INSERT)
        return 0xFFFFFFFFUL;
    if (shape != TCUR_OVERWRITE || y < 0 || y >= h)
        return 0;
    for (x = 0; x < h; x++)         /* the cell is square */
        if (y < t || y >= h - t || x < t || x >= h - t)
            bits |= FONT_BIT(x);
    return bits;
}
