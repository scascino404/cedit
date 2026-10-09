/*
 * font.h - cedit's hand-drawn bitmap fonts.
 *
 * Glyph sources are ASCII-art tables (font8x8.c, font12x12.c, font20x20.c).
 * Box drawing and block elements are generated from stroke rules, and
 * accented Latin-1 letters are composed from a base letter plus a
 * hand-drawn accent.
 */
#ifndef CEDIT_FONT_H
#define CEDIT_FONT_H

#define FONT_MAP_SIZE 0x2800

typedef struct Font {
    int w, h;
    int n;                      /* glyph count */
    unsigned long *bits;        /* h rows per glyph, FONT_BIT(0) leftmost */
    short map[FONT_MAP_SIZE];   /* codepoint -> glyph, -1 if missing */
    int placeholder;            /* glyph for unknown codepoints */
} Font;

/* The pixel at column x of a glyph row; fonts are at most 32 px wide. */
#define FONT_BIT(x) (0x80000000UL >> (x))

extern Font font_8x8;
extern Font font_12x12;
extern Font font_20x20;

/* Source tables: "@XXXX" starts the glyph for codepoint XXXX (hex), followed
 * by h rows of '.' and '#'. NULL terminated. */
extern const char *const font8x8_src[];
extern const char *const font12x12_src[];
extern const char *const font20x20_src[];

void font_init(void);
int font_has(const Font *f, unsigned long cp);
const unsigned long *font_glyph(const Font *f, unsigned long cp);

#endif
