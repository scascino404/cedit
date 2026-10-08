/*
 * cursor.c - cedit's mouse pointers and text cursors, drawn by hand.
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

static const char *const ibeam[] = {
    "ooo ooo",
    "o##o##o",
    "ooo#ooo",
    "  o#o  ",
    "  o#o  ",
    "  o#o  ",
    "  o#o  ",
    "  o+o  ",
    "  o#o  ",
    "  o#o  ",
    "  o#o  ",
    "  o#o  ",
    "ooo#ooo",
    "o##o##o",
    "ooo ooo",
    NULL
};

static const char *const hourglass[] = {
    "###########",
    "#ooooooooo#",
    " #ooooooo# ",
    " #o#####o# ",
    " #oo###oo# ",
    "  #oo#oo#  ",
    "   #o#o#   ",
    "    #+#    ",
    "   #ooo#   ",
    "  #oo#oo#  ",
    " #ooo#ooo# ",
    " #oo###oo# ",
    " #o#####o# ",
    "#ooooooooo#",
    "###########",
    NULL
};

Uint32 *pointer_pixels(int which, int *w, int *h, int *hx, int *hy)
{
    const char *const *art = which == PTR_IBEAM ? ibeam
                           : which == PTR_WAIT ? hourglass : arrow;
    Uint32 *px;
    int x, y;

    *w = (int)strlen(art[0]);
    for (*h = 0; art[*h]; (*h)++)
        ;
    *hx = *hy = 0;
    px = (Uint32 *)SDL_malloc((size_t)*w * *h * sizeof(Uint32));
    if (!px)
        return NULL;
    for (y = 0; y < *h; y++)
        for (x = 0; x < *w; x++) {
            char p = art[y][x];
            px[y * *w + x] = p == 'o' ? 0xFFFFFFFFu : p == ' ' ? 0 : 0xFF000000u;
            if (p == '+') {
                *hx = x;
                *hy = y;
            }
        }
    return px;
}

/* Text cursors. Insert mode is the solid TempleOS block; overwrite mode is a
 * hollow box, so the character being replaced stays readable. */
static const char *const tc_overwrite16[16] = {
    "########",
    "########",
    "##....##",
    "##....##",
    "##....##",
    "##....##",
    "##....##",
    "##....##",
    "##....##",
    "##....##",
    "##....##",
    "##....##",
    "##....##",
    "##....##",
    "########",
    "########"
};

static const char *const tc_overwrite8[8] = {
    "########",
    "#......#",
    "#......#",
    "#......#",
    "#......#",
    "#......#",
    "#......#",
    "########"
};

unsigned char text_cursor_row(int shape, int h, int y)
{
    const char *row;
    unsigned char bits = 0;
    int x;

    if (shape == TCUR_INSERT)
        return 0xFF;
    if (shape != TCUR_OVERWRITE || y < 0 || y >= h)
        return 0;
    row = h == 8 ? tc_overwrite8[y] : tc_overwrite16[y];
    for (x = 0; x < 8; x++)
        if (row[x] == '#')
            bits |= (unsigned char)(0x80 >> x);
    return bits;
}
