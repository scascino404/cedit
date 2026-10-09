/*
 * fontsheet.c - renders a font's glyphs and a text sample to a PPM image, for
 * reviewing the hand-drawn fonts.  usage: fontsheet 8|12|20 scale out.ppm
 */
#include "../src/font.h"
#include "../src/utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const sample[] = {
    "The quick brown fox jumps over the lazy dog. 0123456789",
    "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG! @#$%&*()",
    "int main(void) { return x[i] < y ? a->b : c / 2; } // ~^_`|\\",
    "\xc3\x80\xc3\x81\xc3\x82\xc3\x83\xc3\x84\xc3\x85\xc3\x86\xc3\x87"
    "\xc3\x88\xc3\x89\xc3\x8a\xc3\x8b \xc3\xa0\xc3\xa1\xc3\xa2\xc3\xa3"
    "\xc3\xa4\xc3\xa5\xc3\xa6\xc3\xa7\xc3\xa8\xc3\xa9\xc3\xaa\xc3\xab"
    " \xc3\xb1\xc3\x91 \xc3\xb6\xc3\x96 \xc3\xbc\xc3\x9c \xc3\x9f \xc3\xbf"
    "\xc3\xbd\xc3\x9d",
    "\xe2\x95\x94\xe2\x95\x90\xe2\x95\x90\xe2\x95\xa6\xe2\x95\x90\xe2\x95\x97"
    " \xe2\x94\x8c\xe2\x94\x80\xe2\x94\xac\xe2\x94\x90 \xe2\x96\x91\xe2\x96\x92"
    "\xe2\x96\x93\xe2\x96\x88 \xe2\x80\x9cquotes\xe2\x80\x9d \xe2\x80\x98it\xe2\x80\x99s\xe2\x80\x99"
    " \xe2\x82\xac" "5 \xe2\x80\x94 \xe2\x80\xa6 \xe2\x9c\x93 \xe2\x86\x90\xe2\x86\x91\xe2\x86\x92\xe2\x86\x93",
    "\xe2\x95\xa0\xe2\x95\x90\xe2\x95\x90\xe2\x95\xac\xe2\x95\x90\xe2\x95\xa3"
    " \xe2\x94\x9c\xe2\x94\x80\xe2\x94\xbc\xe2\x94\xa4 \xe2\x96\xb2\xe2\x96\xbc"
    "\xe2\x96\xba\xe2\x97\x84 \xc2\xa9\xc2\xae\xc2\xb0\xc2\xb1\xc2\xb2\xc2\xb3"
    "\xc2\xb5\xc2\xb6\xc2\xa7 \xc2\xab\xc2\xbb \xc2\xbc\xc2\xbd\xc2\xbe \xc2\xbf\xc2\xa1"
    " \xc2\xa3\xc2\xa5\xc2\xa2\xc2\xa4 \xc3\x97\xc3\xb7 \xc3\x98\xc3\xb8 \xc3\x90\xc3\xb0"
    " \xc3\x9e\xc3\xbe \xe2\x80\xa2",
    "\xe2\x95\x9a\xe2\x95\x90\xe2\x95\x90\xe2\x95\xa9\xe2\x95\x90\xe2\x95\x9d"
    " \xe2\x94\x94\xe2\x94\x80\xe2\x94\xb4\xe2\x94\x98 \xe2\x95\x9f\xe2\x94\x80"
    "\xe2\x95\xa2 \xe2\x95\xa4\xe2\x95\xa7 \xe2\x96\x80\xe2\x96\x84\xe2\x96\x8c\xe2\x96\x90"
    " \xc3\x8c\xc3\x8d\xc3\x8e\xc3\x8f\xc3\xac\xc3\xad\xc3\xae\xc3\xaf \xc3\x92\xc3\x93"
    "\xc3\x94\xc3\x95\xc3\xb2\xc3\xb3\xc3\xb4\xc3\xb5 \xc3\x99\xc3\x9a\xc3\x9b\xc3\xb9"
    "\xc3\xba\xc3\xbb \xc2\xaa\xc2\xba\xc2\xb9 \xc2\xa8\xc2\xb4\xc2\xb8\xc2\xaf \xe4\xb8\xad",
    NULL
};

static Font font_20x20;

int main(int argc, char **argv)
{
    const Font *f;
    int scale, cols = 72, rows, W, H, r;
    unsigned char *img;
    FILE *fp;

    if (argc != 4) {
        fprintf(stderr, "usage: fontsheet 8|12|20 scale out.ppm\n");
        return 1;
    }
    font_init();
    font_build(&font_20x20, 20, 20, font20x20_src, "font20x20");
    f = atoi(argv[1]) == 20 ? &font_20x20
      : atoi(argv[1]) == 12 ? &font_12x12 : &font_8x8;
    scale = atoi(argv[2]);
    for (rows = 0; sample[rows]; rows++)
        ;
    rows = rows * 2;
    W = (cols * f->w + 2) * scale;
    H = (rows * f->h + 2) * scale;
    img = (unsigned char *)malloc((size_t)W * H * 3);
    memset(img, 255, (size_t)W * H * 3);

    for (r = 0; sample[r]; r++) {
        const char *s = sample[r];
        size_t n = strlen(s), pos = 0;
        int col = 0;
        while (pos < n && col < cols) {
            unsigned long cp;
            const unsigned long *g;
            int x, y;
            pos += utf8_decode(s + pos, n - pos, &cp);
            g = font_glyph(f, cp);
            for (y = 0; y < f->h; y++)
                for (x = 0; x < f->w; x++) {
                    int on = (g[y] & FONT_BIT(x)) != 0, sx, sy;
                    for (sy = 0; sy < scale; sy++)
                        for (sx = 0; sx < scale; sx++) {
                            int px = (1 + col * f->w + x) * scale + sx;
                            int py = (1 + r * 2 * f->h + y) * scale + sy;
                            unsigned char *p = img + ((size_t)py * W + px) * 3;
                            if (on) {
                                p[0] = 0; p[1] = 0; p[2] = 170;
                            } else if ((col + r) % 2) {
                                p[0] = 235; p[1] = 235; p[2] = 255;
                            }
                        }
                }
            col++;
        }
    }
    fp = fopen(argv[3], "wb");
    if (!fp) {
        perror(argv[3]);
        return 1;
    }
    fprintf(fp, "P6\n%d %d\n255\n", W, H);
    fwrite(img, 1, (size_t)W * H * 3, fp);
    fclose(fp);
    free(img);
    return 0;
}
