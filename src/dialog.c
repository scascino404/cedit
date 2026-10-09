/*
 * dialog.c - dialog boxes: building, drawing and input.
 */
#include "dialog.h"
#include "utf8.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* building                                                            */
/* ------------------------------------------------------------------ */

static int focusable(const Widget *w)
{
    return w->kind != W_LABEL && w->kind != W_PATH;
}

void dlg_list_clear(Dialog *d)
{
    int i;
    for (i = 0; i < d->nitems; i++)
        free(d->items[i]);
    free(d->items);
    d->items = NULL;
    d->nitems = 0;
    d->scroll = 0;
}

void dlg_list_add(Dialog *d, const char *item)
{
    int n = d->nitems;
    /* the capacity is n rounded up to a power of two */
    if ((n & (n - 1)) == 0)
        d->items = (char **)xrealloc(d->items, (size_t)(n ? 2 * n : 1) * sizeof(char *));
    d->items[d->nitems++] = xstrdup(item);
}

void dlg_list(Dialog *d, int x, int y, int w, int h, int framed, int sel)
{
    d->list_x = x;
    d->list_y = y;
    d->list_w = w;
    d->list_h = h;
    d->list_frame = framed;
    d->sel = sel;
}

void dlg_begin(Dialog *d, int kind, const char *title, int w, int h)
{
    dlg_list_clear(d);
    memset(d, 0, sizeof *d);
    d->kind = kind;
    str_copy(d->title, sizeof d->title, title);
    d->w = d->max_w = d->min_w = w;
    d->h = d->max_h = d->min_h = h;
    d->focus = -1;
}

void dlg_close(Dialog *d)
{
    dlg_list_clear(d);
    d->kind = DLG_NONE;
}

Widget *dlg_add(Dialog *d, int kind, int id, int x, int y, int w,
                const char *label)
{
    Widget *wd = &d->wd[d->n++];
    memset(wd, 0, sizeof *wd);
    wd->kind = kind;
    wd->id = id;
    wd->x = x;
    wd->y = y;
    wd->w = w;
    str_copy(wd->label, sizeof wd->label, label ? label : "");
    if (kind == W_BUTTON)
        wd->w = label_width(wd->label) + 4;
    if (d->focus < 0 && focusable(wd))
        d->focus = d->n - 1;
    return wd;
}

/* Centers the buttons in the dialog's width, closer together if they
 * need to be. */
static void center_buttons(Dialog *d)
{
    int i, n = 0, total = 0, x, gap = 2;
    for (i = 0; i < d->n; i++)
        if (d->wd[i].kind == W_BUTTON) {
            total += d->wd[i].w + (n ? gap : 0);
            n++;
        }
    if (total > d->w - 2) {         /* narrow screen: tighter spacing */
        total -= n - 1;
        gap = 1;
    }
    x = (d->w - total) / 2;
    if (x < 1)
        x = 1;
    for (i = 0; i < d->n; i++)
        if (d->wd[i].kind == W_BUTTON) {
            d->wd[i].x = x;
            x += d->wd[i].w + gap;
        }
}

void dlg_buttons(Dialog *d, int y, const int *ids, const char *const *labels,
                 int n)
{
    int i;
    for (i = 0; i < n; i++)
        dlg_add(d, W_BUTTON, ids[i], 0, y, 0, labels[i]);
    center_buttons(d);
}

Widget *dlg_find(Dialog *d, int id)
{
    int i;
    for (i = 0; i < d->n; i++)
        if (d->wd[i].id == id)
            return &d->wd[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* input fields                                                        */
/* ------------------------------------------------------------------ */

void field_set(Widget *f, const char *s)
{
    str_copy(f->text, sizeof f->text, s);
    f->len = f->cur = strlen(f->text);
}

static void field_insert(Widget *f, const char *s, size_t n)
{
    size_t i, m = 0;
    char clean[FIELD_MAX];
    for (i = 0; i < n && m < sizeof clean; i++)
        if (s[i] != '\n' && s[i] != '\r')
            clean[m++] = s[i];
    if (f->len + m >= sizeof f->text)
        return;
    memmove(f->text + f->cur + m, f->text + f->cur, f->len - f->cur + 1);
    memcpy(f->text + f->cur, clean, m);
    f->len += m;
    f->cur += m;
}

static void field_key(Widget *f, SDL_Keycode k, int ctrl)
{
    size_t p;
    if (ctrl && k == SDLK_v) {
        char *t = SDL_GetClipboardText();
        if (t)
            field_insert(f, t, strlen(t));
        SDL_free(t);
        return;
    }
    if (ctrl && k == SDLK_a) {
        f->cur = f->len;
        return;
    }
    switch (k) {
    case SDLK_BACKSPACE:
        if (ctrl) {
            memmove(f->text, f->text + f->cur, f->len - f->cur + 1);
            f->len -= f->cur;
            f->cur = 0;
        } else if (f->cur > 0) {
            p = utf8_prev(f->text, f->cur);
            memmove(f->text + p, f->text + f->cur, f->len - f->cur + 1);
            f->len -= f->cur - p;
            f->cur = p;
        }
        break;
    case SDLK_DELETE:
        if (f->cur < f->len) {
            p = utf8_next(f->text, f->len, f->cur);
            memmove(f->text + f->cur, f->text + p, f->len - p + 1);
            f->len -= p - f->cur;
        }
        break;
    case SDLK_LEFT:
        f->cur = utf8_prev(f->text, f->cur);
        break;
    case SDLK_RIGHT:
        f->cur = utf8_next(f->text, f->len, f->cur);
        break;
    case SDLK_HOME:
        f->cur = 0;
        break;
    case SDLK_END:
        f->cur = f->len;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* list box                                                            */
/* ------------------------------------------------------------------ */

static void list_scroll(Dialog *d, int by)
{
    d->scroll += by;
    if (d->scroll > d->nitems - d->list_h)
        d->scroll = d->nitems - d->list_h;
    if (d->scroll < 0)
        d->scroll = 0;
}

static void list_select(Dialog *d, int i)
{
    Widget *f = dlg_find(d, d->list_field);
    if (d->nitems == 0)
        return;
    if (i < 0)
        i = 0;
    if (i >= d->nitems)
        i = d->nitems - 1;
    d->sel = i;
    if (f && d->list_field)
        field_set(f, d->items[i]);
    d->dirty = 1;
}

/* Rows a key moves the list by, or 0. Home and End only scroll lists
 * without a selection; otherwise they belong to the focused field. */
static int list_step(const Dialog *d, SDL_Keycode sym)
{
    if (d->list_h <= 0)
        return 0;
    switch (sym) {
    case SDLK_UP:       return -1;
    case SDLK_DOWN:     return 1;
    case SDLK_PAGEUP:   return -d->list_h;
    case SDLK_PAGEDOWN: return d->list_h;
    case SDLK_HOME:     return d->sel < 0 ? -d->nitems : 0;
    case SDLK_END:      return d->sel < 0 ? d->nitems : 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */

/* Changes the dialog's size, moving and stretching what follows its edges. */
static void resize(Dialog *d, int w, int h)
{
    int i, dw = w - d->w, dh = h - d->h;
    for (i = 0; i < d->n; i++) {
        Widget *wd = &d->wd[i];
        if (wd->stretch)
            wd->w += dw;
        if (wd->kind == W_BUTTON)
            wd->y += dh;
    }
    d->list_w += dw;
    d->list_h += dh;
    d->w = w;
    d->h = h;
    center_buttons(d);
}

/* Sizes the dialog to fit the screen, between its smallest and full size,
 * and centers it (or puts it above the window's bottom border). */
static void place(Dialog *d, const Screen *s)
{
    int w = d->max_w < s->cols - 4 ? d->max_w : s->cols - 4;
    int h = d->max_h < s->rows - 3 ? d->max_h : s->rows - 3;
    if (w < d->min_w)
        w = d->min_w;
    if (h < d->min_h)
        h = d->min_h;
    if (w != d->w || h != d->h)
        resize(d, w, h);
    d->x = (s->cols - d->w) / 2;
    if (d->x < 0)
        d->x = 0;
    d->y = d->at_bottom ? s->rows - d->h - 1 : (s->rows - d->h) / 2;
    if (d->y < 1)
        d->y = 1;
}

static void draw_field(Screen *s, const Theme *t, const Widget *f, int x, int y,
                       int focused, int blink_on)
{
    size_t i = 0;
    int ci = 0, col = 0, k = 0;
    int bg = focused ? t->focus_bg : t->field_bg;
    int fg = focused ? t->focus_fg : t->field_fg;

    /* character index of the cursor, then scroll to keep it visible */
    while (i < f->cur) {
        i = utf8_next(f->text, f->len, i);
        ci++;
    }
    i = 0;
    while (ci - k >= f->w) {
        i = utf8_next(f->text, f->len, i);
        k++;
    }
    screen_fill(s, x, y, f->w, 1, ' ', fg, bg);
    while (i < f->len && col < f->w) {
        unsigned long cp;
        i += utf8_decode(f->text + i, f->len - i, &cp);
        screen_put(s, x + col++, y, cp, fg, bg);
    }
    if (focused && blink_on)
        screen_cursor(s, x + ci - k, y, TCUR_INSERT);
}

static void draw_widget(Dialog *d, Screen *s, const Theme *t, const Widget *w,
                        int focused, int blink_on)
{
    int x = d->x + w->x, y = d->y + w->y;
    int fg = focused ? t->sel_fg : t->text_fg, bg = focused ? t->sel_bg : t->text_bg;

    switch (w->kind) {
    case W_LABEL: {
        /* clip to the dialog interior */
        char clip[sizeof w->label];
        int room = d->x + d->w - 1 - x, k = 0;
        size_t j = 0, len = strlen(w->label);
        while (j < len && k < room) {
            j = utf8_next(w->label, len, j);
            k++;
        }
        memcpy(clip, w->label, j);
        clip[j] = 0;
        screen_label(s, x, y, clip, t->text_fg, t->hot, t->text_bg);
        break;
    }
    case W_PATH: {
        /* show the tail of long paths */
        int cw = utf8_width(w->label), over = utf8_width(w->text) - (w->w - cw);
        size_t j = 0;
        while (over-- > 0)
            j = utf8_next(w->text, w->len, j);
        screen_puts(s, x, y, w->label, t->lnum, t->text_bg, cw);
        screen_puts(s, x + cw, y, w->text + j, t->text_fg, t->text_bg, w->w - cw);
        break;
    }
    case W_FIELD:
        draw_field(s, t, w, x, y, focused, blink_on);
        break;
    case W_CHECK:
        fg = focused ? t->focus_fg : t->text_fg;
        bg = focused ? t->focus_bg : t->text_bg;
        screen_puts(s, x, y, w->checked ? "[\xe2\x9c\x93]" : "[ ]", fg, bg, 3);
        screen_label(s, x + 4, y, w->label, fg, t->hot, bg);
        break;
    case W_BUTTON:
        screen_put(s, x, y, '[', fg, bg);
        screen_put(s, x + 1, y, ' ', t->text_fg, bg);
        screen_label(s, x + 2, y, w->label, fg, focused ? t->sel_hot : t->hot, bg);
        screen_put(s, x + w->w - 2, y, ' ', t->text_fg, bg);
        screen_put(s, x + w->w - 1, y, ']', fg, bg);
        break;
    }
}

static void draw_list(Dialog *d, Screen *s, const Theme *t)
{
    int lx = d->x + d->list_x, ly = d->y + d->list_y, r;

    if (d->list_frame)
        screen_frame(s, lx - 1, ly - 1, d->list_w + 2, d->list_h + 2, 0,
                     t->frame, t->text_bg);
    if (d->sel >= 0) {
        if (d->sel < d->scroll)
            d->scroll = d->sel;
        if (d->sel >= d->scroll + d->list_h)
            d->scroll = d->sel - d->list_h + 1;
    }
    for (r = 0; r < d->list_h && d->scroll + r < d->nitems; r++) {
        int idx = d->scroll + r, sel = idx == d->sel;
        const char *name = d->items[idx];
        int is_dir = name[0] && name[strlen(name) - 1] == '/';
        screen_fill(s, lx, ly + r, d->list_w, 1, ' ',
                    sel ? t->sel_fg : t->text_fg, sel ? t->sel_bg : t->text_bg);
        screen_puts(s, lx + 1, ly + r, name,
                    sel ? t->sel_fg : is_dir ? t->dir : t->text_fg,
                    sel ? t->sel_bg : t->text_bg, d->list_w - 2);
    }
    if (d->scroll > 0)
        screen_put(s, lx + d->list_w, ly, 0x25B2, t->frame, t->text_bg);
    if (d->scroll + d->list_h < d->nitems)
        screen_put(s, lx + d->list_w, ly + d->list_h - 1, 0x25BC, t->frame,
                   t->text_bg);
}

void dlg_draw(Dialog *d, Screen *s, const Theme *t, int blink_on)
{
    char title[sizeof d->title + 2];
    int i, tw;

    if (d->kind == DLG_NONE)
        return;
    place(d, s);
    screen_fill(s, d->x, d->y, d->w, d->h, ' ', t->text_fg, t->text_bg);
    screen_frame(s, d->x, d->y, d->w, d->h, 1, t->frame, t->text_bg);
    str_copy(title, sizeof title, " ");
    str_cat(title, sizeof title, d->title);
    str_cat(title, sizeof title, " ");
    tw = utf8_width(title);
    screen_puts(s, d->x + (d->w - tw) / 2, d->y, title, t->title_fg, t->title_bg, tw);

    for (i = 0; i < d->n; i++)
        draw_widget(d, s, t, &d->wd[i], i == d->focus, blink_on);
    if (d->list_h > 0)
        draw_list(d, s, t);
    if (d->msg[0])
        screen_puts(s, d->x + 2, d->y + d->h - 4, d->msg, t->bad, t->text_bg,
                    d->w - 4);
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */

static void focus_step(Dialog *d, int dir)
{
    int i = d->focus, k;
    for (k = 0; k < d->n; k++) {
        i = (i + dir + d->n) % d->n;
        if (focusable(&d->wd[i]))
            break;
    }
    d->focus = i;
}

int dlg_key(Dialog *d, const SDL_KeyboardEvent *k)
{
    Widget *w = d->focus >= 0 ? &d->wd[d->focus] : NULL;
    int ctrl = (k->keysym.mod & KMOD_CTRL) != 0;
    int shift = (k->keysym.mod & KMOD_SHIFT) != 0;
#ifdef __APPLE__
    int alt = 0;                        /* Option types characters */
#else
    int alt = (k->keysym.mod & KMOD_ALT) != 0;
#endif
    SDL_Keycode sym = k->keysym.sym;
    int i, step;

    switch (sym) {
    case SDLK_ESCAPE:
        return ID_CANCEL;
    case SDLK_TAB:
        focus_step(d, shift ? -1 : 1);
        return ID_NONE;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return w && w->kind == W_BUTTON ? w->id : d->def_id;
    }
    /* Alt+letter (or a plain letter when no field has focus) presses the
     * matching button or toggles the matching checkbox */
    if (alt || (w && w->kind != W_FIELD && sym < 128 && sym > ' ')) {
        for (i = 0; i < d->n; i++) {
            Widget *b = &d->wd[i];
            if ((b->kind == W_BUTTON || b->kind == W_CHECK) &&
                label_hotkey(b->label) == (int)sym) {
                if (b->kind == W_BUTTON)
                    return b->id;
                b->checked = !b->checked;
                d->focus = i;
                return ID_NONE;
            }
        }
    }
    if ((step = list_step(d, sym)) != 0) {
        if (d->sel < 0)
            list_scroll(d, step);
        else
            list_select(d, d->sel + step);
        return ID_NONE;
    }
    if (!w)
        return ID_NONE;
    switch (w->kind) {
    case W_FIELD:
        field_key(w, sym, ctrl);
        d->dirty = 1;
        break;
    case W_CHECK:
        if (sym == SDLK_SPACE)
            w->checked = !w->checked;
        break;
    case W_BUTTON:
        if (sym == SDLK_SPACE)
            return w->id;
        if (sym == SDLK_LEFT || sym == SDLK_UP)
            focus_step(d, -1);
        else if (sym == SDLK_RIGHT || sym == SDLK_DOWN)
            focus_step(d, 1);
        break;
    }
    return ID_NONE;
}

void dlg_text(Dialog *d, const char *text)
{
    Widget *w = d->focus >= 0 ? &d->wd[d->focus] : NULL;
    if (!w || w->kind != W_FIELD) {
        /* typing on the list goes to its field */
        if (!d->list_field || !(w = dlg_find(d, d->list_field)))
            return;
        d->focus = (int)(w - d->wd);
    }
    field_insert(w, text, strlen(text));
    d->dirty = 1;
}

int dlg_click(Dialog *d, int cx, int cy, int clicks)
{
    int rx = cx - d->x, ry = cy - d->y, r = ry - d->list_y, i;

    if (d->list_h > 0 && d->sel >= 0 && rx >= d->list_x &&
        rx < d->list_x + d->list_w && r >= 0 && r < d->list_h &&
        d->scroll + r < d->nitems) {
        list_select(d, d->scroll + r);
        return clicks >= 2 ? d->def_id : ID_NONE;
    }
    for (i = 0; i < d->n; i++) {
        Widget *w = &d->wd[i];
        int ww = w->kind == W_CHECK ? label_width(w->label) + 4 : w->w;
        if (!focusable(w) || ry != w->y || rx < w->x || rx >= w->x + ww)
            continue;
        d->focus = i;
        if (w->kind == W_BUTTON)
            return w->id;
        if (w->kind == W_CHECK) {
            w->checked = !w->checked;
        } else if (w->kind == W_FIELD) {
            /* place the cursor near the click */
            size_t p = 0;
            int c = rx - w->x;
            while (c-- > 0 && p < w->len)
                p = utf8_next(w->text, w->len, p);
            w->cur = p;
        }
        break;
    }
    return ID_NONE;
}

void dlg_wheel(Dialog *d, int lines)
{
    if (d->list_h <= 0)
        return;
    list_scroll(d, lines);
    if (d->sel >= 0) {
        if (d->sel < d->scroll)
            d->sel = d->scroll;
        if (d->sel >= d->scroll + d->list_h)
            d->sel = d->scroll + d->list_h - 1;
    }
}
