/*
 * test_editor.c - editing commands: line ends, undo grouping, paste,
 * replace, indentation, word motion, several views of one document.
 */
#define _XOPEN_SOURCE 700
#include "../src/editor.h"
#include "../src/utf8.h"
#include "../src/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "testutil.h"

static int failures;

static char *contents(Editor *ed)
{
    size_t n = buf_size(ed->doc->buf);
    char *t = (char *)malloc(n + 1);
    buf_copy(ed->doc->buf, 0, n, t);
    t[n] = 0;
    return t;
}

static void expect(Editor *ed, const char *want, int line)
{
    char *got = contents(ed);
    if (strcmp(got, want) != 0) {
        printf("FAIL line %d:\n  got  [%s]\n  want [%s]\n", line, got, want);
        failures++;
    }
    free(got);
}

#define EXPECT(ed, s) expect(ed, s, __LINE__)
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void open_text(Editor *ed, const char *text)
{
    char path[512];
    int fd = tmp_file(path, sizeof path, "cedit-ed");
    char err[128];
    if (write(fd, text, strlen(text)) < 0)
        printf("write failed\n");
    close(fd);
    ed_open(ed, path, err, sizeof err);
    buf_load_all(ed->doc->buf);
    unlink(path);
}

static void type(Editor *ed, const char *s, unsigned long *t)
{
    while (*s) {
        ed_type(ed, s, 1, *t);
        *t += 50;
        s++;
    }
}

/* The display column of byte col, a character at a time. */
static long ref_disp_col(int tabw, const char *s, size_t len, size_t col)
{
    size_t i = 0;
    long d = 0;
    while (i < col && i < len) {
        d = s[i] == '\t' ? (d / tabw + 1) * tabw : d + 1;
        i = s[i] == '\t' ? i + 1 : utf8_next(s, len, i);
    }
    return d;
}

/* Columns of long lines (counted in blocks) with tabs, UTF-8 and bad
 * bytes here and there. */
static void test_columns(Editor *ed)
{
    static const char *const parts[] = {"a", "a", "a", "a", "a", "a", "\t",
                                        "\xc3\xa9", "\xe2\x86\xb5", "\xff", " "};
    char s[700];
    unsigned long r = 1;
    int k;

    for (k = 0; k < 300; k++) {
        size_t len = 0, col;
        long dcol, total;
        int rare = k % 3 == 0 ? 2 : 40;     /* how often not "a" */
        while (len < sizeof s - 8) {
            const char *p;
            r = r * 1103515245UL + 12345UL;
            p = (r >> 16) % rare ? "a" : parts[(r >> 8) % 11];
            memcpy(s + len, p, strlen(p));
            len += strlen(p);
        }
        len -= (r >> 4) % 100;
        for (col = 0; col <= len + 1; col++)
            if (ed_disp_col(ed, s, len, col) != ref_disp_col(ed->opt->tabw, s, len, col)) {
                printf("FAIL line %d: disp_col %lu\n", __LINE__, (unsigned long)col);
                failures++;
                return;
            }
        total = ref_disp_col(ed->opt->tabw, s, len, len);
        for (dcol = -1; dcol <= total + 1; dcol++) {
            size_t i = ed_byte_col(ed, s, len, dcol), j = 0;
            /* the first character that doesn't end by dcol */
            while (j < len && ref_disp_col(ed->opt->tabw, s, len, utf8_next(s, len, j)) <= dcol)
                j = utf8_next(s, len, j);
            if (i != j) {
                printf("FAIL line %d: byte_col %ld: %lu, want %lu\n", __LINE__, dcol,
                       (unsigned long)i, (unsigned long)j);
                failures++;
                return;
            }
        }
    }
}

/* The rows the view uses for line ln (remembered) are the ones found
 * from its text, and so is the cursor's row. */
static void check_rows(Editor *ed, long ln, int line)
{
    size_t len, start, col;
    const char *l;
    long r, x, row;

    for (r = 0; r < 300; r++) {
        size_t got = ed_line_row_start(ed, ln, r);
        l = ed_line(ed, ln, &len);
        if (got != ed_row_start(ed, l, len, r)) {
            printf("FAIL line %d: row %ld starts at %lu, want %lu\n", line, r,
                   (unsigned long)got, (unsigned long)ed_row_start(ed, l, len, r));
            failures++;
            return;
        }
    }
    l = ed_line(ed, ln, &len);
    for (col = 0; col <= len; col += 7) {
        ed->top = ln;
        ed->top_row = 0;
        ed_set_cursor(ed, ln, col, 0);
        ed_cursor_spot(ed, &row, &x);
        l = ed_line(ed, ln, &len);
        if (row != ed_row_of(ed, l, len, ed->cx, &start)) {
            printf("FAIL line %d: cursor at %lu in row %ld\n", line,
                   (unsigned long)col, row);
            failures++;
            return;
        }
    }
}

/* A file of short lines of a few letters (many near misses), 3 MB, so
 * that it loads in steps. */
static char *slice_text(size_t *n)
{
    size_t i;
    unsigned long r = 7;
    char *t;
    *n = (size_t)3 << 20;
    t = (char *)malloc(*n + 1);
    for (i = 0; i < *n; i++) {
        r = (r * 1103515245UL + 12345UL) & 0xffffffffUL;
        t[i] = (r >> 8) % 9 == 0 ? '\n' : "abcAB"[(r >> 12) % 5];
    }
    t[*n] = 0;
    return t;
}

/* Opens text as a file that is still loading. */
static void open_loading(Editor *ed, const char *text, size_t n)
{
    char path[512];
    int fd = tmp_file(path, sizeof path, "cedit-ed");
    char err[128];
    if (write(fd, text, n) != (long)n)
        printf("write failed\n");
    close(fd);
    ed_open(ed, path, err, sizeof err);
    unlink(path);
}

static int at(const char *t, size_t i, const char *pat, size_t plen, int icase)
{
    size_t k;
    for (k = 0; k < plen; k++) {
        int a = (unsigned char)t[i + k], b = (unsigned char)pat[k];
        if (icase && a >= 'A' && a <= 'Z')
            a += 32;
        if (icase && b >= 'A' && b <= 'Z')
            b += 32;
        if (a != b)
            return 0;
    }
    return 1;
}

/* Searching a slice at a time, with slices of a few bytes, while the file
 * is loading, finds what a plain scan of the text does. */
static void test_find_slices(Editor *ed, EdOptions *opt)
{
    static const char *const pats[] = {"ab", "Ab", "bca", "a", "cAB", "abcab", "BB"};
    unsigned long r = 99;
    size_t n, i;
    char *t = slice_text(&n);
    int k;

    for (k = 0; k < 120; k++) {
        const char *pat;
        size_t plen, start, want = (size_t)-1, got;
        int back, icase, res, wrapped = 0, want_res;
        long ln;
        EdSearch s;

        r = (r * 1103515245UL + 12345UL) & 0xffffffffUL;
        pat = pats[(r >> 8) % 7];
        plen = strlen(pat);
        back = (r >> 12) & 1;
        icase = (r >> 13) & 1;
        open_loading(ed, t, n);
        CHECK(buf_loading(ed->doc->buf));
        /* start in the part loaded so far */
        ln = (long)((r >> 14) % (unsigned long)(ed_lines(ed) - 1));
        ed_set_cursor(ed, ln, (r >> 4) % 3, 0);
        start = buf_line_offset(ed->doc->buf, ed->cy) + ed->cx;
        if (!back) {
            for (i = start; i + plen <= n && want == (size_t)-1; i++)
                if (at(t, i, pat, plen, icase))
                    want = i;
            for (i = 0; i < start && want == (size_t)-1; i++)
                if (at(t, i, pat, plen, icase))
                    want = i, wrapped = 1;
        } else {
            for (i = start; i-- > 0 && want == (size_t)-1;)
                if (i + plen <= n && at(t, i, pat, plen, icase))
                    want = i;
            for (i = n; i-- > start && want == (size_t)-1;)
                if (i + plen <= n && at(t, i, pat, plen, icase))
                    want = i, wrapped = 1;
        }
        want_res = want == (size_t)-1 ? 0 : wrapped ? 2 : 1;
        str_copy(opt->find, sizeof opt->find, pat);
        opt->icase = icase;
        ed_find_begin(ed, &s, back);
        do {
            r = (r * 1103515245UL + 12345UL) & 0xffffffffUL;
            res = ed_find_step(ed, &s, 1 + (r >> 8) % 40000);
        } while (res < 0);
        got = ed->sel ? buf_line_offset(ed->doc->buf, ed->ay) + ed->ax : (size_t)-1;
        if (res != want_res || got != want) {
            printf("FAIL find \"%s\" icase %d back %d from %lu: %d at %ld, want %d at %ld\n",
                   pat, icase, back, (unsigned long)start, res, (long)got, want_res,
                   (long)want);
            failures++;
        }
    }
    free(t);
}

/* Replace All a slice at a time, while the file is loading, replaces what
 * a plain scan does, as one undo step. */
static void test_replace_slices(Editor *ed, EdOptions *opt)
{
    static const char *const pats[] = {"ab", "bA", "cab", "a"};
    static const char *const repls[] = {"", "ab", "xyzab", "B\nb"};
    unsigned long r = 5;
    size_t n, i, m;
    char *t = slice_text(&n), *want, *got;
    int k;

    want = (char *)malloc(n * 3 + 1);
    for (k = 0; k < 8; k++) {
        const char *pat = pats[k % 4], *repl = repls[(k / 2) % 4];
        size_t plen = strlen(pat), rlen = strlen(repl);
        long count = 0;
        EdReplace rp;
        int icase = k & 1;

        for (i = m = 0; i < n;) {
            if (i + plen <= n && at(t, i, pat, plen, icase)) {
                memcpy(want + m, repl, rlen);
                m += rlen;
                i += plen;
                count++;
            } else {
                want[m++] = t[i++];
            }
        }
        want[m] = 0;
        open_loading(ed, t, n);
        str_copy(opt->find, sizeof opt->find, pat);
        str_copy(opt->repl, sizeof opt->repl, repl);
        opt->icase = icase;
        ed_replace_all_begin(ed, &rp);
        do
            r = (r * 1103515245UL + 12345UL) & 0xffffffffUL;
        while (ed_replace_all_step(ed, &rp, 1 + (r >> 8) % 60000));
        got = contents(ed);
        if (rp.count != count || strcmp(got, want) != 0) {
            printf("FAIL replace \"%s\" by \"%s\" icase %d: %ld, want %ld%s\n", pat, repl,
                   icase, rp.count, count, strcmp(got, want) ? ", text differs" : "");
            failures++;
        }
        free(got);
        ed_undo(ed);
        got = contents(ed);
        CHECK(strcmp(got, t) == 0 && !ed_modified(ed));
        free(got);
    }
    free(want);
    free(t);
}

int main(void)
{
    Editor ed, v2;
    EdOptions opt;
    unsigned long t = 100000;
    char *c;
    size_t n;

    ed_options_init(&opt);
    ed_init(&ed, &opt);

    /* typing groups by words; a pause or movement splits steps */
    type(&ed, "hello world", &t);
    EXPECT(&ed, "hello world");
    ed_undo(&ed);
    EXPECT(&ed, "hello ");
    ed_undo(&ed);
    EXPECT(&ed, "");
    ed_redo(&ed);
    ed_redo(&ed);
    EXPECT(&ed, "hello world");
    CHECK(ed.cx == 11);

    /* auto-indent is off by default */
    ed_new(&ed);
    type(&ed, "\tif", &t);
    ed_newline(&ed, t);
    EXPECT(&ed, "\tif\n");

    /* newline with auto-indent, backspace joins lines */
    ed_new(&ed);
    opt.autoindent = 1;
    type(&ed, "\tif", &t);
    ed_newline(&ed, t);
    type(&ed, "x", &t);
    EXPECT(&ed, "\tif\n\tx");
    ed_move(&ed, MV_HOME, 0);
    ed_move(&ed, MV_HOME, 0);
    CHECK(ed.cx == 0);
    ed_backspace(&ed, 0, t += 2000);
    EXPECT(&ed, "\tif\tx");
    opt.autoindent = 0;

    /* CRLF files keep CRLF; the \r is invisible to the cursor */
    open_text(&ed, "ab\r\ncd\r\n");
    CHECK(ed.doc->buf->crlf);
    ed_move(&ed, MV_END, 0);
    CHECK(ed.cx == 2);
    ed_newline(&ed, t += 2000);
    EXPECT(&ed, "ab\r\n\r\ncd\r\n");
    ed_backspace(&ed, 0, t += 2000);
    EXPECT(&ed, "ab\r\ncd\r\n");
    ed_delete(&ed, 0, t += 2000);
    EXPECT(&ed, "abcd\r\n");
    ed_paste(&ed, "1\n2\r\n3", 6);
    EXPECT(&ed, "ab1\r\n2\r\n3cd\r\n");
    ed_set_cursor(&ed, 0, 0, 0);
    ed_set_cursor(&ed, 2, 1, 1);
    c = ed_copy(&ed, &n);
    CHECK(c && strcmp(c, "ab1\n2\n3") == 0);
    free(c);

    /* replace all is one undo step */
    open_text(&ed, "foo bar foo\nFOO\n");
    strcpy(opt.find, "foo");
    strcpy(opt.repl, "quux");
    opt.icase = 1;
    CHECK(ed_replace_all(&ed) == 3);
    EXPECT(&ed, "quux bar quux\nquux\n");
    ed_undo(&ed);
    EXPECT(&ed, "foo bar foo\nFOO\n");
    CHECK(!ed_modified(&ed));

    /* find wraps */
    ed_set_cursor(&ed, 1, 0, 0);
    opt.icase = 0;
    CHECK(ed_find(&ed, 0) == 2);
    CHECK(ed.cy == 0 && ed.cx == 3);
    CHECK(ed_find(&ed, 1) == 2);

    /* block indent / unindent */
    open_text(&ed, "a\nb\nc\n");
    ed_set_cursor(&ed, 0, 0, 0);
    ed_set_cursor(&ed, 2, 0, 1);
    ed_tab(&ed, 0, t += 2000);
    EXPECT(&ed, "\ta\n\tb\nc\n");
    ed_tab(&ed, 1, t += 2000);
    EXPECT(&ed, "a\nb\nc\n");

    /* indent with spaces */
    opt.spaces = 1;
    ed_set_cursor(&ed, 0, 0, 0);
    ed_set_cursor(&ed, 2, 0, 1);
    ed_tab(&ed, 0, t += 2000);
    EXPECT(&ed, "    a\n    b\nc\n");
    ed_tab(&ed, 1, t += 2000);
    EXPECT(&ed, "a\nb\nc\n");
    ed_set_cursor(&ed, 0, 1, 0);
    ed_tab(&ed, 0, t += 2000);
    EXPECT(&ed, "a   \nb\nc\n");
    CHECK(ed.cx == 4);
    opt.spaces = 0;

    /* overwrite mode */
    open_text(&ed, "abcdef");
    opt.overwrite = 1;
    type(&ed, "XY", &t);
    EXPECT(&ed, "XYcdef");
    opt.overwrite = 0;

    /* word motion */
    open_text(&ed, "foo_bar  (baz)");
    ed_move(&ed, MV_WORDRIGHT, 0);
    CHECK(ed.cx == 9);
    ed_move(&ed, MV_WORDRIGHT, 0);
    CHECK(ed.cx == 10);
    ed_move(&ed, MV_WORDLEFT, 0);
    CHECK(ed.cx == 9);
    ed_backspace(&ed, 1, t += 2000);
    EXPECT(&ed, "(baz)");

    /* typing over a selection is one step */
    open_text(&ed, "hello");
    ed_select_all(&ed);
    type(&ed, "bye", &t);
    EXPECT(&ed, "bye");
    ed_undo(&ed);
    EXPECT(&ed, "hello");

    /* utf-8 aware cursor */
    open_text(&ed, "\xc3\xa9t\xc3\xa9");
    ed_move(&ed, MV_RIGHT, 0);
    CHECK(ed.cx == 2);
    ed_move(&ed, MV_END, 0);
    ed_backspace(&ed, 0, t += 2000);
    EXPECT(&ed, "\xc3\xa9t");

    /* word wrap: rows of 10 columns, broken after a space or else anywhere */
    opt.wrap = 1;
    open_text(&ed, "aaaa bbbbbbbbbbbbbbbb\nx\n");
    ed.view_w = 11;
    ed.view_h = 3;
    {
        size_t len, start;
        const char *l = ed_line(&ed, 0, &len);
        long row, x, ln;
        CHECK(ed_wrap_width(&ed) == 10);
        CHECK(ed_row_end(&ed, l, len, 0) == 5);
        CHECK(ed_row_end(&ed, l, len, 5) == 15);
        CHECK(ed_row_end(&ed, l, len, 15) == len);
        CHECK(ed_row_of(&ed, l, len, 5, &start) == 1 && start == 5);
        CHECK(ed_row_of(&ed, l, len, len, &start) == 2 && start == 15);
        CHECK(ed_row_start(&ed, l, len, 9) == 15);

        /* Up and Down go by rows, keeping the column */
        ed_set_cursor(&ed, 0, 2, 0);
        ed_move(&ed, MV_DOWN, 0);
        CHECK(ed.cy == 0 && ed.cx == 7);
        ed_move(&ed, MV_DOWN, 0);
        CHECK(ed.cy == 0 && ed.cx == 17);
        ed_move(&ed, MV_DOWN, 0);
        CHECK(ed.cy == 1 && ed.cx == 1);
        ed_move(&ed, MV_UP, 0);
        CHECK(ed.cy == 0 && ed.cx == 17);
        /* past a wrapped row's end: before its last character */
        ed_set_cursor(&ed, 0, 13, 0);
        ed_move(&ed, MV_UP, 0);
        CHECK(ed.cx == 4);

        /* 5 rows; the last one stays at the bottom of 3 */
        ed.top = ed.top_row = 0;
        ed_scroll(&ed, 1);
        CHECK(ed.top == 0 && ed.top_row == 1);
        ed_scroll(&ed, 10);
        CHECK(ed.top == 0 && ed.top_row == 2);
        ed_scroll(&ed, -1);
        CHECK(ed.top == 0 && ed.top_row == 1);

        /* the cursor's row counts the rows above it */
        ed.top = ed.top_row = 0;
        ed_set_cursor(&ed, 2, 0, 0);
        ed_scroll_to_cursor(&ed);
        CHECK(ed.top == 0 && ed.top_row == 2);
        ed_cursor_spot(&ed, &row, &x);
        CHECK(row == 2 && x == 0);

        /* cells to positions */
        ed_pos_at(&ed, 0, 20, &ln, &start);
        CHECK(ln == 0 && start == len);
        ed.top_row = 0;
        ed_pos_at(&ed, 0, 9, &ln, &start);
        CHECK(ln == 0 && start == 4);
        ed_pos_at(&ed, 1, 1, &ln, &start);
        CHECK(ln == 0 && start == 6);
    }

    test_columns(&ed);

    /* the rows of a long line are remembered, and follow edits, the wrap
     * width, the tab width and the document shown */
    {
        char text[6000];
        unsigned long t2 = 0;
        size_t i;
        for (i = 0; i + 1 < sizeof text; i++)
            text[i] = i % 13 == 12 ? ' ' : i % 97 == 0 ? '\t' : (char)('a' + i % 26);
        text[i] = 0;
        open_text(&ed, text);
        ed.view_w = 41;
        ed.view_h = 4;
        check_rows(&ed, 0, __LINE__);
        ed_set_cursor(&ed, 0, 100, 0);
        type(&ed, "  xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", &t2);
        check_rows(&ed, 0, __LINE__);
        ed.view_w = 30;
        check_rows(&ed, 0, __LINE__);
        opt.tabw = 8;
        check_rows(&ed, 0, __LINE__);
        opt.tabw = 4;
        for (i = 0; i + 1 < sizeof text; i++)
            text[i] = i % 7 == 6 ? ' ' : 'q';
        open_text(&ed, text);
        check_rows(&ed, 0, __LINE__);
        for (i = 0; i + 1 < sizeof text; i++)   /* same size: same generation */
            text[i] = i % 5 == 4 ? ' ' : 'r';
        open_text(&ed, text);
        check_rows(&ed, 0, __LINE__);
        opt.wrap = 0;
        check_rows(&ed, 0, __LINE__);
    }
    opt.wrap = 0;
    ed.view_w = 80;
    ed.view_h = 25;

    /* a second view keeps its place while the first one edits */
    open_text(&ed, "one\ntwo\nthree\n");
    ed_set_cursor(&ed, 1, 1, 0);
    ed_init_view(&v2, &ed);
    CHECK(v2.doc == ed.doc && ed_views(&ed) == 2);
    CHECK(v2.cy == 1 && v2.cx == 1);
    ed_set_cursor(&ed, 0, 0, 0);
    type(&ed, "zero\n", &t);
    EXPECT(&v2, "zero\none\ntwo\nthree\n");
    CHECK(v2.cy == 2 && v2.cx == 1);
    /* text deleted around it puts it at the deletion */
    ed_set_cursor(&ed, 2, 0, 0);
    ed_set_cursor(&ed, 3, 0, 1);
    ed_delete(&ed, 0, t += 2000);
    CHECK(v2.cy == 2 && v2.cx == 0);
    /* its selection and top line move too */
    ed_set_cursor(&v2, 2, 0, 0);
    ed_set_cursor(&v2, 2, 5, 1);
    v2.view_h = 2;
    v2.top = 2;
    ed_set_cursor(&ed, 0, 0, 0);
    ed_newline(&ed, t += 2000);
    CHECK(v2.ay == 3 && v2.ax == 0 && v2.cy == 3 && v2.cx == 5 && v2.top == 3);
    /* undo works from either view, and moves the other one too */
    ed_undo(&v2);
    CHECK(v2.cy == 0 && ed.cy == 0 && ed.cx == 0);
    ed_undo(&ed);
    EXPECT(&ed, "zero\none\ntwo\nthree\n");
    CHECK(v2.cy == 0 && v2.cx == 0);
    /* an edit in another view starts a new undo step */
    ed_set_cursor(&ed, 0, 0, 0);
    ed_set_cursor(&v2, 0, 0, 0);
    type(&ed, "a", &t);
    type(&v2, "b", &t);
    EXPECT(&ed, "bazero\none\ntwo\nthree\n");
    ed_undo(&ed);
    EXPECT(&ed, "azero\none\ntwo\nthree\n");
    /* opening a file in one view leaves the other on the document */
    ed_new(&v2);
    CHECK(v2.doc != ed.doc && ed_views(&ed) == 1 && ed_views(&v2) == 1);
    EXPECT(&ed, "azero\none\ntwo\nthree\n");
    ed_free(&v2);

    test_find_slices(&ed, &opt);
    test_replace_slices(&ed, &opt);

    ed_free(&ed);
    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("all editor tests passed\n");
    return 0;
}
