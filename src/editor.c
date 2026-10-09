/*
 * editor.c - cursor movement, selection and edit commands.
 */
#include "editor.h"
#include "utf8.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

enum { K_NONE, K_TYPE, K_BACK, K_DEL, K_OTHER };

#define GROUP_MS 1000   /* a pause this long starts a new undo step */

void ed_options_init(EdOptions *opt)
{
    memset(opt, 0, sizeof *opt);
    opt->tabw = 4;
    opt->icase = 1;
}

static Doc *doc_new(Buffer *b, const char *path)
{
    Doc *d = (Doc *)xmalloc(sizeof *d);
    memset(d, 0, sizeof *d);
    d->buf = b;
    undo_init(&d->undo);
    d->path = path ? xstrdup(path) : NULL;
    return d;
}

static void attach(Editor *ed, Doc *d)
{
    ed->doc = d;
    ed->next_view = d->views;
    d->views = ed;
}

/* Unlinks ed from its document, and frees the document if no other view
 * shows it. */
static void detach(Editor *ed)
{
    Doc *d = ed->doc;
    Editor **p = &d->views;
    while (*p != ed)
        p = &(*p)->next_view;
    *p = ed->next_view;
    ed->doc = NULL;
    ed->next_view = NULL;
    if (d->last_ed == ed)
        d->last_ed = NULL;
    if (d->views)
        return;
    if (d->free_data)
        d->free_data(d->data);
    buf_free(d->buf);
    undo_free(&d->undo);
    free(d->path);
    free(d);
}

static void init_view(Editor *ed, EdOptions *opt)
{
    memset(ed, 0, sizeof *ed);
    ed->opt = opt;
    ed->want = -1;
    ed->view_w = 80;
    ed->view_h = 25;
}

void ed_init(Editor *ed, EdOptions *opt)
{
    init_view(ed, opt);
    attach(ed, doc_new(buf_new(), NULL));
}

void ed_init_view(Editor *ed, const Editor *from)
{
    init_view(ed, from->opt);
    ed->cy = from->cy;
    ed->cx = from->cx;
    ed->sel = from->sel;
    ed->ay = from->ay;
    ed->ax = from->ax;
    ed->want = from->want;
    ed->top = from->top;
    ed->top_row = from->top_row;
    ed->left = from->left;
    ed->view_w = from->view_w;
    ed->view_h = from->view_h;
    ed->moved = 1;
    attach(ed, from->doc);
}

void ed_free(Editor *ed)
{
    if (ed->doc)
        detach(ed);
}

int ed_views(const Editor *ed)
{
    const Editor *v;
    int n = 0;
    for (v = ed->doc->views; v; v = v->next_view)
        n++;
    return n;
}

static void reset_view(Editor *ed)
{
    ed->cy = ed->ay = 0;
    ed->cx = ed->ax = 0;
    ed->sel = 0;
    ed->want = -1;
    ed->top = ed->left = 0;
    ed->top_row = 0;
    ed->moved = 1;
    ed->follow = 1;
}

/* Shows a new document of buffer b in this view. */
static void set_buffer(Editor *ed, Buffer *b, const char *path)
{
    Doc *d = doc_new(b, path);
    detach(ed);
    attach(ed, d);
    reset_view(ed);
}

void ed_new(Editor *ed)
{
    set_buffer(ed, buf_new(), NULL);
}

int ed_open(Editor *ed, const char *path, char *err, size_t errlen)
{
    Buffer *b = buf_new();
    int is_new;

    if (buf_open(b, path, &is_new, err, errlen) < 0) {
        buf_free(b);
        return -1;
    }
    set_buffer(ed, b, path);
    return is_new;
}

int ed_save(Editor *ed, const char *path, char *err, size_t errlen)
{
    Doc *d = ed->doc;
    char *p;
    if (buf_save(d->buf, path, err, errlen) < 0)
        return -1;
    undo_mark_saved(&d->undo);
    d->last_kind = K_NONE;
    p = xstrdup(path);          /* path may be d->path itself */
    free(d->path);
    d->path = p;
    return 0;
}

int ed_modified(const Editor *ed)
{
    return undo_modified(&ed->doc->undo);
}

const char *ed_name(const Editor *ed)
{
    const char *s, *path = ed->doc->path;
    if (!path)
        return "Untitled";
    s = strrchr(path, '/');
    return s ? s + 1 : path;
}

long ed_lines(Editor *ed)
{
    return buf_lines(ed->doc->buf);
}

const char *ed_line(Editor *ed, long ln, size_t *len)
{
    return buf_line(ed->doc->buf, ln, len);
}

static size_t line_len(Editor *ed, long ln)
{
    size_t n;
    buf_line(ed->doc->buf, ln, &n);
    return n;
}

static size_t pos_off(Editor *ed, long ln, size_t col)
{
    return buf_line_offset(ed->doc->buf, ln) + col;
}

static size_t cur_off(Editor *ed)
{
    return pos_off(ed, ed->cy, ed->cx);
}

static void off_pos(Editor *ed, size_t off, long *ln, size_t *col)
{
    size_t len;
    buf_offset_to_pos(ed->doc->buf, off, ln, col);
    len = line_len(ed, *ln);
    if (*col > len)
        *col = len;
}

static void clamp(Editor *ed)
{
    long n = ed_lines(ed);
    size_t len;
    if (ed->cy >= n)
        ed->cy = n - 1;
    if (ed->cy < 0)
        ed->cy = 0;
    len = line_len(ed, ed->cy);
    if (ed->cx > len)
        ed->cx = len;
}

/* ------------------------------------------------------------------ */
/* display columns                                                     */
/* ------------------------------------------------------------------ */

long ed_disp_col(const Editor *ed, const char *s, size_t len, size_t col)
{
    size_t i = 0;
    long d = 0;
    if (col > len)
        col = len;
    while (i < col) {
        if (s[i] == '\t') {
            d = (d / ed->opt->tabw + 1) * ed->opt->tabw;
            i++;
        } else {
            i = utf8_next(s, len, i);
            d++;
        }
    }
    return d;
}

size_t ed_byte_col(const Editor *ed, const char *s, size_t len, long dcol)
{
    size_t i = 0;
    long d = 0;
    while (i < len) {
        long w = s[i] == '\t' ? (d / ed->opt->tabw + 1) * ed->opt->tabw - d : 1;
        if (d + w > dcol)
            break;
        d += w;
        i = utf8_next(s, len, i);
    }
    return i;
}

/* ------------------------------------------------------------------ */
/* rows (word wrap)                                                    */
/* ------------------------------------------------------------------ */

int ed_wrap_width(const Editor *ed)
{
    return ed->view_w > 2 ? ed->view_w - 1 : 1;
}

size_t ed_row_end(const Editor *ed, const char *s, size_t len, size_t start)
{
    size_t i = start, brk = start;
    long x = 0, w = ed_wrap_width(ed), tabw = ed->opt->tabw;

    if (!ed->opt->wrap)
        return len;
    while (i < len) {
        long cw = s[i] == '\t' ? tabw - x % tabw : 1;
        if (x + cw > w && i > start)
            return brk > start ? brk : i;
        x += cw;
        if (s[i] == ' ' || s[i] == '\t')
            brk = ++i;
        else
            i = utf8_next(s, len, i);
    }
    return len;
}

size_t ed_row_start(const Editor *ed, const char *s, size_t len, long row)
{
    size_t start = 0, end;
    while (row-- > 0 && (end = ed_row_end(ed, s, len, start)) < len)
        start = end;
    return start;
}

long ed_row_of(const Editor *ed, const char *s, size_t len, size_t col, size_t *start)
{
    size_t a = 0, b;
    long row = 0;
    while ((b = ed_row_end(ed, s, len, a)) < len && col >= b) {
        a = b;
        row++;
    }
    *start = a;
    return row;
}

static long line_rows(Editor *ed, long ln)
{
    size_t len, start;
    const char *l = ed_line(ed, ln, &len);
    return ed_row_of(ed, l, len, len, &start) + 1;
}

/* Moves the row position (*ln, *row) n rows down, or up if n < 0, as far
 * as the document goes. Returns how many rows it moved. */
static long step_rows(Editor *ed, long *ln, long *row, long n)
{
    long moved = 0, last = ed_lines(ed) - 1;
    for (; n > 0; n--, moved++) {
        if (*row + 1 < line_rows(ed, *ln))
            (*row)++;
        else if (*ln < last) {
            (*ln)++;
            *row = 0;
        } else
            break;
    }
    for (; n < 0; n++, moved--) {
        if (*row > 0)
            (*row)--;
        else if (*ln > 0) {
            (*ln)--;
            *row = line_rows(ed, *ln) - 1;
        } else
            break;
    }
    return moved;
}

/* Keeps the top line in the document and its row in the line. */
static void fix_top(Editor *ed)
{
    long rows;
    if (ed->top >= ed_lines(ed))
        ed->top = ed_lines(ed) - 1;
    if (ed->top < 0)
        ed->top = 0;
    if (!ed->opt->wrap) {
        ed->top_row = 0;
        return;
    }
    rows = line_rows(ed, ed->top);
    if (ed->top_row >= rows)
        ed->top_row = rows - 1;
    if (ed->top_row < 0)
        ed->top_row = 0;
}

void ed_cursor_spot(Editor *ed, long *row, long *x)
{
    size_t len, start;
    const char *l = ed_line(ed, ed->cy, &len);
    long crow = ed_row_of(ed, l, len, ed->cx, &start), ln;

    *x = ed_disp_col(ed, l + start, len - start, ed->cx - start) - ed->left;
    fix_top(ed);
    if (ed->cy < ed->top) {
        *row = -1;
    } else if (ed->cy - ed->top >= ed->view_h) {
        *row = ed->view_h;
    } else {
        /* a line takes at least a row, so this counts fewer than view_h */
        *row = crow - ed->top_row;
        for (ln = ed->top; ln < ed->cy; ln++)
            *row += line_rows(ed, ln);
    }
}

void ed_pos_at(Editor *ed, long row, long x, long *ln, size_t *col)
{
    size_t len, start, end;
    const char *l;
    long r;

    fix_top(ed);
    *ln = ed->top;
    r = ed->top_row;
    step_rows(ed, ln, &r, row);
    l = ed_line(ed, *ln, &len);
    start = ed_row_start(ed, l, len, r);
    end = ed_row_end(ed, l, len, start);
    *col = start + ed_byte_col(ed, l + start, end - start, ed->left + x);
    if (*col == end && end < len)
        *col = utf8_prev(l, end);
}

/* ------------------------------------------------------------------ */
/* movement and selection                                              */
/* ------------------------------------------------------------------ */

static int char_class(const char *s, size_t len, size_t i)
{
    unsigned char c;
    if (i >= len)
        return 0;
    c = (unsigned char)s[i];
    if (c == ' ' || c == '\t')
        return 0;
    if (c >= 0x80 || c == '_' || (c >= '0' && c <= '9') ||
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
        return 1;
    return 2;
}

static size_t word_right(const char *s, size_t len, size_t i)
{
    int c = char_class(s, len, i);
    while (i < len && char_class(s, len, i) == c && c != 0)
        i = utf8_next(s, len, i);
    while (i < len && char_class(s, len, i) == 0)
        i++;
    return i;
}

static size_t word_left(const char *s, size_t i)
{
    int c;
    while (i > 0 && char_class(s, i, i - 1) == 0)
        i--;
    if (i == 0)
        return 0;
    c = char_class(s, i, utf8_prev(s, i));
    while (i > 0 && char_class(s, i, utf8_prev(s, i)) == c)
        i = utf8_prev(s, i);
    return i;
}

int ed_sel_range(const Editor *ed, long *sy, size_t *sx, long *ey, size_t *ex)
{
    if (!ed->sel || (ed->ay == ed->cy && ed->ax == ed->cx))
        return 0;
    if (ed->ay < ed->cy || (ed->ay == ed->cy && ed->ax < ed->cx)) {
        *sy = ed->ay;
        *sx = ed->ax;
        *ey = ed->cy;
        *ex = ed->cx;
    } else {
        *sy = ed->cy;
        *sx = ed->cx;
        *ey = ed->ay;
        *ex = ed->ax;
    }
    return 1;
}

int ed_has_selection(const Editor *ed)
{
    long sy, ey;
    size_t sx, ex;
    return ed_sel_range(ed, &sy, &sx, &ey, &ex);
}

static void begin_move(Editor *ed, int extend)
{
    if (extend && !ed->sel) {
        ed->sel = 1;
        ed->ay = ed->cy;
        ed->ax = ed->cx;
    } else if (!extend) {
        ed->sel = 0;
    }
    ed->moved = 1;
    ed->follow = 1;
}

void ed_move(Editor *ed, int how, int extend)
{
    size_t len;
    const char *l;
    long sy = 0, ey = 0, n;
    size_t sx = 0, ex = 0;
    int had_sel = ed_sel_range(ed, &sy, &sx, &ey, &ex);
    int keep_want = how == MV_UP || how == MV_DOWN || how == MV_PGUP ||
                    how == MV_PGDN;

    begin_move(ed, extend);
    if (!keep_want)
        ed->want = -1;
    l = ed_line(ed, ed->cy, &len);
    n = ed_lines(ed);

    /* Left/Right with a selection and no Shift collapse to its edge. */
    if (had_sel && !extend && (how == MV_LEFT || how == MV_RIGHT)) {
        if (how == MV_LEFT) {
            ed->cy = sy;
            ed->cx = sx;
        } else {
            ed->cy = ey;
            ed->cx = ex;
        }
        return;
    }

    switch (how) {
    case MV_LEFT:
        if (ed->cx > 0)
            ed->cx = utf8_prev(l, ed->cx);
        else if (ed->cy > 0) {
            ed->cy--;
            ed->cx = line_len(ed, ed->cy);
        }
        break;
    case MV_RIGHT:
        if (ed->cx < len)
            ed->cx = utf8_next(l, len, ed->cx);
        else if (ed->cy < n - 1) {
            ed->cy++;
            ed->cx = 0;
        }
        break;
    case MV_WORDLEFT:
        if (ed->cx > 0)
            ed->cx = word_left(l, ed->cx);
        else if (ed->cy > 0) {
            ed->cy--;
            ed->cx = line_len(ed, ed->cy);
        }
        break;
    case MV_WORDRIGHT:
        if (ed->cx < len)
            ed->cx = word_right(l, len, ed->cx);
        else if (ed->cy < n - 1) {
            ed->cy++;
            ed->cx = 0;
        }
        break;
    case MV_HOME: {
        /* smart home: first non-blank, then column 0 */
        size_t i = 0;
        while (i < len && (l[i] == ' ' || l[i] == '\t'))
            i++;
        ed->cx = ed->cx == i ? 0 : i;
        break;
    }
    case MV_END:
        ed->cx = len;
        break;
    case MV_UP:
    case MV_DOWN:
    case MV_PGUP:
    case MV_PGDN: {
        long d = how == MV_UP ? -1 : how == MV_DOWN ? 1
               : (how == MV_PGUP ? -1 : 1) * (ed->view_h > 2 ? ed->view_h - 1 : 1);
        size_t start, end;
        long row = ed_row_of(ed, l, len, ed->cx, &start);
        if (ed->want < 0)
            ed->want = ed_disp_col(ed, l + start, len - start, ed->cx - start);
        if (how == MV_PGUP || how == MV_PGDN)
            ed_scroll(ed, d);
        /* Up on the first row goes to the start, Down on the last to the end */
        if (!step_rows(ed, &ed->cy, &row, d) && (how == MV_UP || how == MV_DOWN)) {
            ed->cx = how == MV_UP ? 0 : len;
            break;
        }
        l = ed_line(ed, ed->cy, &len);
        start = ed_row_start(ed, l, len, row);
        end = ed_row_end(ed, l, len, start);
        ed->cx = start + ed_byte_col(ed, l + start, end - start, ed->want);
        if (ed->cx == end && end < len)
            ed->cx = utf8_prev(l, end);
        break;
    }
    case MV_DOCSTART:
        ed->cy = 0;
        ed->cx = 0;
        break;
    case MV_DOCEND:
        buf_load_all(ed->doc->buf);
        ed->cy = ed_lines(ed) - 1;
        ed->cx = line_len(ed, ed->cy);
        break;
    }
}

void ed_set_cursor(Editor *ed, long ln, size_t col, int extend)
{
    begin_move(ed, extend);
    ed->cy = ln;
    ed->cx = col;
    ed->want = -1;
    clamp(ed);
}

void ed_select_all(Editor *ed)
{
    buf_load_all(ed->doc->buf);
    ed->sel = 1;
    ed->ay = 0;
    ed->ax = 0;
    ed->cy = ed_lines(ed) - 1;
    ed->cx = line_len(ed, ed->cy);
    ed->moved = 1;
}

void ed_select_word(Editor *ed)
{
    size_t len, a, b;
    const char *l = ed_line(ed, ed->cy, &len);
    int c = char_class(l, len, ed->cx);

    a = b = ed->cx;
    while (b < len && char_class(l, len, b) == c)
        b = utf8_next(l, len, b);
    while (a > 0 && char_class(l, len, utf8_prev(l, a)) == c)
        a = utf8_prev(l, a);
    ed->sel = 1;
    ed->ay = ed->cy;
    ed->ax = a;
    ed->cx = b;
    ed->moved = 1;
}

void ed_select_line(Editor *ed)
{
    ed->sel = 1;
    ed->ay = ed->cy;
    ed->ax = 0;
    if (ed->cy < ed_lines(ed) - 1) {
        ed->cy++;
        ed->cx = 0;
    } else {
        ed->cx = line_len(ed, ed->cy);
    }
    ed->moved = 1;
}

void ed_clear_selection(Editor *ed)
{
    ed->sel = 0;
}

void ed_scroll(Editor *ed, long rows)
{
    long max = ed_lines(ed) - ed->view_h, mrow;

    if (!ed->opt->wrap) {
        ed->top += rows;
        if (ed->top > max)
            ed->top = max;
        if (ed->top < 0)
            ed->top = 0;
        ed->top_row = 0;
        return;
    }
    fix_top(ed);
    step_rows(ed, &ed->top, &ed->top_row, rows);
    /* the last row stays at the bottom, or below it; with view_h lines
     * below the top, it is */
    if (ed->top <= max)
        return;
    max = ed_lines(ed) - 1;
    mrow = line_rows(ed, max) - 1;
    step_rows(ed, &max, &mrow, -(ed->view_h - 1));
    if (ed->top > max || (ed->top == max && ed->top_row > mrow)) {
        ed->top = max;
        ed->top_row = mrow;
    }
}

void ed_scroll_cursor_to(Editor *ed, long row)
{
    size_t len, start;
    const char *l = ed_line(ed, ed->cy, &len);
    ed->top = ed->cy;
    ed->top_row = ed_row_of(ed, l, len, ed->cx, &start);
    step_rows(ed, &ed->top, &ed->top_row, -row);
}

void ed_scroll_to_cursor(Editor *ed)
{
    size_t len;
    const char *l;
    long d, h = ed->view_h > 0 ? ed->view_h : 1, w = ed->view_w > 0 ? ed->view_w : 1;

    clamp(ed);
    if (ed->opt->wrap) {
        long row, x;
        ed_cursor_spot(ed, &row, &x);
        ed->left = 0;
        /* far jumps put the cursor a third of the way down */
        if (row < 0)
            ed_scroll_cursor_to(ed, ed->top - ed->cy > h ? h / 3 : 0);
        else if (row >= h)
            ed_scroll_cursor_to(ed, ed->cy - ed->top > 2 * h ? h / 3 : h - 1);
        ed->follow = 0;
        return;
    }
    if (ed->cy < ed->top)
        ed->top = ed->top - ed->cy > h ? ed->cy - h / 3 : ed->cy;
    else if (ed->cy >= ed->top + h)
        ed->top = ed->cy - (ed->top + h) > h ? ed->cy - h / 3 : ed->cy - h + 1;
    if (ed->top < 0)
        ed->top = 0;

    l = ed_line(ed, ed->cy, &len);
    d = ed_disp_col(ed, l, len, ed->cx);
    if (d < ed->left)
        ed->left = d < w / 2 ? 0 : d - w / 2;
    else if (d >= ed->left + w)
        ed->left = d - w + w / 4 + 1;
    ed->follow = 0;
}

/* ------------------------------------------------------------------ */
/* editing                                                             */
/* ------------------------------------------------------------------ */

/*
 * The other views of the document keep their place in the text: before an
 * edit their positions become offsets, each change moves the offsets, and
 * after it they become positions again.
 */

/* Moves offset p past inserting (ins) or deleting n bytes at off. A view
 * at off stays before inserted text, except a top line (after). */
static size_t map_off(size_t p, int ins, size_t off, size_t n, int after)
{
    if (ins)
        return p > off || (after && p == off) ? p + n : p;
    if (p >= off + n)
        return p - n;
    return p > off ? off : p;
}

static void views_save(Editor *ed)
{
    Editor *v;
    for (v = ed->doc->views; v; v = v->next_view) {
        if (v == ed)
            continue;
        clamp(v);
        if (v->top >= ed_lines(v))
            v->top = ed_lines(v) - 1;
        v->o_cur = cur_off(v);
        v->o_anc = v->sel ? pos_off(v, v->ay, v->ax) : v->o_cur;
        v->o_top = buf_line_offset(v->doc->buf, v->top);
    }
}

static void views_map(Editor *ed, int ins, size_t off, size_t n)
{
    Editor *v;
    for (v = ed->doc->views; v; v = v->next_view) {
        if (v == ed)
            continue;
        v->o_cur = map_off(v->o_cur, ins, off, n, 0);
        v->o_anc = map_off(v->o_anc, ins, off, n, 0);
        v->o_top = map_off(v->o_top, ins, off, n, 1);
    }
}

static void views_restore(Editor *ed)
{
    Editor *v;
    size_t col;
    for (v = ed->doc->views; v; v = v->next_view) {
        if (v == ed)
            continue;
        off_pos(v, v->o_cur, &v->cy, &v->cx);
        if (v->sel)
            off_pos(v, v->o_anc, &v->ay, &v->ax);
        off_pos(v, v->o_top, &v->top, &col);
        ed_scroll(v, 0);
    }
}

/* Edits the buffer, recording the change for undo. */
static void ins(Editor *ed, size_t off, const char *s, size_t n, size_t cursor)
{
    undo_insert(&ed->doc->undo, ed->doc->buf, off, s, n, cursor);
    views_map(ed, 1, off, n);
}

static void del(Editor *ed, size_t off, size_t n, size_t cursor)
{
    size_t size = buf_size(ed->doc->buf);
    if (off + n > size)
        n = size - off;
    undo_delete(&ed->doc->undo, ed->doc->buf, off, n, cursor);
    views_map(ed, 0, off, n);
}

/* Starts an edit, deciding whether it continues the current undo step. */
static void group(Editor *ed, int kind, int space, unsigned long now)
{
    Doc *d = ed->doc;
    if (kind == K_OTHER || kind != d->last_kind || ed->moved || ed != d->last_ed ||
        now - d->last_time > GROUP_MS ||
        (kind == K_TYPE && !space && d->last_space))
        undo_boundary(&d->undo);
    d->last_ed = ed;
    d->last_kind = kind;
    d->last_space = space;
    d->last_time = now;
    ed->moved = 0;
    views_save(ed);
}

/* Ends an edit. */
static void after_edit(Editor *ed)
{
    clamp(ed);
    ed->want = -1;
    ed->follow = 1;
    undo_set_cursor(&ed->doc->undo, cur_off(ed));
    views_restore(ed);
}

static int del_sel(Editor *ed)
{
    long sy, ey;
    size_t sx, ex, a, b;

    if (!ed_sel_range(ed, &sy, &sx, &ey, &ex)) {
        ed->sel = 0;
        return 0;
    }
    a = pos_off(ed, sy, sx);
    b = pos_off(ed, ey, ex);
    del(ed, a, b - a, cur_off(ed));
    ed->cy = sy;
    ed->cx = sx;
    ed->sel = 0;
    return 1;
}

/* Deletes the selection as an undo step of its own; returns 0 if there is
 * none. */
static int delete_selection(Editor *ed, unsigned long now)
{
    if (!ed_has_selection(ed)) {
        ed->sel = 0;
        return 0;
    }
    group(ed, K_OTHER, 0, now);
    del_sel(ed);
    after_edit(ed);
    return 1;
}

void ed_type(Editor *ed, const char *s, size_t n, unsigned long now)
{
    size_t off;
    int space = n == 1 && (s[0] == ' ' || s[0] == '\t');

    if (!n)
        return;
    if (ed_has_selection(ed))
        ed->moved = 1;          /* replacing a selection starts a new step */
    group(ed, K_TYPE, space, now);
    del_sel(ed);
    off = cur_off(ed);
    if (ed->opt->overwrite) {
        size_t len, end = ed->cx, i;
        const char *l = ed_line(ed, ed->cy, &len);
        for (i = 0; i < n; i = utf8_next(s, n, i))
            end = utf8_next(l, len, end);
        if (end > ed->cx)
            del(ed, off, end - ed->cx, off);
    }
    ins(ed, off, s, n, off);
    ed->cx += n;
    after_edit(ed);
}

void ed_newline(Editor *ed, unsigned long now)
{
    size_t len, i = 0, off;
    const char *l;
    char *t;
    size_t eol = ed->doc->buf->crlf ? 2 : 1;

    group(ed, K_OTHER, 0, now);
    del_sel(ed);
    l = ed_line(ed, ed->cy, &len);
    while (ed->opt->autoindent && i < ed->cx && (l[i] == ' ' || l[i] == '\t'))
        i++;
    t = (char *)xmalloc(eol + i);
    memcpy(t, ed->doc->buf->crlf ? "\r\n" : "\n", eol);
    memcpy(t + eol, l, i);
    off = cur_off(ed);
    ins(ed, off, t, eol + i, off);
    free(t);
    ed->cy++;
    ed->cx = i;
    after_edit(ed);
}

void ed_tab(Editor *ed, int unindent, unsigned long now)
{
    long sy, ey, ln;
    size_t sx, ex;
    int had = ed_sel_range(ed, &sy, &sx, &ey, &ex);

    char sp[16];

    memset(sp, ' ', sizeof sp);
    if (!unindent && (!had || sy == ey)) {
        size_t len;
        const char *l = ed_line(ed, ed->cy, &len);
        long d = ed_disp_col(ed, l, len, had ? sx : ed->cx);
        if (ed->opt->spaces)
            ed_type(ed, sp, (size_t)(ed->opt->tabw - d % ed->opt->tabw), now);
        else
            ed_type(ed, "\t", 1, now);
        return;
    }
    if (!had) {
        sy = ey = ed->cy;
        ex = 1;
    }
    if (ey > sy && ex == 0)
        ey--;
    group(ed, K_OTHER, 0, now);
    for (ln = sy; ln <= ey; ln++) {
        size_t off = pos_off(ed, ln, 0), len, k = 0;
        const char *l;
        if (!unindent) {
            if (ed->opt->spaces)
                ins(ed, off, sp, (size_t)ed->opt->tabw, cur_off(ed));
            else
                ins(ed, off, "\t", 1, cur_off(ed));
            continue;
        }
        l = ed_line(ed, ln, &len);
        if (len && l[0] == '\t')
            k = 1;
        else
            while (k < len && k < (size_t)ed->opt->tabw && l[k] == ' ')
                k++;
        if (k) {
            del(ed, off, k, cur_off(ed));
            if (!had && ln == ed->cy)
                ed->cx = ed->cx > k ? ed->cx - k : 0;
        }
    }
    if (had) {
        ed->sel = 1;
        ed->ay = sy;
        ed->ax = 0;
        ed->cy = ey;
        ed->cx = line_len(ed, ey);
    }
    after_edit(ed);
}

void ed_backspace(Editor *ed, int word, unsigned long now)
{
    size_t len, start;
    const char *l;

    if (delete_selection(ed, now))
        return;
    group(ed, K_BACK, 0, now);
    l = ed_line(ed, ed->cy, &len);
    if (ed->cx > 0) {
        size_t off;
        start = word ? word_left(l, ed->cx) : utf8_prev(l, ed->cx);
        off = pos_off(ed, ed->cy, start);
        del(ed, off, ed->cx - start, off + (ed->cx - start));
        ed->cx = start;
    } else if (ed->cy > 0) {
        size_t plen = line_len(ed, ed->cy - 1);
        size_t a = pos_off(ed, ed->cy - 1, plen), b = cur_off(ed);
        del(ed, a, b - a, b);
        ed->cy--;
        ed->cx = plen;
    }
    after_edit(ed);
}

void ed_delete(Editor *ed, int word, unsigned long now)
{
    size_t len, end, off;
    const char *l;

    if (delete_selection(ed, now))
        return;
    group(ed, K_DEL, 0, now);
    l = ed_line(ed, ed->cy, &len);
    off = cur_off(ed);
    if (ed->cx < len) {
        end = word ? word_right(l, len, ed->cx) : utf8_next(l, len, ed->cx);
        del(ed, off, end - ed->cx, off);
    } else if (ed->cy < ed_lines(ed) - 1) {
        size_t b = pos_off(ed, ed->cy + 1, 0);
        del(ed, off, b - off, off);
    }
    after_edit(ed);
}

void ed_paste(Editor *ed, const char *s, size_t n)
{
    char *t = (char *)xmalloc(n * 2 + 1);
    size_t i, m = 0, off;

    /* normalize line ends to the buffer's format */
    for (i = 0; i < n; i++) {
        if (s[i] == '\r' && i + 1 < n && s[i + 1] == '\n')
            continue;
        if (s[i] == '\n' && ed->doc->buf->crlf)
            t[m++] = '\r';
        t[m++] = s[i];
    }
    group(ed, K_OTHER, 0, 0);
    del_sel(ed);
    off = cur_off(ed);
    ins(ed, off, t, m, off);
    free(t);
    off_pos(ed, off + m, &ed->cy, &ed->cx);
    after_edit(ed);
}

char *ed_copy(Editor *ed, size_t *n)
{
    long sy, ey;
    size_t sx, ex, a, b, i, m = 0;
    char *t;

    if (!ed_sel_range(ed, &sy, &sx, &ey, &ex))
        return NULL;
    a = pos_off(ed, sy, sx);
    b = pos_off(ed, ey, ex);
    t = (char *)xmalloc(b - a + 1);
    buf_copy(ed->doc->buf, a, b - a, t);
    for (i = 0; i < b - a; i++) {
        if (ed->doc->buf->crlf && t[i] == '\r' && i + 1 < b - a && t[i + 1] == '\n')
            continue;
        t[m++] = t[i];
    }
    t[m] = 0;
    *n = m;
    return t;
}

void ed_cut(Editor *ed)
{
    delete_selection(ed, 0);
}

static void after_undo(Editor *ed, size_t c)
{
    off_pos(ed, c, &ed->cy, &ed->cx);
    ed->sel = 0;
    ed->doc->last_kind = K_NONE;
    ed->want = -1;
    ed->follow = 1;
    views_restore(ed);
}

int ed_undo(Editor *ed)
{
    Undo *u = &ed->doc->undo;
    UndoStep *s;
    size_t c;
    int i;

    views_save(ed);
    if (!undo_undo(u, ed->doc->buf, &c))
        return 0;
    /* the step's ops were reverted last to first */
    s = &u->steps[u->pos];
    for (i = s->nops - 1; i >= 0; i--)
        views_map(ed, !s->ops[i].ins, s->ops[i].off, s->ops[i].len);
    after_undo(ed, c);
    return 1;
}

int ed_redo(Editor *ed)
{
    Undo *u = &ed->doc->undo;
    UndoStep *s;
    size_t c;
    int i;

    views_save(ed);
    if (!undo_redo(u, ed->doc->buf, &c))
        return 0;
    s = &u->steps[u->pos - 1];
    for (i = 0; i < s->nops; i++)
        views_map(ed, s->ops[i].ins, s->ops[i].off, s->ops[i].len);
    after_undo(ed, c);
    return 1;
}

/* ------------------------------------------------------------------ */
/* search                                                              */
/* ------------------------------------------------------------------ */

static void select_range(Editor *ed, size_t a, size_t b)
{
    off_pos(ed, a, &ed->ay, &ed->ax);
    off_pos(ed, b, &ed->cy, &ed->cx);
    ed->sel = 1;
    ed->moved = 1;
    ed->want = -1;
    ed->follow = 1;
}

int ed_find(Editor *ed, int backward)
{
    size_t plen = strlen(ed->opt->find), from, m, none = (size_t)-1;
    long sy, ey;
    size_t sx, ex;
    int have = ed_sel_range(ed, &sy, &sx, &ey, &ex), wrapped = 0;

    if (!plen)
        return 0;
    if (!backward) {
        from = have ? pos_off(ed, ey, ex) : cur_off(ed);
        m = buf_find(ed->doc->buf, from, ed->opt->find, plen, ed->opt->icase, 0);
        if (m == none) {
            m = buf_find(ed->doc->buf, 0, ed->opt->find, plen, ed->opt->icase, 0);
            wrapped = 1;
        }
    } else {
        from = have ? pos_off(ed, sy, sx) : cur_off(ed);
        m = from ? buf_find(ed->doc->buf, from - 1, ed->opt->find, plen, ed->opt->icase, 1) : none;
        if (m == none) {
            m = buf_find(ed->doc->buf, buf_size(ed->doc->buf), ed->opt->find, plen, ed->opt->icase, 1);
            wrapped = 1;
        }
    }
    if (m == none)
        return 0;
    select_range(ed, m, m + plen);
    return wrapped ? 2 : 1;
}

int ed_replace(Editor *ed)
{
    long sy, ey;
    size_t sx, ex, plen = strlen(ed->opt->find), rlen = strlen(ed->opt->repl);

    /* replace the selection if it is a match, then find the next one */
    if (ed_sel_range(ed, &sy, &sx, &ey, &ex)) {
        size_t a = pos_off(ed, sy, sx), b = pos_off(ed, ey, ex);
        int same = b - a == plen;
        if (same) {
            char *t = (char *)xmalloc(plen);
            buf_copy(ed->doc->buf, a, plen, t);
            same = mem_match(t, ed->opt->find, plen, ed->opt->icase);
            free(t);
        }
        if (same) {
            group(ed, K_OTHER, 0, 0);
            del(ed, a, plen, cur_off(ed));
            ins(ed, a, ed->opt->repl, rlen, a);
            off_pos(ed, a + rlen, &ed->cy, &ed->cx);
            ed->sel = 0;
            after_edit(ed);
        }
    }
    return ed_find(ed, 0);
}

long ed_replace_all(Editor *ed)
{
    size_t plen = strlen(ed->opt->find), rlen = strlen(ed->opt->repl), off = 0, m;
    size_t cur = cur_off(ed);
    long count = 0;

    if (!plen)
        return 0;
    group(ed, K_OTHER, 0, 0);
    while ((m = buf_find(ed->doc->buf, off, ed->opt->find, plen, ed->opt->icase, 0)) != (size_t)-1) {
        del(ed, m, plen, cur);
        ins(ed, m, ed->opt->repl, rlen, cur);
        off = m + rlen;
        count++;
    }
    ed->sel = 0;
    after_edit(ed);
    return count;
}

void ed_goto(Editor *ed, long line)
{
    if (line > ed_lines(ed))
        buf_load_all(ed->doc->buf);
    if (line < 1)
        line = 1;
    if (line > ed_lines(ed))
        line = ed_lines(ed);
    ed_set_cursor(ed, line - 1, 0, 0);
}
