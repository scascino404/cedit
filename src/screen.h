/*
 * screen.h - a text-mode style character grid rendered with SDL2.
 *
 * Each frame the UI composes a grid of cells; screen_present() rasterizes
 * only the cells that changed since the previous frame into a framebuffer at
 * the font's native resolution, uploads the dirty rows, and lets the GPU
 * scale it up by an integer factor.
 */
#ifndef CEDIT_SCREEN_H
#define CEDIT_SCREEN_H

#include <SDL.h>
#include "cursor.h"
#include "font.h"

/* The 16-color EGA/VGA palette. */
enum {
    BLACK, BLUE, GREEN, CYAN, RED, MAGENTA, BROWN, LIGHTGRAY,
    DARKGRAY, LIGHTBLUE, LIGHTGREEN, LIGHTCYAN, LIGHTRED, LIGHTMAGENTA,
    YELLOW, WHITE
};

/* Text cursor shapes (see cursor.c). */
enum { TCUR_NONE, TCUR_INSERT, TCUR_OVERWRITE };

/* Text sizes. */
enum { SIZE_SMALL, SIZE_MEDIUM, SIZE_LARGE, SIZE_COUNT };

/* The smallest grid the UI is laid out for. A smaller window gets the
 * grid cut off at its right and bottom edges. */
#define MIN_COLS 20
#define MIN_ROWS 8

typedef struct Cell {
    unsigned long ch;
    unsigned char fg, bg, cur;
} Cell;

typedef struct Screen {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    const Font *font;
    int size;               /* SIZE_*, as chosen */
    int shown;              /* the size drawn: smaller while the chosen
                               one does not fit MIN_COLS x MIN_ROWS */
    int hidpi;              /* output pixels per window unit */
    int scale;              /* output pixels per font pixel */
    int ptr_scale;          /* output pixels per pointer pixel */
    int cols, rows;
    int cw, ch;             /* cell size in font pixels */
    int fb_w, fb_h;
    Uint32 *fb;
    Cell *cells, *prev;
    int full;               /* force a full redraw */
    int presented;          /* a frame has been shown */
    int cur_bg, cur_ink, cur_box;   /* text cursor colors */

    /* Self-drawn mouse pointer. The compositor scales system pointers by
     * fractional display scales (blurring them); drawing the pointer into
     * our own frame keeps it pixel-exact like the text. */
    int ptr_visible;                /* the mouse is over the window */
    int ptr_x, ptr_y;               /* position in output pixels */
    SDL_Texture *ptr_tex;
    int ptr_w, ptr_h, ptr_hx, ptr_hy;
    int win_w, win_h, out_w, out_h;

    /* The picture last drawn (see screen_picture), at pixel pic_x, pic_y
     * of the grid. */
    unsigned char *pic;
    int pic_x, pic_y, pic_w, pic_h;
} Screen;

/* Opens a w x h window, or a default size for the text size if w or h
 * is 0. */
int screen_init(Screen *s, int size, int w, int h);
void screen_quit(Screen *s);
/* Recomputes the grid after a resize or text size change. */
void screen_layout(Screen *s);
/* Chooses the text size, and the smallest window that fits a grid of it. */
void screen_set_size(Screen *s, int size);

/* Drawing. Cells outside the grid are ignored. */
void screen_put(Screen *s, int x, int y, unsigned long ch, int fg, int bg);
/* Writes UTF-8 text, at most maxw cells; returns the cells written. */
int screen_puts(Screen *s, int x, int y, const char *str, int fg, int bg, int maxw);
void screen_fill(Screen *s, int x, int y, int w, int h, unsigned long ch,
                 int fg, int bg);
/* A single or double line box. */
void screen_frame(Screen *s, int x, int y, int w, int h, int dbl, int fg, int bg);
void screen_cursor(Screen *s, int x, int y, int shape);

/*
 * A scrollbar down a column h cells tall, for a view of h rows from row top
 * of total: arrows at both ends, and between them a track with a thumb.
 */
typedef struct Scrollbar {
    int h;
    int track;              /* cells between the arrows */
    int pos, len;           /* the thumb's place and length in the track */
} Scrollbar;

Scrollbar scrollbar_make(long total, long top, int h);
void screen_scrollbar(Screen *s, int x, int y, const Scrollbar *sb, int fg,
                      int bg);
/* Rows a click on cell r of the scrollbar scrolls by: a row on an arrow, a
 * screenful (h - 1 rows) on the track, and 0 on the thumb. */
int scrollbar_step(const Scrollbar *sb, int r);

/* Labels mark their hotkey letter with '&', as in "&Open". screen_label
 * draws one with the hotkey in color hot and returns its width. */
int screen_label(Screen *s, int x, int y, const char *label, int fg, int hot,
                 int bg);
int label_width(const char *label);
/* The lowercase hotkey letter of a label, or 0. */
int label_hotkey(const char *label);

/*
 * Draws a picture of w x h palette colors (PIC_CLEAR where the cell
 * background shows) with its top-left corner at pixel x, y of the grid, one
 * font pixel per picture pixel, over the cells it covers. Like the cells, it
 * has to be drawn again for each frame, after them; there is room for one
 * picture in a frame.
 */
#define PIC_CLEAR 255
void screen_picture(Screen *s, int x, int y, int w, int h,
                    const unsigned char *px);

/* Moves the self-drawn pointer (window coordinates) or hides it. */
void screen_pointer(Screen *s, int wx, int wy, int visible);

void screen_present(Screen *s);
/* Converts window coordinates (mouse events) to a cell. */
void screen_cell_at(const Screen *s, int wx, int wy, int *cx, int *cy);

#endif
