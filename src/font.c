/*
 * font.c - builds the glyph tables from the hand-drawn sources.
 */
#include "font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Font font_8x8;
Font font_8x16;

#define MAX_GLYPHS 512

/* Internal map slots: private-use glyphs and the placeholder are stored in a
 * range of the map we never draw real characters for. */
#define PRIV_SLOT(cp)    (0x2600 + ((cp) - 0xE000))
#define PLACEHOLDER_SLOT 0x25FF

/* Private-use codepoints in the sources. */
#define ACC_GRAVE   0xE000
#define ACC_ACUTE   0xE001
#define ACC_CIRC    0xE002
#define ACC_TILDE   0xE003
#define ACC_DIAER   0xE004
#define ACC_RING    0xE005
#define ACC_CEDIL   0xE006
#define SMALL_CAPS  0xE020      /* squashed A E I O U N Y for accented caps */

static const char small_caps_order[] = "AEIOUNY";

/* Accented Latin-1 letters: codepoint, base letter, accent. */
static const struct {
    unsigned short cp, base, accent;
} compose[] = {
    {0xC0, 'A', ACC_GRAVE}, {0xC1, 'A', ACC_ACUTE}, {0xC2, 'A', ACC_CIRC},
    {0xC3, 'A', ACC_TILDE}, {0xC4, 'A', ACC_DIAER}, {0xC5, 'A', ACC_RING},
    {0xC7, 'C', ACC_CEDIL}, {0xC8, 'E', ACC_GRAVE}, {0xC9, 'E', ACC_ACUTE},
    {0xCA, 'E', ACC_CIRC},  {0xCB, 'E', ACC_DIAER}, {0xCC, 'I', ACC_GRAVE},
    {0xCD, 'I', ACC_ACUTE}, {0xCE, 'I', ACC_CIRC},  {0xCF, 'I', ACC_DIAER},
    {0xD1, 'N', ACC_TILDE}, {0xD2, 'O', ACC_GRAVE}, {0xD3, 'O', ACC_ACUTE},
    {0xD4, 'O', ACC_CIRC},  {0xD5, 'O', ACC_TILDE}, {0xD6, 'O', ACC_DIAER},
    {0xD9, 'U', ACC_GRAVE}, {0xDA, 'U', ACC_ACUTE}, {0xDB, 'U', ACC_CIRC},
    {0xDC, 'U', ACC_DIAER}, {0xDD, 'Y', ACC_ACUTE},
    {0xE0, 'a', ACC_GRAVE}, {0xE1, 'a', ACC_ACUTE}, {0xE2, 'a', ACC_CIRC},
    {0xE3, 'a', ACC_TILDE}, {0xE4, 'a', ACC_DIAER}, {0xE5, 'a', ACC_RING},
    {0xE7, 'c', ACC_CEDIL}, {0xE8, 'e', ACC_GRAVE}, {0xE9, 'e', ACC_ACUTE},
    {0xEA, 'e', ACC_CIRC},  {0xEB, 'e', ACC_DIAER}, {0xEC, 0x131, ACC_GRAVE},
    {0xED, 0x131, ACC_ACUTE}, {0xEE, 0x131, ACC_CIRC}, {0xEF, 0x131, ACC_DIAER},
    {0xF1, 'n', ACC_TILDE}, {0xF2, 'o', ACC_GRAVE}, {0xF3, 'o', ACC_ACUTE},
    {0xF4, 'o', ACC_CIRC},  {0xF5, 'o', ACC_TILDE}, {0xF6, 'o', ACC_DIAER},
    {0xF9, 'u', ACC_GRAVE}, {0xFA, 'u', ACC_ACUTE}, {0xFB, 'u', ACC_CIRC},
    {0xFC, 'u', ACC_DIAER}, {0xFD, 'y', ACC_ACUTE}, {0xFF, 'y', ACC_DIAER}
};

/* Box drawing: codepoint and arm styles (0 none, 1 single, 2 double). */
static const struct {
    unsigned short cp;
    unsigned char up, down, left, right;
} boxes[] = {
    {0x2500, 0, 0, 1, 1}, {0x2502, 1, 1, 0, 0}, {0x250C, 0, 1, 0, 1},
    {0x2510, 0, 1, 1, 0}, {0x2514, 1, 0, 0, 1}, {0x2518, 1, 0, 1, 0},
    {0x251C, 1, 1, 0, 1}, {0x2524, 1, 1, 1, 0}, {0x252C, 0, 1, 1, 1},
    {0x2534, 1, 0, 1, 1}, {0x253C, 1, 1, 1, 1},
    {0x2550, 0, 0, 2, 2}, {0x2551, 2, 2, 0, 0}, {0x2554, 0, 2, 0, 2},
    {0x2557, 0, 2, 2, 0}, {0x255A, 2, 0, 0, 2}, {0x255D, 2, 0, 2, 0},
    {0x2560, 2, 2, 0, 2}, {0x2563, 2, 2, 2, 0}, {0x2566, 0, 2, 2, 2},
    {0x2569, 2, 0, 2, 2}, {0x256C, 2, 2, 2, 2},
    {0x255F, 2, 2, 0, 1}, {0x2562, 2, 2, 1, 0}, {0x2564, 0, 1, 2, 2},
    {0x2567, 1, 0, 2, 2}
};

static unsigned char *glyph_ptr(Font *f, int i)
{
    return f->bits + (size_t)i * (size_t)f->h;
}

static int add_glyph(Font *f, unsigned long cp)
{
    int i;
    if (cp < FONT_MAP_SIZE && f->map[cp] >= 0)
        return f->map[cp];
    if (f->n >= MAX_GLYPHS) {
        fprintf(stderr, "cedit: font glyph table full\n");
        exit(1);
    }
    i = f->n++;
    memset(glyph_ptr(f, i), 0, (size_t)f->h);
    if (cp < FONT_MAP_SIZE)
        f->map[cp] = (short)i;
    return i;
}

static int lookup(const Font *f, unsigned long cp)
{
    if (cp < FONT_MAP_SIZE)
        return f->map[cp];
    if (cp == 0xFFFD)
        return f->placeholder;
    if (cp >= 0xE000 && cp < 0xE100) {
        return f->map[PRIV_SLOT(cp)];
    }
    return -1;
}

static void parse(Font *f, const char *const *src, const char *name)
{
    int i, row = -1, g = -1;

    for (i = 0; src[i]; i++) {
        const char *s = src[i];
        if (s[0] == '@') {
            unsigned long cp = strtoul(s + 1, NULL, 16);
            if (row >= 0 && row != f->h)
                fprintf(stderr, "cedit: %s: glyph before %s has %d rows\n",
                        name, s, row);
            if (cp >= 0xE000 && cp < 0xE100)
                cp = PRIV_SLOT(cp);
            else if (cp == 0xFFFD)
                cp = PLACEHOLDER_SLOT;
            g = add_glyph(f, cp);
            row = 0;
        } else if (g >= 0 && row < f->h) {
            int x;
            unsigned char bits = 0;
            if ((int)strlen(s) != f->w)
                fprintf(stderr, "cedit: %s: bad row \"%s\"\n", name, s);
            for (x = 0; x < f->w && s[x]; x++)
                if (s[x] == '#')
                    bits |= (unsigned char)(0x80 >> x);
            glyph_ptr(f, g)[row++] = bits;
        } else {
            fprintf(stderr, "cedit: %s: extra row \"%s\"\n", name, s);
        }
    }
    f->placeholder = f->map[PLACEHOLDER_SLOT];
}

static void hline(unsigned char *g, int y, int x0, int x1)
{
    int x;
    for (x = x0; x <= x1; x++)
        g[y] |= (unsigned char)(0x80 >> x);
}

static void vline(unsigned char *g, int x, int y0, int y1)
{
    int y;
    for (y = y0; y <= y1; y++)
        g[y] |= (unsigned char)(0x80 >> x);
}

/* Draws a box-drawing glyph from its arms so that joins always line up. */
static void make_box(Font *f, unsigned long cp, int up, int down, int left,
                     int right)
{
    unsigned char *g = glyph_ptr(f, add_glyph(f, cp));
    int cx = 3, cy = f->h / 2 - 1, w = f->w - 1, h = f->h - 1;
    int vd = up == 2 || down == 2, hd = left == 2 || right == 2;

    /* horizontal arms */
    if (right == 1)
        hline(g, cy, vd ? cx + 1 : cx, w);
    if (left == 1)
        hline(g, cy, 0, vd ? cx - 1 : cx);
    if (right == 2) {
        hline(g, cy - 1, up ? cx + 1 : down ? cx - 1 : cx, w);
        hline(g, cy + 1, down ? cx + 1 : up ? cx - 1 : cx, w);
    }
    if (left == 2) {
        hline(g, cy - 1, 0, up ? cx - 1 : down ? cx + 1 : cx);
        hline(g, cy + 1, 0, down ? cx - 1 : up ? cx + 1 : cx);
    }
    /* vertical arms */
    if (down == 1)
        vline(g, cx, hd ? cy + 1 : cy, h);
    if (up == 1)
        vline(g, cx, 0, hd ? cy - 1 : cy);
    if (down == 2) {
        vline(g, cx - 1, left ? cy + 1 : right ? cy - 1 : cy, h);
        vline(g, cx + 1, right ? cy + 1 : left ? cy - 1 : cy, h);
    }
    if (up == 2) {
        vline(g, cx - 1, 0, left ? cy - 1 : right ? cy + 1 : cy);
        vline(g, cx + 1, 0, right ? cy - 1 : left ? cy + 1 : cy);
    }
}

static void make_blocks(Font *f)
{
    int x, y;
    unsigned char *g;

    g = glyph_ptr(f, add_glyph(f, 0x2588));         /* full */
    memset(g, 0xFF, (size_t)f->h);
    g = glyph_ptr(f, add_glyph(f, 0x2580));         /* upper half */
    memset(g, 0xFF, (size_t)f->h / 2);
    g = glyph_ptr(f, add_glyph(f, 0x2584));         /* lower half */
    memset(g + f->h / 2, 0xFF, (size_t)f->h / 2);
    g = glyph_ptr(f, add_glyph(f, 0x258C));         /* left half */
    memset(g, 0xF0, (size_t)f->h);
    g = glyph_ptr(f, add_glyph(f, 0x2590));         /* right half */
    memset(g, 0x0F, (size_t)f->h);
    for (y = 0; y < f->h; y++) {
        unsigned char light = 0, medium = 0;
        for (x = 0; x < 8; x++) {
            if ((y % 2 == 0 && x % 4 == 0) || (y % 2 == 1 && x % 4 == 2))
                light |= (unsigned char)(0x80 >> x);
            if ((x + y) % 2 == 0)
                medium |= (unsigned char)(0x80 >> x);
        }
        glyph_ptr(f, add_glyph(f, 0x2591))[y] = light;
        glyph_ptr(f, add_glyph(f, 0x2592))[y] = medium;
        glyph_ptr(f, add_glyph(f, 0x2593))[y] = (unsigned char)~light;
    }
}

/* Accented letters: base | accent. Lowercase accents sit lower in tall
 * fonts; capitals use squashed letters when the font provides them. */
static void make_accented(Font *f, int lower_shift)
{
    size_t i;
    for (i = 0; i < sizeof compose / sizeof compose[0]; i++) {
        int base = -1, acc, y, shift, g;
        unsigned long b = compose[i].base;
        const char *sc = b < 128 ? strchr(small_caps_order, (int)b) : NULL;
        int upper = b >= 'A' && b <= 'Z';

        if (upper && sc && compose[i].accent != ACC_CEDIL)
            base = lookup(f, SMALL_CAPS + (unsigned long)(sc - small_caps_order));
        if (base < 0)
            base = lookup(f, b);
        acc = lookup(f, compose[i].accent);
        if (base < 0 || acc < 0)
            continue;
        g = add_glyph(f, compose[i].cp);
        shift = upper || compose[i].accent == ACC_CEDIL ? 0 : lower_shift;
        for (y = 0; y < f->h; y++) {
            unsigned char a = y - shift >= 0 ? glyph_ptr(f, acc)[y - shift] : 0;
            glyph_ptr(f, g)[y] = glyph_ptr(f, base)[y] | a;
        }
    }
}

static void build(Font *f, int w, int h, const char *const *src,
                  const char *name, int lower_shift)
{
    size_t i;

    f->w = w;
    f->h = h;
    f->n = 0;
    f->bits = (unsigned char *)calloc(MAX_GLYPHS, (size_t)h);
    for (i = 0; i < FONT_MAP_SIZE; i++)
        f->map[i] = -1;
    parse(f, src, name);
    for (i = 0; i < sizeof boxes / sizeof boxes[0]; i++)
        make_box(f, boxes[i].cp, boxes[i].up, boxes[i].down, boxes[i].left,
                 boxes[i].right);
    make_blocks(f);
    make_accented(f, lower_shift);
    if (f->map[0xAD] < 0 && f->map['-'] >= 0) {     /* soft hyphen */
        int g = add_glyph(f, 0xAD);
        memcpy(glyph_ptr(f, g), glyph_ptr(f, f->map['-']), (size_t)h);
    }
    if (f->map[0xA0] < 0)                           /* no-break space */
        add_glyph(f, 0xA0);
    if (f->placeholder < 0)
        f->placeholder = add_glyph(f, PLACEHOLDER_SLOT);
}

void font_init(void)
{
    build(&font_8x8, 8, 8, font8x8_src, "font8x8", 0);
    build(&font_8x16, 8, 16, font8x16_src, "font8x16", 3);
}

int font_has(const Font *f, unsigned long cp)
{
    /* private-use and internal slots are not real characters */
    if (cp >= PLACEHOLDER_SLOT && cp <= PRIV_SLOT(0xE0FF))
        return 0;
    return cp < FONT_MAP_SIZE && f->map[cp] >= 0;
}

const unsigned char *font_glyph(const Font *f, unsigned long cp)
{
    int i = font_has(f, cp) ? f->map[cp] : f->placeholder;
    return f->bits + (size_t)i * (size_t)f->h;
}
