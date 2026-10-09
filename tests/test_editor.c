/*
 * test_editor.c - editing commands: line ends, undo grouping, paste,
 * replace, indentation, word motion, several views of one document.
 */
#define _XOPEN_SOURCE 700
#include "../src/editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    char path[] = "/tmp/cedit-ed-XXXXXX";
    int fd = mkstemp(path);
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

    ed_free(&ed);
    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("all editor tests passed\n");
    return 0;
}
