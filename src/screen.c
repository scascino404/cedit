/*
 * screen.c - character grid, dirty-cell rasterizer and SDL presentation.
 */
#include "screen.h"
#include "utf8.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

static const Uint32 palette[16] = {
    0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA,
    0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
    0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
    0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF
};

/* Font, magnification and pointer magnification for each text size,
 * before HiDPI scaling. The pointer is drawn at about the font's pixel size. */
static const struct {
    const Font *font;
    int mult, ptr_mult;
} sizes[SIZE_COUNT] = {
    {&font_12x12, 1, 2},
    {&font_8x8, 2, 2},
    {&font_8x8, 3, 3}
};

int screen_init(Screen *s, int size)
{
    int w, h;
    SDL_Rect usable;

    memset(s, 0, sizeof *s);
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
    s->ren = SDL_CreateRenderer(s->win, -1, SDL_RENDERER_ACCELERATED);
    if (!s->ren)
        s->ren = SDL_CreateRenderer(s->win, -1, SDL_RENDERER_SOFTWARE);
    if (!s->ren)
        return -1;
    screen_set_size(s, size);
    return 0;
}

/* (Re)creates the pointer texture; textures are lost on a device reset. */
static void make_pointer_texture(Screen *s)
{
    Uint32 *px;
    if (s->ptr_tex)
        SDL_DestroyTexture(s->ptr_tex);
    s->ptr_tex = NULL;
    px = pointer_pixels(&s->ptr_w, &s->ptr_h, &s->ptr_hx, &s->ptr_hy);
    if (!px)
        return;
    s->ptr_tex = SDL_CreateTexture(s->ren, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STATIC, s->ptr_w, s->ptr_h);
    if (s->ptr_tex) {
        SDL_UpdateTexture(s->ptr_tex, NULL, px, s->ptr_w * (int)sizeof(Uint32));
        SDL_SetTextureBlendMode(s->ptr_tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(s->ptr_tex, SDL_ScaleModeNearest);
    }
    SDL_free(px);
}

void screen_quit(Screen *s)
{
    if (s->ptr_tex)
        SDL_DestroyTexture(s->ptr_tex);
    if (s->tex)
        SDL_DestroyTexture(s->tex);
    if (s->ren)
        SDL_DestroyRenderer(s->ren);
    if (s->win)
        SDL_DestroyWindow(s->win);
    free(s->fb);
    free(s->cells);
    free(s->prev);
    free(s->pic);
}

void screen_layout(Screen *s)
{
    int cols, rows;

    SDL_GetWindowSize(s->win, &s->win_w, &s->win_h);
    SDL_GetRendererOutputSize(s->ren, &s->out_w, &s->out_h);
    s->hidpi = s->win_w > 0 ? (s->out_w + s->win_w / 2) / s->win_w : 1;
    if (s->hidpi < 1)
        s->hidpi = 1;
    /* the chosen size, or the largest smaller one whose grid fits, for a
     * window manager that makes the window smaller than its minimum */
    for (s->shown = s->size; ; s->shown--) {
        s->font = sizes[s->shown].font;
        s->scale = sizes[s->shown].mult * s->hidpi;
        s->ptr_scale = sizes[s->shown].ptr_mult * s->hidpi;
        s->cw = s->font->w;
        s->ch = s->font->h;
        cols = s->out_w / (s->cw * s->scale);
        rows = s->out_h / (s->ch * s->scale);
        if (s->shown == 0 || (cols >= MIN_COLS && rows >= MIN_ROWS))
            break;
    }
    if (cols < MIN_COLS)
        cols = MIN_COLS;
    if (rows < MIN_ROWS)
        rows = MIN_ROWS;

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
    make_pointer_texture(s);
    s->full = 1;
}

void screen_set_size(Screen *s, int size)
{
    if (size < 0)
        size = 0;
    if (size >= SIZE_COUNT)
        size = SIZE_COUNT - 1;
    s->size = size;
    SDL_SetWindowMinimumSize(s->win, MIN_COLS * sizes[size].font->w * sizes[size].mult,
                             MIN_ROWS * sizes[size].font->h * sizes[size].mult);
    screen_layout(s);
}

static Cell *screen_cell(Screen *s, int x, int y)
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

void screen_frame(Screen *s, int x, int y, int w, int h, int dbl, int fg, int bg)
{
    int i;
    unsigned long hz = dbl ? 0x2550 : 0x2500, vt = dbl ? 0x2551 : 0x2502;
    screen_put(s, x, y, dbl ? 0x2554 : 0x250C, fg, bg);
    screen_put(s, x + w - 1, y, dbl ? 0x2557 : 0x2510, fg, bg);
    screen_put(s, x, y + h - 1, dbl ? 0x255A : 0x2514, fg, bg);
    screen_put(s, x + w - 1, y + h - 1, dbl ? 0x255D : 0x2518, fg, bg);
    for (i = 1; i < w - 1; i++) {
        screen_put(s, x + i, y, hz, fg, bg);
        screen_put(s, x + i, y + h - 1, hz, fg, bg);
    }
    for (i = 1; i < h - 1; i++) {
        screen_put(s, x, y + i, vt, fg, bg);
        screen_put(s, x + w - 1, y + i, vt, fg, bg);
    }
}

void screen_cursor(Screen *s, int x, int y, int shape)
{
    Cell *c = screen_cell(s, x, y);
    if (c)
        c->cur = (unsigned char)shape;
}

Scrollbar scrollbar_make(long total, long top, int h)
{
    Scrollbar sb;
    long range = total - h;

    sb.h = h;
    sb.track = h - 2 < 1 ? 1 : h - 2;
    sb.len = total > h ? (int)((long)sb.track * h / total) : sb.track;
    if (sb.len < 1)
        sb.len = 1;
    sb.pos = range > 0 ? (int)((sb.track - sb.len) * top / range) : 0;
    if (sb.pos > sb.track - sb.len)
        sb.pos = sb.track - sb.len;
    return sb;
}

void screen_scrollbar(Screen *s, int x, int y, const Scrollbar *sb, int fg,
                      int bg)
{
    int i;
    screen_put(s, x, y, 0x25B2, fg, bg);
    screen_put(s, x, y + sb->h - 1, 0x25BC, fg, bg);
    for (i = 0; i < sb->track && sb->h > 2; i++)
        screen_put(s, x, y + 1 + i,
                   i >= sb->pos && i < sb->pos + sb->len ? 0x2588 : 0x2591, fg, bg);
}

int scrollbar_step(const Scrollbar *sb, int r)
{
    if (r == 0)
        return -1;
    if (r == sb->h - 1)
        return 1;
    if (r - 1 < sb->pos)
        return -(sb->h - 1);
    if (r - 1 >= sb->pos + sb->len)
        return sb->h - 1;
    return 0;
}

int screen_label(Screen *s, int x, int y, const char *label, int fg, int hot,
                 int bg)
{
    size_t n = strlen(label), i = 0;
    int w = 0, next_hot = 0;
    while (i < n) {
        unsigned long cp;
        i += utf8_decode(label + i, n - i, &cp);
        if (cp == '&') {
            next_hot = 1;
            continue;
        }
        screen_put(s, x + w, y, cp, next_hot ? hot : fg, bg);
        next_hot = 0;
        w++;
    }
    return w;
}

int label_width(const char *label)
{
    int w = utf8_width(label);
    for (; *label; label++)
        w -= *label == '&';
    return w;
}

int label_hotkey(const char *label)
{
    const char *p = strchr(label, '&');
    return p ? ascii_lower((unsigned char)p[1]) : 0;
}

/* Cells under the picture hold this, beyond the last codepoint. */
#define PIC_CELL 0x110000ul

void screen_picture(Screen *s, int x, int y, int w, int h,
                    const unsigned char *px)
{
    size_t n = (size_t)w * h;
    int cx, cy;

    if (w <= 0 || h <= 0)
        return;
    /* a picture moved or changed has to be drawn over all its cells, even
     * the ones that showed it before */
    if (!s->pic || x != s->pic_x || y != s->pic_y || w != s->pic_w ||
        h != s->pic_h || memcmp(px, s->pic, n) != 0) {
        free(s->pic);
        s->pic = (unsigned char *)xmalloc(n);
        memcpy(s->pic, px, n);
        s->pic_x = x;
        s->pic_y = y;
        s->pic_w = w;
        s->pic_h = h;
        s->full = 1;
    }
    for (cy = y / s->ch; cy <= (y + h - 1) / s->ch; cy++)
        for (cx = x / s->cw; cx <= (x + w - 1) / s->cw; cx++)
            if (cx >= 0 && cy >= 0 && cx < s->cols && cy < s->rows)
                s->cells[cy * s->cols + cx].ch = PIC_CELL;
}

/* A cell under the picture: its part of the picture, on the cell's
 * background. */
static void raster_picture(Screen *s, int cx, int cy, const Cell *c)
{
    Uint32 bg = palette[c->bg & 15];
    Uint32 *row = s->fb + (size_t)cy * s->ch * s->fb_w + (size_t)cx * s->cw;
    int x, y;

    for (y = 0; y < s->ch; y++, row += s->fb_w) {
        int py = cy * s->ch + y - s->pic_y;
        for (x = 0; x < s->cw; x++) {
            int px = cx * s->cw + x - s->pic_x;
            int p = px >= 0 && py >= 0 && px < s->pic_w && py < s->pic_h
                        ? s->pic[py * s->pic_w + px] : PIC_CLEAR;
            row[x] = p == PIC_CLEAR ? bg : palette[p & 15];
        }
    }
}

static void raster(Screen *s, int cx, int cy, const Cell *c)
{
    const unsigned long *g = font_glyph(s->font, c->ch);
    Uint32 fg = palette[c->fg & 15], bg = palette[c->bg & 15];
    Uint32 *row = s->fb + (size_t)cy * s->ch * s->fb_w + (size_t)cx * s->cw;
    int x, y;

    for (y = 0; y < s->ch; y++, row += s->fb_w) {
        unsigned long bits = g[y];
        unsigned long mask = c->cur ? text_cursor_row(c->cur, s->ch, y) : 0;
        for (x = 0; x < s->cw; x++) {
            unsigned long b = FONT_BIT(x);
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

static void fill_rect(Screen *s, int x, int y, int w, int h, int color)
{
    Uint32 c = palette[color & 15];
    SDL_Rect r;
    if (w <= 0 || h <= 0)
        return;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    SDL_SetRenderDrawColor(s->ren, (Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c, 0xFF);
    SDL_RenderFillRect(s->ren, &r);
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
                if (c->ch == PIC_CELL)
                    raster_picture(s, x, y, c);
                else
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
        int top = y * s->ch * s->scale;
        int h = y == s->rows - 1 ? s->out_h - top : s->ch * s->scale;
        fill_rect(s, dst.w, top, s->out_w - dst.w, h,
                  s->cells[y * s->cols + s->cols - 1].bg);
    }
    fill_rect(s, 0, dst.h, dst.w, s->out_h - dst.h,
              s->cells[(s->rows - 1) * s->cols].bg);
    if (s->ptr_visible && s->ptr_tex) {
        /* about the font's pixel size, hot spot pixel under the mouse */
        SDL_Rect r;
        r.x = s->ptr_x - s->ptr_hx * s->ptr_scale;
        r.y = s->ptr_y - s->ptr_hy * s->ptr_scale;
        r.w = s->ptr_w * s->ptr_scale;
        r.h = s->ptr_h * s->ptr_scale;
        SDL_RenderCopy(s->ren, s->ptr_tex, NULL, &r);
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
