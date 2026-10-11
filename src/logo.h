/*
 * logo.h - cedit's logo, shown in the About box, and its icon.
 */
#ifndef CEDIT_LOGO_H
#define CEDIT_LOGO_H

#define LOGO_W 79
#define LOGO_H 15

/* Fills px with the logo's LOGO_W x LOGO_H pixels, row by row, as palette
 * colors (PIC_CLEAR where the background shows). */
void logo_pixels(unsigned char *px);

/* The icon: the logo's "c" and cursor, centered in a square of ICON_SIZE
 * pixels. data/cedit.svg and data/cedit.ico are drawn from it. */
#define ICON_SIZE 32
void icon_pixels(unsigned char *px);

#endif
