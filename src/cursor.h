/*
 * cursor.h - hand-drawn mouse pointer and text cursor shapes.
 */
#ifndef CEDIT_CURSOR_H
#define CEDIT_CURSOR_H

#include <SDL.h>

/* ARGB pixels of the pointer at 1x (SDL_free them), its size and hot spot. */
Uint32 *pointer_pixels(int *w, int *h, int *hx, int *hy);

/* Row mask (FONT_BIT(0) = leftmost pixel) of a text cursor shape for a
 * cell of height h, at row y. */
unsigned long text_cursor_row(int shape, int h, int y);

#endif
