/*
 * font.h - cedit's hand-drawn bitmap fonts.
 *
 * Glyph sources are ASCII-art tables (font8x8.c, font8x16.c). Box drawing and
 * block elements are generated from stroke rules, and accented Latin-1
 * letters are composed from a base letter plus a hand-drawn accent.
 */
#ifndef CEDIT_FONT_H
#define CEDIT_FONT_H

#define FONT_MAP_SIZE 0x2800

typedef struct Font {
    int w, h;
    int n;                      /* glyph count */
    unsigned char *bits;        /* h bytes per glyph, bit 7 = leftmost */
    short map[FONT_MAP_SIZE];   /* codepoint -> glyph, -1 if missing */
    int placeholder;            /* glyph for unknown codepoints */
} Font;

extern Font font_8x8;
extern Font font_8x16;

/* Source tables: "@XXXX" starts the glyph for codepoint XXXX (hex), followed
 * by h rows of '.' and '#'. NULL terminated. */
extern const char *const font8x8_src[];
extern const char *const font8x16_src[];

void font_init(void);
int font_has(const Font *f, unsigned long cp);
const unsigned char *font_glyph(const Font *f, unsigned long cp);

#endif
