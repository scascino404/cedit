/*
 * screen.c - character grid, dirty-cell rasterizer and SDL presentation.
 */
#include "screen.h"
#include "cursor.h"
#include "utf8.h"

#include <stdlib.h>
#include <string.h>

static const Uint32 palette[16] = {
    0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
    0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
    0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
    0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF
};

/* Font and magnification for each text size, before HiDPI scaling. */
static const struct {
    const Font *font;
    int mult;
} sizes[SIZE_COUNT] = {
    {&font_8x8, 1},
    {&font_8x16, 1},
    {&font_8x16, 2}
};

int screen_init(Screen *s, int size)
{
    int w, h;
    SDL_Rect usable;

    memset(s, 0, sizeof *s);
    s->size = size;
    s->cur_bg = YELLOW;
    s->cur_ink = BLUE;
    s->cur_box = RED;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    /* about 80x25 cells of the chosen size, within 90% of the display */
    w = 80 * sizes[size].font->w * sizes[size].mult;
    h = 25 * sizes[size].font->h * sizes[size].mult;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
        if (w > usable.w * 9 / 10)
            w = usable.w * 9 / 10;
        if (h > usable.h * 9 / 10)
            h = usable.h * 9 / 10;
    }
    s->win = SDL_CreateWindow("cedit", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, w, h,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!s->win)
        return -1;
    SDL_SetWindowMinimumSize(s->win, 320, 200);
    s->ren = SDL_CreateRenderer(s->win, -1, SDL_RENDERER_ACCELERATED);
    if (!s->ren)
        s->ren = SDL_CreateRenderer(s->win, -1, SDL_RENDERER_SOFTWARE);
    if (!s->ren)
        return -1;
    screen_layout(s);
    return 0;
}

/* (Re)creates the pointer textures; textures are lost on a device reset. */
static void make_pointer_textures(Screen *s)
{
    int i;
    for (i = 0; i < PTR_COUNT; i++) {
        Uint32 *px;
        if (s->ptr_tex[i])
            SDL_DestroyTexture(s->ptr_tex[i]);
        s->ptr_tex[i] = NULL;
        px = pointer_pixels(i, &s->ptr_w[i], &s->ptr_h[i], &s->ptr_hx[i],
                            &s->ptr_hy[i]);
        if (!px)
            continue;
        s->ptr_tex[i] = SDL_CreateTexture(s->ren, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STATIC,
                                          s->ptr_w[i], s->ptr_h[i]);
        if (s->ptr_tex[i]) {
            SDL_UpdateTexture(s->ptr_tex[i], NULL, px,
                              s->ptr_w[i] * (int)sizeof(Uint32));
            SDL_SetTextureBlendMode(s->ptr_tex[i], SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(s->ptr_tex[i], SDL_ScaleModeNearest);
        }
        SDL_free(px);
    }
}

void screen_quit(Screen *s)
{
    int i;
    for (i = 0; i < PTR_COUNT; i++)
        if (s->ptr_tex[i])
            SDL_DestroyTexture(s->ptr_tex[i]);
    if (s->tex)
        SDL_DestroyTexture(s->tex);
    if (s->ren)
        SDL_DestroyRenderer(s->ren);
    if (s->win)
        SDL_DestroyWindow(s->win);
    free(s->fb);
    free(s->cells);
    free(s->prev);
}

void screen_layout(Screen *s)
{
    int cols, rows;

    SDL_GetWindowSize(s->win, &s->win_w, &s->win_h);
    SDL_GetRendererOutputSize(s->ren, &s->out_w, &s->out_h);
    s->hidpi = s->win_w > 0 ? (s->out_w + s->win_w / 2) / s->win_w : 1;
    if (s->hidpi < 1)
        s->hidpi = 1;
    s->font = sizes[s->size].font;
    s->scale = sizes[s->size].mult * s->hidpi;
    s->cw = s->font->w;
    s->ch = s->font->h;
    cols = s->out_w / (s->cw * s->scale);
    rows = s->out_h / (s->ch * s->scale);
    if (cols < 20)
        cols = 20;
    if (rows < 8)
        rows = 8;

    if (cols != s->cols || rows != s->rows || !s->tex ||
        s->fb_w != cols * s->cw || s->fb_h != rows * s->ch) {
        s->cols = cols;
        s->rows = rows;
        s->fb_w = cols * s->cw;
        s->fb_h = rows * s->ch;
        free(s->fb);
        free(s->cells);
        free(s->prev);
        s->fb = (Uint32 *)calloc((size_t)s->fb_w * s->fb_h, sizeof(Uint32));
        s->cells = (Cell *)calloc((size_t)cols * rows, sizeof(Cell));
        s->prev = (Cell *)calloc((size_t)cols * rows, sizeof(Cell));
        if (s->tex)
            SDL_DestroyTexture(s->tex);
        s->tex = SDL_CreateTexture(s->ren, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, s->fb_w, s->fb_h);
        if (s->tex)
            SDL_SetTextureScaleMode(s->tex, SDL_ScaleModeNearest);
    }
    make_pointer_textures(s);
    s->full = 1;
}

void screen_set_size(Screen *s, int size)
{
    if (size < 0)
        size = 0;
    if (size >= SIZE_COUNT)
        size = SIZE_COUNT - 1;
    s->size = size;
    screen_layout(s);
}

Cell *screen_cell(Screen *s, int x, int y)
{
    if (x < 0 || y < 0 || x >= s->cols || y >= s->rows)
        return NULL;
    return &s->cells[y * s->cols + x];
}

void screen_put(Screen *s, int x, int y, unsigned long ch, int fg, int bg)
{
    Cell *c = screen_cell(s, x, y);
    if (!c)
        return;
    c->ch = ch;
    c->fg = (unsigned char)fg;
    c->bg = (unsigned char)bg;
    c->cur = TCUR_NONE;
}

void screen_clear(Screen *s, int fg, int bg)
{
    screen_fill(s, 0, 0, s->cols, s->rows, ' ', fg, bg);
}

void screen_fill(Screen *s, int x, int y, int w, int h, unsigned long ch,
                 int fg, int bg)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            screen_put(s, i, j, ch, fg, bg);
}

int screen_puts(Screen *s, int x, int y, const char *str, int fg, int bg,
                int maxw)
{
    size_t n = strlen(str), i = 0;
    int w = 0;
    while (i < n && w < maxw) {
        unsigned long cp;
        i += utf8_decode(str + i, n - i, &cp);
        screen_put(s, x + w, y, cp, fg, bg);
        w++;
    }
    return w;
}

void screen_shadow(Screen *s, int x, int y, int w, int h)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++) {
            Cell *c = screen_cell(s, i, j);
            if (c) {
                c->fg = DARKGRAY;
                c->bg = BLACK;
            }
        }
}

void screen_cursor(Screen *s, int x, int y, int shape)
{
    Cell *c = screen_cell(s, x, y);
    if (c)
        c->cur = (unsigned char)shape;
}

static void raster(Screen *s, int cx, int cy, const Cell *c)
{
    const unsigned char *g = font_glyph(s->font, c->ch);
    Uint32 fg = palette[c->fg & 15], bg = palette[c->bg & 15];
    Uint32 *row = s->fb + (size_t)cy * s->ch * s->fb_w + (size_t)cx * s->cw;
    int x, y;

    for (y = 0; y < s->ch; y++, row += s->fb_w) {
        unsigned char bits = g[y];
        unsigned char mask = c->cur ? text_cursor_row(c->cur, s->ch, y) : 0;
        for (x = 0; x < s->cw; x++) {
            unsigned char b = (unsigned char)(0x80 >> x);
            if (mask & b) {
                /* block: ink on the cursor color; hollow box: a frame */
                row[x] = c->cur == TCUR_INSERT
                             ? palette[(bits & b ? s->cur_ink : s->cur_bg) & 15]
                             : palette[s->cur_box & 15];
            } else {
                row[x] = bits & b ? fg : bg;
            }
        }
    }
}

void screen_present(Screen *s)
{
    int x, y, y0 = s->rows, y1 = -1;
    SDL_Rect src, dst;

    for (y = 0; y < s->rows; y++) {
        for (x = 0; x < s->cols; x++) {
            Cell *c = &s->cells[y * s->cols + x];
            Cell *p = &s->prev[y * s->cols + x];
            if (s->full || c->ch != p->ch || c->fg != p->fg || c->bg != p->bg ||
                c->cur != p->cur) {
                raster(s, x, y, c);
                *p = *c;
                if (y < y0)
                    y0 = y;
                y1 = y;
            }
        }
    }
    s->full = 0;
    if (y1 >= y0) {
        src.x = 0;
        src.y = y0 * s->ch;
        src.w = s->fb_w;
        src.h = (y1 - y0 + 1) * s->ch;
        SDL_UpdateTexture(s->tex, &src, s->fb + (size_t)src.y * s->fb_w,
                          s->fb_w * (int)sizeof(Uint32));
    }

    /* The grid sits at the top-left; the leftover strip on the right and
     * bottom (less than one cell) continues each row's edge color. */
    SDL_SetRenderDrawColor(s->ren, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_RenderClear(s->ren);
    dst.x = 0;
    dst.y = 0;
    dst.w = s->fb_w * s->scale;
    dst.h = s->fb_h * s->scale;
    SDL_RenderCopy(s->ren, s->tex, NULL, &dst);
    for (y = 0; y < s->rows; y++) {
        Uint32 col = palette[s->cells[y * s->cols + s->cols - 1].bg & 15];
        SDL_Rect r;
        r.x = dst.w;
        r.y = y * s->ch * s->scale;
        r.w = s->out_w - dst.w;
        r.h = s->ch * s->scale;
        if (y == s->rows - 1)
            r.h = s->out_h - r.y;
        SDL_SetRenderDrawColor(s->ren, (Uint8)(col >> 16), (Uint8)(col >> 8),
                               (Uint8)col, 0xFF);
        if (r.w > 0)
            SDL_RenderFillRect(s->ren, &r);
    }
    {
        Uint32 col = palette[s->cells[(s->rows - 1) * s->cols].bg & 15];
        SDL_Rect r;
        r.x = 0;
        r.y = dst.h;
        r.w = dst.w;
        r.h = s->out_h - dst.h;
        SDL_SetRenderDrawColor(s->ren, (Uint8)(col >> 16), (Uint8)(col >> 8),
                               (Uint8)col, 0xFF);
        if (r.h > 0)
            SDL_RenderFillRect(s->ren, &r);
    }
    if (s->ptr_visible && s->ptr_tex[s->ptr_kind]) {
        /* same pixel size as the font, hot spot pixel under the mouse */
        int k = s->ptr_kind;
        SDL_Rect r;
        r.x = s->ptr_x - s->ptr_hx[k] * s->scale;
        r.y = s->ptr_y - s->ptr_hy[k] * s->scale;
        r.w = s->ptr_w[k] * s->scale;
        r.h = s->ptr_h[k] * s->scale;
        SDL_RenderCopy(s->ren, s->ptr_tex[k], NULL, &r);
    }
    SDL_RenderPresent(s->ren);
}

void screen_pointer(Screen *s, int wx, int wy, int visible)
{
    s->ptr_x = wx * s->out_w / (s->win_w ? s->win_w : 1);
    s->ptr_y = wy * s->out_h / (s->win_h ? s->win_h : 1);
    s->ptr_visible = visible && wx >= 0 && wy >= 0 && wx < s->win_w &&
                     wy < s->win_h;
}

void screen_cell_at(const Screen *s, int wx, int wy, int *cx, int *cy)
{
    int px = wx * s->out_w / (s->win_w ? s->win_w : 1);
    int py = wy * s->out_h / (s->win_h ? s->win_h : 1);
    *cx = px / (s->cw * s->scale);
    *cy = py / (s->ch * s->scale);
    if (*cx >= s->cols)
        *cx = s->cols - 1;
    if (*cy >= s->rows)
        *cy = s->rows - 1;
    if (*cx < 0)
        *cx = 0;
    if (*cy < 0)
        *cy = 0;
}
