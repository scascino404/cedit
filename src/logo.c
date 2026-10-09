/*
 * logo.c - cedit's logo, pixel art drawn by hand like the fonts: the name
 * in the Temple font at double size and a block cursor, each in a bright
 * and a dark shade of a VGA color, with a dark gray shadow. Its colors read
 * on both the light and the dark theme.
 *
 * Art: '.' clear, '0'-'9' and 'A'-'F' a palette color (see screen.h).
 */
#include "logo.h"
#include "screen.h"

static const char *const art[] = {
    "....................................AAAA....BBBB......9999........DDDDDDDDDDDD.",
    "....................................AAAA8...BBBB8.....99998.......DDDDDDDDDDDD8",
    "....................................AAAA8....8888.....99998.......DDDDDDDDDDDD8",
    "....................................AAAA8.............99998.......DDDDDDDDDDDD8",
    "..CCCCCCCC......EEEEEEEE......AAAAAAAAAA8.BBBBBB....9999999999....DDDDDDDDDDDD8",
    "..CCCCCCCC8.....EEEEEEEE8.....AAAAAAAAAA8.BBBBBB8...99999999998...DDDDDDDDDDDD8",
    "CCCC8888CCCC..EEEE8888EEEE..AAAA8888AAAA8..8BBBB8....8999988888...DDDDDDDDDDDD8",
    "CCCC8...CCCC8.EEEE8...EEEE8.AAAA8...AAAA8...BBBB8.....99998.......DDDDDDDDDDDD8",
    "44448....8888.6666666666668.22228...22228...33338.....11118.......5555555555558",
    "44448.........6666666666668.22228...22228...33338.....11118.......5555555555558",
    "44448...4444..6666888888888.22228...22228...33338.....11118.1111..5555555555558",
    "44448...44448.66668.........22228...22228...33338.....11118.11118.5555555555558",
    ".844444444888..866666666.....822222222228.33333333.....8111111888.5555555555558",
    "..444444448.....666666668.....22222222228.333333338.....1111118...5555555555558",
    "...88888888......88888888......8888888888..88888888......888888....888888888888",
    NULL
};

void logo_pixels(unsigned char *px)
{
    int x, y;
    for (y = 0; art[y]; y++)
        for (x = 0; art[y][x]; x++) {
            char c = art[y][x];
            *px++ = (unsigned char)(c == '.' ? PIC_CLEAR
                                    : c >= 'A' ? c - 'A' + 10 : c - '0');
        }
}
