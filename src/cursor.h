/*
 * cursor.h - hand-drawn mouse pointers and text cursor shapes.
 */
#ifndef CEDIT_CURSOR_H
#define CEDIT_CURSOR_H

#include <SDL.h>

enum { PTR_ARROW, PTR_IBEAM, PTR_WAIT, PTR_COUNT };

/* ARGB pixels of a pointer at 1x (SDL_free them), its size and hot spot. */
Uint32 *pointer_pixels(int which, int *w, int *h, int *hx, int *hy);

/* Row mask (bit 7 = leftmost pixel) of a text cursor shape for a cell of
 * height h, at row y. */
unsigned char text_cursor_row(int shape, int h, int y);

#endif
