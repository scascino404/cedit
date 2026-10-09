/*
 * logo.h - cedit's logo, shown in the About box.
 */
#ifndef CEDIT_LOGO_H
#define CEDIT_LOGO_H

#define LOGO_W 79
#define LOGO_H 15

/* Fills px with the logo's LOGO_W x LOGO_H pixels, row by row, as palette
 * colors (PIC_CLEAR where the background shows). */
void logo_pixels(unsigned char *px);

#endif
