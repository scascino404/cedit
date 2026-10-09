/*
 * test_syntax.c - tests of the highlighting lexer, language detection, and
 * the state cache (against lexing the whole buffer from the top).
 */
#define _XOPEN_SOURCE 700
#include "../src/syntax.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

/* One letter per class: . normal, k keyword, t type, c comment, s string,
 * n number, p preprocessor. */
static const char letters[] = ".ktcsnp";

static const Syntax *lang(const char *path)
{
    const Syntax *syn = syn_detect(path, NULL, 0);
    if (!syn) {
        printf("FAIL: no language for %s\n", path);
        exit(1);
    }
    return syn;
}

/* Lexes the lines of text (separated by '\n') from state 0 and compares
 * their classes with want, written the same way. */
static void check(const char *path, const char *text, const char *want)
{
    const Syntax *syn = lang(path);
    char got[1024];
    unsigned char cls[1024];
    const char *s = text;
    unsigned st = 0;
    size_t n = 0, i;

    for (;;) {
        const char *e = strchr(s, '\n');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        st = syn_lex(syn, st, s, len, cls);
        for (i = 0; i < len; i++)
            got[n++] = letters[cls[i]];
        if (!e)
            break;
        got[n++] = '\n';
        s = e + 1;
    }
    got[n] = 0;
    if (strcmp(got, want) != 0) {
        printf("FAIL %s:\n%s\n  got:\n%s\n  want:\n%s\n", path, text, got, want);
        failures++;
    }
}

static void test_lexer(void)
{
    /* C */
    check("a.c", "int x = 10; /* c */ \"s\\\"\" 'c' // end",
                 "ttt.....nn..ccccccc.sssss.sss.cccccc");
    check("a.c", "a /* one\ntwo */ b",
                 "..cccccc\ncccccc..");
    check("a.c", "  #  include <x.h>\n\"a\\\nb\" c",
                 "..pppppppppp......\nsss\nss..");
    check("a.c", "x = 1'000 + 0x1Fu + 1.5e-3;",
                 "....nnnnn...nnnnn...nnnnnn.");
    check("a.c", "\"/* not */\" /* \"not\" */",
                 "sssssssssss.ccccccccccc");
    check("a.c", "for (;;) return sizeof(int);",
                 "kkk......kkkkkk.kkkkkk.ttt..");
    check("a.c", "\"unterminated\nok",
                 "sssssssssssss\n..");

    /* C++ raw strings */
    check("a.cpp", "R\"(a \" b\nc)\" class",
                   "ssssssss\nsss.kkkkk");

    /* HolyC: nested comments, multi-line strings, multi-char constants */
    check("a.HC", "U0 F(I64 x) { /* a /* b */ c */ return 'AB'; }",
                  "tt...ttt......ccccccccccccccccc.kkkkkk.ssss...");
    check("a.HC", "#ifaot\n\"a\nb\" switch start: end: F64 y=0b101;",
                  "pppppp\nss\nss.kkkkkk.kkkkk..kkk..ttt...nnnnn.");

    /* Python */
    check("a.py", "def f(): # c\n  \"\"\"doc\n  ' \"\"\" x",
                  "kkk......ccc\n..ssssss\nsssssss..");
    check("a.py", "s = f'{x}' + b\"y\"",
                  ".....sssss....sss");

    /* Shell: $# is not a comment, an escaped quote opens nothing */
    check("a.sh", "echo $# \\\" x # c\nif true; then",
                  ".............ccc\nkk.......kkkk");
    check("a.sh", "echo 'a\nb' \"$x\"",
                  ".....ss\nss.ssss");

    /* Makefile */
    check("Makefile", "-include x.mk # c\nifeq ($(A),b)",
                      "kkkkkkkk......ccc\nkkkk.........");

    /* JavaScript and JSON */
    check("a.js", "const s = `a\n${b}` // c",
                  "kkkkk.....ss\nsssss.cccc");
    check("a.json", "{\"a\": [1, true, null]}",
                    ".sss...n..kkkk..kkkk..");

    /* Go */
    check("a.go", "func f() string { return `x\ny` }",
                  "kkkk.....tttttt...kkkkkk.ss\nss..");

    /* Rust: nested comments, raw strings, chars and lifetimes */
    check("a.rs", "/* a /* b */ c */ x",
                  "ccccccccccccccccc..");
    check("a.rs", "r##\"a \"# b\n\"## fn",
                  "ssssssssss\nsss.kk");
    check("a.rs", "fn f<'a>(x: &'a str) -> char { 'x' }",
                  "kk..............ttt.....tttt...sss..");
    check("a.rs", "b'\\'' '\\u{1F600}'",
                  ".ssss.sssssssssss");
    check("a.rs", "r#type r\"x\"",          /* a raw identifier */
                  "..kkkk.ssss");

    /* Lua long brackets need the same level to close */
    check("a.lua", "--[==[ ]] ]=]\n]==] x -- c",
                   "ccccccccccccc\ncccc...cccc");
    check("a.lua", "a[b[1]] = [[s]] local",
                   "....n.....sssss.kkkkk");

    /* Markdown */
    check("a.md", "# Title\ntext `code` x\n```c\nint x;\n```\n  # no",
                  "kkkkkkk\n.....ssssss..\nssss\nssssss\nsss\n..kkkk");
}

static void expect_lang(const char *path, const char *line1, const char *want)
{
    const Syntax *syn = syn_detect(path, line1, line1 ? strlen(line1) : 0);
    const char *got = syn ? syn->name : "none";
    if (strcmp(got, want) != 0) {
        printf("FAIL detect %s / %s: got %s, want %s\n", path ? path : "(none)",
               line1 ? line1 : "(none)", got, want);
        failures++;
    }
}

static void test_detect(void)
{
    expect_lang("/x/y/main.c", NULL, "C");
    expect_lang("a.hpp", NULL, "C++");
    expect_lang("src/Makefile", NULL, "Makefile");
    expect_lang("Kernel/KMain.HC", NULL, "HolyC");
    expect_lang("KernelA.HH", NULL, "HolyC");
    expect_lang("a.hh", NULL, "C++");
    expect_lang("/home/me/.bashrc", NULL, "Shell");
    expect_lang("notes.md", NULL, "Markdown");
    expect_lang("a.txt", NULL, "none");
    expect_lang("Makefile.bak", NULL, "none");
    expect_lang(NULL, "#!/bin/sh", "Shell");
    expect_lang("run", "#!/usr/bin/env python3.12", "Python");
    expect_lang("run", "#!/usr/bin/env -S lua -e x", "Lua");
    expect_lang("run", "#!/bin/bash -e", "Shell");
    expect_lang("run", "#!/usr/bin/shellcheck", "none");
    expect_lang("run", "# not a shebang", "none");
}

/* ---- the state cache ---------------------------------------------- */

static unsigned long rng = 4242;
static unsigned long rnd(void)
{
    rng = (rng * 1103515245UL + 12345UL) & 0xffffffffUL;
    return rng >> 8;
}

static const char *const pieces[] = {
    "int ", "x ", "/*", "*/", "\"", "'", "// ", "\n", "\n", "\n", "\\",
    "# ", "return ", "12 ", "\n"
};

static char *rand_text(size_t n, size_t *len)
{
    char *s = (char *)malloc(n * 8 + 1);
    size_t i;
    *len = 0;
    for (i = 0; i < n; i++) {
        const char *p = pieces[rnd() % (sizeof pieces / sizeof pieces[0])];
        memcpy(s + *len, p, strlen(p));
        *len += strlen(p);
    }
    s[*len] = 0;
    return s;
}

/* Compares hl_line for line ln with lexing everything from the top. */
static void compare(Highlight *h, Buffer *b, long ln, const char *what)
{
    unsigned char want[4096];
    const unsigned char *got;
    const char *s;
    unsigned st = 0;
    size_t len;
    long k;

    for (k = 0; k < ln; k++) {
        s = buf_line(b, k, &len);
        st = syn_lex(h->syn, st, s, len, NULL);
    }
    s = buf_line(b, ln, &len);
    syn_lex(h->syn, st, s, len, want);
    got = hl_line(h, ln);
    if (len > sizeof want || memcmp(got, want, len) != 0) {
        printf("FAIL cache (%s): line %ld differs\n", what, ln);
        failures++;
    }
}

static void test_cache(void)
{
    Buffer *b = buf_new();
    Highlight h;
    size_t len;
    char *text = rand_text(20000, &len);
    int round;

    hl_init(&h);
    buf_insert(b, 0, text, len);
    hl_set(&h, b, lang("a.c"));
    for (round = 0; round < 300; round++) {
        long n = buf_lines(b), ln = (long)(rnd() % (unsigned long)n);
        size_t off = rnd() % (buf_size(b) + 1);
        compare(&h, b, ln, "query");
        if (ln + 1 < n)
            compare(&h, b, ln + 1, "next line");
        if (rnd() % 2) {
            char *t = rand_text(1 + rnd() % 4, &len);
            buf_insert(b, off, t, len);
            free(t);
        } else {
            buf_delete(b, off, rnd() % 40);
        }
    }
    free(text);
    hl_free(&h);
    buf_free(b);
}

/* A comment opened at the top of a big file: far lines are lexed from a
 * guess until hl_fill reaches them. */
static void test_far(void)
{
    Buffer *b = buf_new();
    Highlight h;
    const unsigned char *cls;
    long last;
    int steps = 0;

    hl_init(&h);
    buf_insert(b, 0, "/*\n", 3);
    while (buf_size(b) < ((size_t)6 << 20))
        buf_insert(b, buf_size(b), "x = 1; y = 2; z = 3;\n", 21);
    hl_set(&h, b, lang("a.c"));
    last = buf_lines(b) - 2;
    cls = hl_line(&h, last);
    if (cls[0] != HL_NORMAL || !hl_behind(&h, last)) {
        printf("FAIL far: expected a guessed state\n");
        failures++;
    }
    while (hl_behind(&h, last) && steps++ < 1000)
        hl_fill(&h, last, (size_t)1 << 20);
    cls = hl_line(&h, last);
    if (cls[0] != HL_COMMENT) {
        printf("FAIL far: still not inside the comment after filling\n");
        failures++;
    }
    /* closing the comment at the top makes the far line normal again */
    buf_insert(b, 2, "*/", 2);
    while (hl_behind(&h, last) && steps++ < 2000)
        hl_fill(&h, last, (size_t)1 << 20);
    cls = hl_line(&h, last);
    if (cls[0] != HL_NORMAL) {
        printf("FAIL far: comment closed at the top but line still in it\n");
        failures++;
    }
    hl_free(&h);
    buf_free(b);
}

/* For every language, lexing for the state alone (which skips some lines)
 * ends lines in the same states as lexing their classes. */
static void test_states(void)
{
    static const char *const parts[] = {
        "a", "a", "a", "a", " ", " ", "\t", "1", "1'0", "x1", "_", "$", "-", "\"",
        "'", "`", "/*", "*/", "//", "#", "--", "[[", "]]", "[==[", "]==]",
        "--[=[", "r#\"", "\"#", "R\"(", ")\"", "\\", "<!--", "-->", "```",
        "~~~", "\"\"\"", "'''", "(", ";", "{"};
    char line[200];
    unsigned char cls[200];
    unsigned long r = 7;
    int l, k;

    for (l = 0; l < syn_nlangs; l++) {
        const Syntax *syn = &syn_langs[l];
        unsigned st = 0;
        syn_prepare(&syn_langs[l]);
        for (k = 0; k < 20000; k++) {
            size_t len = 0;
            unsigned a, b;
            int nparts = (int)((r >> 8) % 12), p;
            for (p = 0; p < nparts; p++) {
                const char *t;
                r = r * 1103515245UL + 12345UL;
                /* mostly plain text, so that many lines have no opens */
                t = (r >> 20) % 4 ? parts[(r >> 8) % 7] : parts[(r >> 8) % 40];
                memcpy(line + len, t, strlen(t));
                len += strlen(t);
            }
            r = r * 1103515245UL + 12345UL;
            a = syn_lex(syn, st, line, len, NULL);
            b = syn_lex(syn, st, line, len, cls);
            if (a != b) {
                printf("FAIL %s: \"%.*s\" from state %u: %u, want %u\n", syn->name,
                       (int)len, line, st, a, b);
                failures++;
                break;
            }
            st = b;
        }
    }
}

/* The class of word s[0..n): of the first list that has it. */
static int ref_word_class(const Syntax *syn, const char *s, size_t n)
{
    int k;
    for (k = 0; k < syn->nrules; k++) {
        const char *const *w;
        if (syn->rules[k].kind != R_WORDS)
            continue;
        for (w = syn->rules[k].words; *w; w++)
            if (strlen(*w) == n && memcmp(*w, s, n) == 0)
                return syn->rules[k].cls;
    }
    return HL_NORMAL;
}

/* Every word of every language's lists, and words a letter off, get the
 * class of the lists (which word_class looks up in a hash). */
static void test_words(void)
{
    int l, k;
    for (l = 0; l < syn_nlangs; l++) {
        Syntax *syn = &syn_langs[l];
        syn_prepare(syn);
        for (k = 0; k < syn->nrules; k++) {
            const char *const *w;
            if (syn->rules[k].kind != R_WORDS)
                continue;
            for (w = syn->rules[k].words; *w; w++) {
                char word[64];
                unsigned char cls[64];
                size_t n = strlen(*w), v;
                for (v = 0; v < 4; v++) {
                    size_t m = n;
                    strcpy(word, *w);
                    if (v == 1)
                        word[m++] = 'x';
                    else if (v == 2)
                        m--;
                    else if (v == 3)
                        word[0] = word[0] == 'q' ? 'z' : 'q';
                    if (m == 0 || !syn->wordc[(unsigned char)word[0]] ||
                        (word[0] >= '0' && word[0] <= '9'))
                        continue;
                    syn_lex(syn, 0, word, m, cls);
                    if (cls[0] != ref_word_class(syn, word, m)) {
                        printf("FAIL %s: \"%.*s\" is %d, want %d\n", syn->name, (int)m,
                               word, cls[0], ref_word_class(syn, word, m));
                        failures++;
                    }
                }
            }
        }
    }
}

int main(void)
{
    test_lexer();
    test_detect();
    test_cache();
    test_far();
    test_words();
    test_states();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("test_syntax: all passed\n");
    return 0;
}
