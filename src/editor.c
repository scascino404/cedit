/*
 * editor.c - cursor movement, selection and edit commands.
 */
#include "editor.h"
#include "utf8.h"

#include <stdlib.h>
#include <string.h>

enum { K_NONE, K_TYPE, K_BACK, K_DEL, K_OTHER };

#define GROUP_MS 1000   /* a pause this long starts a new undo step */

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        abort();
    return p;
}

void ed_init(Editor *ed)
{
    memset(ed, 0, sizeof *ed);
    ed->buf = buf_new();
    undo_init(&ed->undo);
    ed->want = -1;
    ed->tabw = 4;
    ed->icase = 1;
    ed->view_w = 80;
    ed->view_h = 25;
}

void ed_free(Editor *ed)
{
    buf_free(ed->buf);
    undo_free(&ed->undo);
    free(ed->path);
    ed->buf = NULL;
    ed->path = NULL;
}

static void reset_view(Editor *ed)
{
    ed->cy = ed->ay = 0;
    ed->cx = ed->ax = 0;
    ed->sel = 0;
    ed->want = -1;
    ed->top = ed->left = 0;
    ed->last_kind = K_NONE;
    ed->follow = 1;
}

void ed_new(Editor *ed)
{
    buf_free(ed->buf);
    undo_free(&ed->undo);
    ed->buf = buf_new();
    undo_init(&ed->undo);
    free(ed->path);
    ed->path = NULL;
    reset_view(ed);
}

static char *dupstr(const char *s)
{
    char *d = (char *)xmalloc(strlen(s) + 1);
    strcpy(d, s);
    return d;
}

int ed_open(Editor *ed, const char *path, char *err, size_t errlen)
{
    Buffer *b = buf_new();
    int is_new;

    if (buf_open(b, path, &is_new, err, errlen) < 0) {
        buf_free(b);
        return -1;
    }
    buf_free(ed->buf);
    undo_free(&ed->undo);
    ed->buf = b;
    undo_init(&ed->undo);
    free(ed->path);
    ed->path = dupstr(path);
    reset_view(ed);
    return is_new;
}

int ed_save(Editor *ed, const char *path, char *err, size_t errlen)
{
    if (buf_save(ed->buf, path, err, errlen) < 0)
        return -1;
    undo_mark_saved(&ed->undo);
    ed->last_kind = K_NONE;
    if (ed->path != path) {
        free(ed->path);
        ed->path = dupstr(path);
    }
    return 0;
}

int ed_modified(const Editor *ed)
{
    return undo_modified(&ed->undo);
}

const char *ed_name(const Editor *ed)
{
    const char *s;
    if (!ed->path)
        return "Untitled";
    s = strrchr(ed->path, '/');
    return s ? s + 1 : ed->path;
}

long ed_lines(Editor *ed)
{
    return buf_lines(ed->buf);
}

const char *ed_line(Editor *ed, long ln, size_t *len)
{
    return buf_line(ed->buf, ln, len);
}

static size_t line_len(Editor *ed, long ln)
{
    size_t n;
    buf_line(ed->buf, ln, &n);
    return n;
}

static size_t pos_off(Editor *ed, long ln, size_t col)
{
    return buf_line_offset(ed->buf, ln) + col;
}

static size_t cur_off(Editor *ed)
{
    return pos_off(ed, ed->cy, ed->cx);
}

static void off_pos(Editor *ed, size_t off, long *ln, size_t *col)
{
    size_t len;
    buf_offset_to_pos(ed->buf, off, ln, col);
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
            d = (d / ed->tabw + 1) * ed->tabw;
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
        long w = s[i] == '\t' ? (d / ed->tabw + 1) * ed->tabw - d : 1;
        if (d + w > dcol)
            break;
        d += w;
        i = utf8_next(s, len, i);
    }
    return i;
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
        if (ed->want < 0)
            ed->want = ed_disp_col(ed, l, len, ed->cx);
        if (how == MV_PGUP || how == MV_PGDN)
            ed_scroll(ed, d);
        if (how == MV_UP && ed->cy == 0) {
            ed->cx = 0;
            break;
        }
        if (how == MV_DOWN && ed->cy == n - 1) {
            ed->cx = len;
            break;
        }
        ed->cy += d;
        if (ed->cy < 0)
            ed->cy = 0;
        if (ed->cy >= ed_lines(ed))
            ed->cy = ed_lines(ed) - 1;
        l = ed_line(ed, ed->cy, &len);
        ed->cx = ed_byte_col(ed, l, len, ed->want);
        break;
    }
    case MV_DOCSTART:
        ed->cy = 0;
        ed->cx = 0;
        break;
    case MV_DOCEND:
        buf_load_all(ed->buf);
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
    buf_load_all(ed->buf);
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

void ed_scroll(Editor *ed, long lines)
{
    long max = ed_lines(ed) - ed->view_h;
    ed->top += lines;
    if (ed->top > max)
        ed->top = max;
    if (ed->top < 0)
        ed->top = 0;
}

void ed_scroll_to_cursor(Editor *ed, unsigned long now)
{
    size_t len;
    const char *l;
    long d, h = ed->view_h > 0 ? ed->view_h : 1, w = ed->view_w > 0 ? ed->view_w : 1;

    (void)now;
    clamp(ed);
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

/* Decides whether this edit continues the current undo step. */
static void group(Editor *ed, int kind, int space, unsigned long now)
{
    if (kind == K_OTHER || kind != ed->last_kind || ed->moved ||
        now - ed->last_time > GROUP_MS ||
        (kind == K_TYPE && !space && ed->last_space))
        undo_boundary(&ed->undo);
    ed->last_kind = kind;
    ed->last_space = space;
    ed->last_time = now;
    ed->moved = 0;
}

static void after_edit(Editor *ed)
{
    clamp(ed);
    ed->want = -1;
    ed->follow = 1;
    undo_set_cursor(&ed->undo, cur_off(ed));
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
    undo_delete(&ed->undo, ed->buf, a, b - a, cur_off(ed));
    ed->cy = sy;
    ed->cx = sx;
    ed->sel = 0;
    return 1;
}

void ed_type(Editor *ed, const char *s, size_t n, unsigned long now)
{
    size_t off;
    int space = n == 1 && (s[0] == ' ' || s[0] == '\t');
    long sy, ey;
    size_t sx, ex;

    if (!n)
        return;
    if (ed_sel_range(ed, &sy, &sx, &ey, &ex))
        ed->moved = 1;          /* replacing a selection starts a new step */
    group(ed, K_TYPE, space, now);
    del_sel(ed);
    off = cur_off(ed);
    if (ed->overwrite) {
        size_t len, end = ed->cx, i;
        const char *l = ed_line(ed, ed->cy, &len);
        for (i = 0; i < n; i = utf8_next(s, n, i))
            end = utf8_next(l, len, end);
        if (end > ed->cx)
            undo_delete(&ed->undo, ed->buf, off, end - ed->cx, off);
    }
    undo_insert(&ed->undo, ed->buf, off, s, n, off);
    ed->cx += n;
    after_edit(ed);
}

void ed_newline(Editor *ed, unsigned long now)
{
    size_t len, i = 0, off;
    const char *l;
    char *t;
    size_t eol = ed->buf->crlf ? 2 : 1;

    group(ed, K_OTHER, 0, now);
    del_sel(ed);
    l = ed_line(ed, ed->cy, &len);
    while (ed->autoindent && i < ed->cx && (l[i] == ' ' || l[i] == '\t'))
        i++;
    t = (char *)xmalloc(eol + i);
    memcpy(t, ed->buf->crlf ? "\r\n" : "\n", eol);
    memcpy(t + eol, l, i);
    off = cur_off(ed);
    undo_insert(&ed->undo, ed->buf, off, t, eol + i, off);
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

    if (!unindent && (!had || sy == ey)) {
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
            undo_insert(&ed->undo, ed->buf, off, "\t", 1, cur_off(ed));
            continue;
        }
        l = ed_line(ed, ln, &len);
        if (len && l[0] == '\t')
            k = 1;
        else
            while (k < len && k < (size_t)ed->tabw && l[k] == ' ')
                k++;
        if (k) {
            undo_delete(&ed->undo, ed->buf, off, k, cur_off(ed));
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
    ed->last_kind = K_OTHER;
}

void ed_backspace(Editor *ed, int word, unsigned long now)
{
    size_t len, start, sx, ex;
    const char *l;
    long sy, ey;

    if (ed_sel_range(ed, &sy, &sx, &ey, &ex)) {
        group(ed, K_OTHER, 0, now);
        del_sel(ed);
        after_edit(ed);
        return;
    }
    ed->sel = 0;
    group(ed, K_BACK, 0, now);
    l = ed_line(ed, ed->cy, &len);
    if (ed->cx > 0) {
        size_t off;
        start = word ? word_left(l, ed->cx) : utf8_prev(l, ed->cx);
        off = pos_off(ed, ed->cy, start);
        undo_delete(&ed->undo, ed->buf, off, ed->cx - start, off + (ed->cx - start));
        ed->cx = start;
    } else if (ed->cy > 0) {
        size_t plen = line_len(ed, ed->cy - 1);
        size_t a = pos_off(ed, ed->cy - 1, plen), b = cur_off(ed);
        undo_delete(&ed->undo, ed->buf, a, b - a, b);
        ed->cy--;
        ed->cx = plen;
    }
    after_edit(ed);
}

void ed_delete(Editor *ed, int word, unsigned long now)
{
    size_t len, end, off;
    const char *l;
    long sy, ey;
    size_t sx, ex;

    if (ed_sel_range(ed, &sy, &sx, &ey, &ex)) {
        group(ed, K_OTHER, 0, now);
        del_sel(ed);
        after_edit(ed);
        return;
    }
    ed->sel = 0;
    group(ed, K_DEL, 0, now);
    l = ed_line(ed, ed->cy, &len);
    off = cur_off(ed);
    if (ed->cx < len) {
        end = word ? word_right(l, len, ed->cx) : utf8_next(l, len, ed->cx);
        undo_delete(&ed->undo, ed->buf, off, end - ed->cx, off);
    } else if (ed->cy < ed_lines(ed) - 1) {
        size_t b = pos_off(ed, ed->cy + 1, 0);
        undo_delete(&ed->undo, ed->buf, off, b - off, off);
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
        if (s[i] == '\n' && ed->buf->crlf)
            t[m++] = '\r';
        t[m++] = s[i];
    }
    group(ed, K_OTHER, 0, 0);
    del_sel(ed);
    off = cur_off(ed);
    undo_insert(&ed->undo, ed->buf, off, t, m, off);
    free(t);
    off_pos(ed, off + m, &ed->cy, &ed->cx);
    after_edit(ed);
    ed->last_kind = K_OTHER;
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
    buf_copy(ed->buf, a, b - a, t);
    for (i = 0; i < b - a; i++) {
        if (ed->buf->crlf && t[i] == '\r' && i + 1 < b - a && t[i + 1] == '\n')
            continue;
        t[m++] = t[i];
    }
    t[m] = 0;
    *n = m;
    return t;
}

void ed_cut(Editor *ed)
{
    group(ed, K_OTHER, 0, 0);
    if (del_sel(ed))
        after_edit(ed);
    ed->last_kind = K_OTHER;
}

static void after_undo(Editor *ed, size_t c)
{
    off_pos(ed, c, &ed->cy, &ed->cx);
    ed->sel = 0;
    ed->last_kind = K_NONE;
    ed->want = -1;
    ed->follow = 1;
}

int ed_undo(Editor *ed)
{
    size_t c;
    if (!undo_undo(&ed->undo, ed->buf, &c))
        return 0;
    after_undo(ed, c);
    return 1;
}

int ed_redo(Editor *ed)
{
    size_t c;
    if (!undo_redo(&ed->undo, ed->buf, &c))
        return 0;
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
    size_t plen = strlen(ed->find), from, m, none = (size_t)-1;
    long sy, ey;
    size_t sx, ex;
    int have = ed_sel_range(ed, &sy, &sx, &ey, &ex), wrapped = 0;

    if (!plen)
        return 0;
    if (!backward) {
        from = have ? pos_off(ed, ey, ex) : cur_off(ed);
        m = buf_find(ed->buf, from, ed->find, plen, ed->icase, 0);
        if (m == none) {
            m = buf_find(ed->buf, 0, ed->find, plen, ed->icase, 0);
            wrapped = 1;
        }
    } else {
        from = have ? pos_off(ed, sy, sx) : cur_off(ed);
        m = from ? buf_find(ed->buf, from - 1, ed->find, plen, ed->icase, 1) : none;
        if (m == none) {
            m = buf_find(ed->buf, buf_size(ed->buf), ed->find, plen, ed->icase, 1);
            wrapped = 1;
        }
    }
    if (m == none)
        return 0;
    select_range(ed, m, m + plen);
    return wrapped ? 2 : 1;
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

int ed_replace(Editor *ed)
{
    long sy, ey;
    size_t sx, ex, plen = strlen(ed->find), rlen = strlen(ed->repl);

    if (ed_sel_range(ed, &sy, &sx, &ey, &ex)) {
        size_t a = pos_off(ed, sy, sx), b = pos_off(ed, ey, ex), i;
        int same = b - a == plen;
        if (same) {
            char *t = (char *)xmalloc(plen + 1);
            buf_copy(ed->buf, a, plen, t);
            for (i = 0; i < plen && same; i++)
                same = ed->icase ? lower((unsigned char)t[i]) == lower((unsigned char)ed->find[i])
                                 : t[i] == ed->find[i];
            free(t);
        }
        if (same) {
            group(ed, K_OTHER, 0, 0);
            undo_delete(&ed->undo, ed->buf, a, plen, cur_off(ed));
            undo_insert(&ed->undo, ed->buf, a, ed->repl, rlen, a);
            off_pos(ed, a + rlen, &ed->cy, &ed->cx);
            ed->sel = 0;
            after_edit(ed);
            ed->last_kind = K_OTHER;
        }
    }
    return ed_find(ed, 0);
}

long ed_replace_all(Editor *ed)
{
    size_t plen = strlen(ed->find), rlen = strlen(ed->repl), off = 0, m;
    size_t cur = cur_off(ed);
    long count = 0;

    if (!plen)
        return 0;
    group(ed, K_OTHER, 0, 0);
    while ((m = buf_find(ed->buf, off, ed->find, plen, ed->icase, 0)) != (size_t)-1) {
        undo_delete(&ed->undo, ed->buf, m, plen, cur);
        undo_insert(&ed->undo, ed->buf, m, ed->repl, rlen, cur);
        off = m + rlen;
        count++;
    }
    ed->sel = 0;
    clamp(ed);
    after_edit(ed);
    ed->last_kind = K_OTHER;
    return count;
}

void ed_goto(Editor *ed, long line)
{
    if (line > ed_lines(ed))
        buf_load_all(ed->buf);
    if (line < 1)
        line = 1;
    if (line > ed_lines(ed))
        line = ed_lines(ed);
    ed_set_cursor(ed, line - 1, 0, 0);
}
