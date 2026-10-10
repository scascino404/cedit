/*
 * test_buffer.c - randomized tests of the B-tree buffer and undo against a
 * flat string reference model.
 */
#define _XOPEN_SOURCE 700
#include "../src/buffer.h"
#include "../src/undo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#endif

#include "testutil.h"

static int failures;

#define CHECK(c, what, msg) do { if (!(c)) { \
    printf("FAIL line %d: %s: %s\n", __LINE__, what, msg); \
    failures++; return; } } while (0)

/* reference model */
static char *ref;
static size_t ref_len, ref_cap;

static void ref_insert(size_t off, const char *s, size_t n)
{
    if (ref_len + n > ref_cap) {
        ref_cap = (ref_len + n) * 2 + 16;
        ref = (char *)realloc(ref, ref_cap);
    }
    memmove(ref + off + n, ref + off, ref_len - off);
    memcpy(ref + off, s, n);
    ref_len += n;
}

static void ref_delete(size_t off, size_t n)
{
    memmove(ref + off, ref + off + n, ref_len - off - n);
    ref_len -= n;
}

static unsigned long rng = 12345;
static unsigned long rnd(void)
{
    rng = (rng * 1103515245UL + 12345UL) & 0xffffffffUL;
    return rng >> 8;
}

static void rand_text(char *s, size_t n, int nl_rate)
{
    size_t i;
    for (i = 0; i < n; i++)
        s[i] = (rnd() % 100 < (unsigned long)nl_rate) ? '\n' : (char)('a' + rnd() % 26);
}

static void verify(Buffer *b, const char *what)
{
    char *tmp;
    long ln, nlines = 1;
    size_t i, start;
    int r = buf_check(b);

    CHECK(r == 0, what, "buf_check = ?");
    CHECK(buf_size(b) == ref_len, what, "size ? vs ?");
    tmp = (char *)malloc(ref_len + 1);
    buf_copy(b, 0, ref_len, tmp);
    r = memcmp(tmp, ref, ref_len);
    free(tmp);
    CHECK(r == 0, what, "content differs");
    for (i = 0; i < ref_len; i++)
        nlines += ref[i] == '\n';
    CHECK(buf_lines(b) == nlines, what, "lines ? vs ?");

    /* every line, sequentially */
    start = 0;
    ln = 0;
    for (i = 0; i <= ref_len; i++) {
        if (i == ref_len || ref[i] == '\n') {
            size_t len;
            const char *p = buf_line(b, ln, &len);
            CHECK(len == i - start && memcmp(p, ref + start, len) == 0, what, "line ? differs");
            CHECK(buf_line_offset(b, ln) == start, what, "line offset ?");
            start = i + 1;
            ln++;
        }
    }
    /* random lookups both ways */
    for (i = 0; i < 50 && ref_len; i++) {
        size_t off = rnd() % (ref_len + 1), col, j, s0 = 0;
        long l2, want = 0;
        for (j = 0; j < off; j++)
            if (ref[j] == '\n') {
                want++;
                s0 = j + 1;
            }
        buf_offset_to_pos(b, off, &l2, &col);
        CHECK(l2 == want && col == off - s0, what, "offset_to_pos(?)");
    }
}

static void test_random_edits(int rounds, int maxlen, int nl_rate)
{
    Buffer *b = buf_new();
    char s[70000];
    int i;

    ref_len = 0;
    for (i = 0; i < rounds; i++) {
        size_t off = ref_len ? rnd() % (ref_len + 1) : 0;
        if (rnd() % 3 || ref_len == 0) {
            size_t n = 1 + rnd() % (rnd() % 10 == 0 ? (unsigned long)maxlen : 20);
            rand_text(s, n, nl_rate);
            buf_insert(b, off, s, n);
            ref_insert(off, s, n);
        } else {
            size_t n = 1 + rnd() % (rnd() % 10 == 0 ? (unsigned long)maxlen : 20);
            if (off >= ref_len)
                off = ref_len - 1;
            if (n > ref_len - off)
                n = ref_len - off;
            buf_delete(b, off, n);
            ref_delete(off, n);
        }
        if (i % 97 == 0 || i == rounds - 1) {
            int f = failures;
            verify(b, "random");
            if (failures != f) {
                printf("  at round %d\n", i);
                break;
            }
        }
    }
    buf_free(b);
}

static void test_undo(void)
{
    Buffer *b = buf_new();
    Undo u;
    char s[5000];
    char *orig, *final;
    size_t orig_len, final_len, cur;
    int i, steps = 0;

    undo_init(&u);
    ref_len = 0;
    rand_text(s, 3000, 5);
    buf_insert(b, 0, s, 3000);
    ref_insert(0, s, 3000);
    orig = (char *)malloc(ref_len);
    memcpy(orig, ref, ref_len);
    orig_len = ref_len;

    for (i = 0; i < 500; i++) {
        size_t off = rnd() % (ref_len + 1);
        if (rnd() % 4 == 0) {
            undo_boundary(&u);
            steps++;
        }
        if (rnd() % 2) {
            size_t n = 1 + rnd() % 40;
            rand_text(s, n, 10);
            undo_insert(&u, b, off, s, n, off);
            ref_insert(off, s, n);
        } else if (ref_len) {
            size_t n = 1 + rnd() % 40;
            if (off >= ref_len)
                off = ref_len - 1;
            if (n > ref_len - off)
                n = ref_len - off;
            undo_delete(&u, b, off, n, off);
            ref_delete(off, n);
        }
    }
    verify(b, "undo-final");
    final = (char *)malloc(ref_len);
    memcpy(final, ref, ref_len);
    final_len = ref_len;
    CHECK(undo_modified(&u), "test", "should be modified");

    while (undo_undo(&u, b, &cur))
        ;
    memcpy(ref, orig, orig_len);
    ref_len = orig_len;
    verify(b, "undo-all");
    CHECK(!undo_modified(&u), "test", "should be unmodified after undoing all");

    while (undo_redo(&u, b, &cur))
        ;
    memcpy(ref, final, final_len);
    ref_len = final_len;
    verify(b, "redo-all");

    /* undo half, new edit drops the redo tail */
    for (i = 0; i < 10; i++)
        undo_undo(&u, b, &cur);
    undo_boundary(&u);
    undo_insert(&u, b, 0, "X", 1, 0);
    CHECK(!undo_redo(&u, b, &cur), "test", "redo tail should be gone");

    free(orig);
    free(final);
    undo_free(&u);
    buf_free(b);
    (void)steps;
}

static void test_load(void)
{
    char path[512];
    int fd = tmp_file(path, sizeof path, "cedit-test");
    size_t n = 3 * 1024 * 1024, i;
    char *data = (char *)malloc(n);
    Buffer *b;
    int is_new;
    char err[256];

    rand_text(data, n, 3);
    data[100] = 'x';
    /* a few very long lines */
    for (i = 200000; i < 300000; i++)
        data[i] = 'L';
    if (write(fd, data, n) != (long)n)
        printf("write failed\n");
    close(fd);

    b = buf_new();
    CHECK(buf_open(b, path, &is_new, err, sizeof err) == 0, err, "open: ?");
    CHECK(buf_loading(b), "test", "should still be loading");

    /* edit the loaded prefix, including the phantom last line, while loading */
    ref_len = 0;
    ref_insert(0, data, b->load_pos);
    buf_insert(b, 5, "HELLO", 5);
    ref_insert(5, "HELLO", 5);
    buf_insert(b, buf_size(b), "tail", 4);
    ref_insert(ref_len, "tail", 4);
    buf_load_all(b);
    {
        size_t loaded_before = ref_len - 9;
        ref_insert(ref_len, data + loaded_before, n - loaded_before);
    }
    verify(b, "load");

    CHECK(buf_find(b, 0, (size_t)-1, "HELLO", 5, 0, 0) == 5, "test", "find");
    CHECK(buf_find(b, 0, (size_t)-1, "hello", 5, 1, 0) == 5, "test", "find icase");
    CHECK(buf_find(b, buf_size(b), 0, "HELLO", 5, 0, 1) == 5, "test", "find backward");

    CHECK(buf_save(b, path, err, sizeof err) == 0, err, "save: ?");
    buf_free(b);

    b = buf_new();
    buf_open(b, path, &is_new, err, sizeof err);
    buf_load_all(b);
    verify(b, "reload");
    buf_free(b);
    unlink(path);
    free(data);
}

static void test_crlf(void)
{
    char path[512];
    int fd = tmp_file(path, sizeof path, "cedit-test"), is_new;
    const char *txt = "one\r\ntwo\r\nthree";
    Buffer *b = buf_new();
    char err[128];
    size_t len;
    const char *p;

    if (write(fd, txt, strlen(txt)) < 0)
        printf("write failed\n");
    close(fd);
    buf_open(b, path, &is_new, err, sizeof err);
    CHECK(b->crlf, "test", "crlf not detected");
    p = buf_line(b, 1, &len);
    CHECK(len == 3 && memcmp(p, "two", 3) == 0, "test", "crlf line");
    p = buf_line(b, 2, &len);
    CHECK(len == 5, "test", "last line");
    buf_free(b);
    unlink(path);
}

#ifdef _WIN32
/* Saves text, as one insertion, to path. */
static int save_text(const char *path, const char *text, char *err, size_t errlen)
{
    Buffer *b = buf_new();
    int r;
    buf_insert(b, 0, text, strlen(text));
    r = buf_save(b, path, err, errlen);
    buf_free(b);
    return r;
}

/* Whether the file at path holds text. */
static int holds(const char *path, const char *text)
{
    char got[64];
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(got, 1, sizeof got - 1, f) : 0;
    if (f)
        fclose(f);
    got[n] = 0;
    return f && strcmp(got, text) == 0;
}

/* Saving a file that another program holds open, or reached through a
 * symbolic link; opening one whose path is too long for the "A" calls. */
static void test_windows_files(void)
{
    char path[512], link[512], err[256], dir[2048];
    WCHAR w[2048];
    int i, r, ok, is_new = -1;
    HANDLE h;
    Buffer *b;
    DWORD a;
    PSECURITY_DESCRIPTOR sd;
    FILE *f;

    /* held open by a program that lets others read and write it, but not
     * replace it */
    close(tmp_file(path, sizeof path, "cedit-held"));
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, 0, NULL);
    r = save_text(path, "saved\n", err, sizeof err);
    CloseHandle(h);
    CHECK(r == 0, err, "save of a file held open");
    CHECK(holds(path, "saved\n"), "held open", "not saved");
    unlink(path);

    /* through a symbolic link (when this account can make one) */
    close(tmp_file(path, sizeof path, "cedit-target"));
    sprintf(link, "%s.link", path);
    strcpy(dir, path);              /* a link's target takes backslashes */
    for (i = 0; dir[i]; i++)
        if (dir[i] == '/')
            dir[i] = '\\';
    if (CreateSymbolicLinkA(link, dir, SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        r = save_text(link, "saved\n", err, sizeof err);
        a = GetFileAttributesA(link);
        DeleteFileA(link);
        CHECK(r == 0, err, "save through a symbolic link");
        CHECK(a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT),
              "symbolic link", "replaced by a file");
        CHECK(holds(path, "saved\n"), "symbolic link", "target not saved");
    } else {
        printf("(no symbolic link test: this account can't make links)\n");
    }
    unlink(path);

    /* a hidden file saved in place, in a folder where no file can be added
     * (so no temporary file either), though the file can be written */
    sprintf(dir, "%s/cedit-noadd", getenv("TEMP"));
    sprintf(path, "%s/hidden.txt", dir);
    CreateDirectoryA(dir, NULL);
    f = fopen(path, "wb");
    CHECK(f != NULL, "in place", "can't make the file");
    fclose(f);
    SetFileAttributesA(path, FILE_ATTRIBUTE_HIDDEN);
    /* everyone: denied adding files to the folder, allowed all else */
    CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorA(
              "D:P(D;;0x2;;;WD)(A;OICI;FA;;;WD)", SDDL_REVISION_1, &sd, NULL),
          "in place", "no security descriptor");
    r = SetFileSecurityA(dir, DACL_SECURITY_INFORMATION, sd) ? 0 : -1;
    LocalFree(sd);
    CHECK(r == 0, "in place", "can't deny adding files");
    r = save_text(path, "saved\n", err, sizeof err);
    a = GetFileAttributesA(path);
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    ok = holds(path, "saved\n");
    DeleteFileA(path);
    RemoveDirectoryA(dir);
    CHECK(r == 0, err, "save of a hidden file in place");
    CHECK(ok, "in place", "not saved");
    CHECK(a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_HIDDEN), "in place",
          "no longer hidden");

    /* a file at a path of over MAX_PATH characters is never taken for a new
     * one: it opens where Windows allows long paths, or else fails */
    sprintf(dir, "\\\\?\\%s\\cedit-long", getenv("TEMP"));
    for (i = 0; i < 8; i++) {
        if (i)
            strcat(dir, "\\a-folder-name-long-enough-to-pass-max-path");
        MultiByteToWideChar(CP_ACP, 0, dir, -1, w, 2048);
        CreateDirectoryW(w, NULL);
    }
    strcat(dir, "\\file.txt");
    MultiByteToWideChar(CP_ACP, 0, dir, -1, w, 2048);
    h = CreateFileW(w, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CHECK(h != INVALID_HANDLE_VALUE, "long path", "can't make the file");
    CloseHandle(h);
    b = buf_new();
    r = buf_open(b, dir + 4, &is_new, err, sizeof err);
    buf_free(b);
    DeleteFileW(w);
    for (i = 0; i < 8; i++) {
        *wcsrchr(w, '\\') = 0;
        RemoveDirectoryW(w);
    }
    CHECK(r < 0 || !is_new, "long path", "an existing file opened as a new one");
}
#endif

/* The match buf_find should give, from the reference. */
static size_t ref_find(size_t from, size_t to, const char *pat, size_t plen,
                       int icase, int backward)
{
    size_t i, k, none = (size_t)-1;
    if (ref_len < plen)
        return none;
    for (k = 0; k <= ref_len - plen; k++) {
        i = backward ? ref_len - plen - k : k;
        if (backward ? i > from || i < to : i < from || i >= to)
            continue;
        if (icase ? strncasecmp(ref + i, pat, plen) == 0
                  : memcmp(ref + i, pat, plen) == 0)
            return i;
    }
    return none;
}

/* Searches in both directions, with and without case, in ranges, over
 * many leaves of short lines of a few letters (lots of near misses). */
static void test_find(void)
{
    static const char *const pats[] = {"ab", "Ab", "aB", "bca", "a", "B",
                                       "abcab", "cc", "1a"};
    Buffer *b = buf_new();
    char *t, msg[128];
    size_t n = 200000, i;
    int k;

    ref_len = 0;
    t = (char *)malloc(n);
    for (i = 0; i < n; i++)
        t[i] = rnd() % 12 == 0 ? '\n' : "abcABC1"[rnd() % 7];
    buf_insert(b, 0, t, n);
    ref_insert(0, t, n);
    free(t);
    for (k = 0; k < 3000; k++) {
        const char *pat = pats[rnd() % (sizeof pats / sizeof pats[0])];
        size_t from = rnd() % (n + 1), to = rnd() % 4 ? rnd() % (n + 1) : (size_t)-1;
        int icase = rnd() % 2, back = rnd() % 2;
        size_t got, want;
        if (back && to == (size_t)-1)
            to = 0;
        got = buf_find(b, from, to, pat, strlen(pat), icase, back);
        want = ref_find(from, to, pat, strlen(pat), icase, back);
        sprintf(msg, "\"%s\" from %lu to %ld icase %d back %d: %ld, want %ld", pat,
                (unsigned long)from, (long)to, icase, back, (long)got, (long)want);
        CHECK(got == want, "find", msg);
    }
    buf_free(b);
}

int main(void)
{
    test_random_edits(3000, 200, 10);
    test_random_edits(2000, 60000, 2);      /* big inserts/deletes: splits */
    test_random_edits(2000, 30000, 0);      /* very long lines */
    test_random_edits(3000, 5000, 60);      /* many empty lines */
    test_undo();
    test_load();
    test_crlf();
#ifdef _WIN32
    test_windows_files();
#endif
    test_find();
    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("all buffer tests passed\n");
    return 0;
}
