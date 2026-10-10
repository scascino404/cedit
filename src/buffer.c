/*
 * buffer.c - B-tree text storage, mmap loading and atomic saving.
 */
#define _XOPEN_SOURCE 700
#include "buffer.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#define W_OK 2
#define fsync _commit
#else
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif

#define FANOUT      64
#define LOAD_CHUNK  8192        /* loaded leaves borrow ~this many bytes */
#define LEAF_TARGET 8192        /* rebuilt leaves are cut near this size */
#define LEAF_MAX    16384       /* an owned leaf is split above this size */
#define LEAF_MIN    1024        /* a leaf below this is merged with its neighbor */
#define FIRST_LOAD  (1024 * 1024)
/* Windows: files up to this size are read rather than mapped. No program
 * can truncate a mapped file there, and most save by truncating: they
 * couldn't save a file while cedit has it open. */
#define READ_MAX    ((size_t)16 * 1024 * 1024)

struct Node {
    Node *parent;
    int is_leaf;
    int nkids;          /* inner nodes only */
    long newlines;      /* aggregated over the subtree */
    size_t bytes;       /* aggregated over the subtree */
    char *text;         /* leaf only */
    size_t cap;         /* leaf only: 0 => text is borrowed (file mapping) */
    Node **kid;         /* inner only: FANOUT slots */
};

/* Counts '\n' bytes. The inner loop over 128-byte blocks with a byte-wide
 * accumulator is shaped so that both gcc and clang vectorize it (about
 * 20 ms per GB, 5-12x faster than the naive loop). */
static long count_nl(const char *s, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    long c = 0;
    size_t i = 0;

    for (; n - i >= 128; i += 128) {
        unsigned char k = 0;
        int j;
        for (j = 0; j < 128; j++)
            k += (unsigned char)(p[i + j] == '\n');
        c += k;
    }
    for (; i < n; i++)
        c += p[i] == '\n';
    return c;
}

/* ------------------------------------------------------------------ */
/* Tree primitives                                                     */
/* ------------------------------------------------------------------ */

static Node *leaf_new(char *text, size_t len, size_t cap, long nl)
{
    Node *n = (Node *)xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->is_leaf = 1;
    n->text = text;
    n->bytes = len;
    n->cap = cap;
    n->newlines = nl;
    return n;
}

/* Gives an empty leaf an owned copy of s. */
static void leaf_fill(Node *l, const char *s, size_t len)
{
    l->cap = len + len / 4 + 64;
    l->text = (char *)xmalloc(l->cap);
    memcpy(l->text, s, len);
    l->bytes = len;
    l->newlines = count_nl(s, len);
}

static Node *leaf_copy(const char *s, size_t len)
{
    Node *n = leaf_new(NULL, 0, 0, 0);
    leaf_fill(n, s, len);
    return n;
}

static Node *inner_new(void)
{
    Node *n = (Node *)xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->kid = (Node **)xmalloc(FANOUT * sizeof(Node *));
    return n;
}

static void node_free(Node *n)
{
    if (n->is_leaf) {
        if (n->cap)
            free(n->text);
    } else {
        int i;
        for (i = 0; i < n->nkids; i++)
            node_free(n->kid[i]);
        free(n->kid);
    }
    free(n);
}

static int kid_index(const Node *p, const Node *c)
{
    int i;
    for (i = 0; i < p->nkids; i++)
        if (p->kid[i] == c)
            return i;
    return -1;
}

/* Adds a delta to n and all its ancestors. */
static void add_up(Node *n, long dnl, long dbytes)
{
    for (; n; n = n->parent) {
        n->newlines += dnl;
        n->bytes += (size_t)dbytes;
    }
}

static void recount(Node *n)
{
    int i;
    n->newlines = 0;
    n->bytes = 0;
    for (i = 0; i < n->nkids; i++) {
        n->newlines += n->kid[i]->newlines;
        n->bytes += n->kid[i]->bytes;
    }
}

/* Inserts c as kid idx of p; c's counts are added to p and its ancestors. */
static void insert_kid(Buffer *b, Node *p, int idx, Node *c)
{
    Node *q, *dst;
    long old_nl;
    size_t old_bytes;
    int mid, i;

    if (p->nkids < FANOUT) {
        memmove(&p->kid[idx + 1], &p->kid[idx],
                (size_t)(p->nkids - idx) * sizeof(Node *));
        p->kid[idx] = c;
        p->nkids++;
        c->parent = p;
        add_up(p, c->newlines, (long)c->bytes);
        return;
    }

    /* Full: split. Appending at the right edge splits n/1 so that bulk
     * loading produces packed nodes; anything else splits in half. */
    old_nl = p->newlines;
    old_bytes = p->bytes;
    q = inner_new();
    mid = idx == FANOUT ? FANOUT : FANOUT / 2;
    for (i = mid; i < FANOUT; i++) {
        q->kid[i - mid] = p->kid[i];
        p->kid[i]->parent = q;
    }
    q->nkids = FANOUT - mid;
    p->nkids = mid;
    if (idx <= mid && idx != FANOUT) {
        dst = p;
    } else {
        dst = q;
        idx -= mid;
    }
    memmove(&dst->kid[idx + 1], &dst->kid[idx],
            (size_t)(dst->nkids - idx) * sizeof(Node *));
    dst->kid[idx] = c;
    dst->nkids++;
    c->parent = dst;
    recount(p);
    recount(q);

    if (p->parent) {
        add_up(p->parent, p->newlines - old_nl,
               (long)(p->bytes - old_bytes));
        insert_kid(b, p->parent, kid_index(p->parent, p) + 1, q);
    } else {
        Node *r = inner_new();
        r->kid[0] = p;
        r->kid[1] = q;
        r->nkids = 2;
        p->parent = r;
        q->parent = r;
        recount(r);
        b->root = r;
    }
}

static void insert_after(Buffer *b, Node *n, Node *c)
{
    if (!n->parent) {
        Node *r = inner_new();
        r->kid[0] = n;
        r->nkids = 1;
        n->parent = r;
        recount(r);
        b->root = r;
    }
    insert_kid(b, n->parent, kid_index(n->parent, n) + 1, c);
}

/* Unlinks n from the tree (n itself is not freed). n must not be root. */
static void unlink_node(Buffer *b, Node *n)
{
    Node *p = n->parent;
    int idx = kid_index(p, n);

    add_up(p, -n->newlines, -(long)n->bytes);
    memmove(&p->kid[idx], &p->kid[idx + 1],
            (size_t)(p->nkids - idx - 1) * sizeof(Node *));
    p->nkids--;
    n->parent = NULL;
    if (p->nkids == 0 && p->parent) {
        unlink_node(b, p);
        node_free(p);
    }
}

static void collapse_root(Buffer *b)
{
    while (!b->root->is_leaf && b->root->nkids == 1) {
        Node *old = b->root;
        b->root = old->kid[0];
        b->root->parent = NULL;
        old->nkids = 0;
        node_free(old);
    }
}

static void leaf_set_empty(Node *l)
{
    if (l->cap)
        free(l->text);
    l->text = NULL;
    l->cap = 0;
    l->bytes = 0;
    l->newlines = 0;
}

/* Removes a leaf; the last remaining leaf is emptied instead. */
static void remove_leaf(Buffer *b, Node *l)
{
    if (b->root == l) {
        leaf_set_empty(l);
        return;
    }
    unlink_node(b, l);
    node_free(l);
    collapse_root(b);
}

static Node *first_leaf(Node *n)
{
    while (!n->is_leaf)
        n = n->kid[0];
    return n;
}

static Node *last_leaf(Node *n)
{
    while (!n->is_leaf)
        n = n->kid[n->nkids - 1];
    return n;
}

static Node *leaf_next(Node *n)
{
    while (n->parent) {
        Node *p = n->parent;
        int i = kid_index(p, n);
        if (i + 1 < p->nkids)
            return first_leaf(p->kid[i + 1]);
        n = p;
    }
    return NULL;
}

static Node *leaf_prev(Node *n)
{
    while (n->parent) {
        Node *p = n->parent;
        int i = kid_index(p, n);
        if (i > 0)
            return last_leaf(p->kid[i - 1]);
        n = p;
    }
    return NULL;
}

/* Leaf where line ln starts or, when ln < 0, the leaf containing byte off
 * (the last leaf for off == size). line0 and off0 receive the first line and
 * the byte offset of that leaf. */
static Node *find_leaf(Buffer *b, long ln, size_t off, long *line0,
                       size_t *off0)
{
    Node *n = b->root;
    long l = 0;
    size_t o = 0;

    while (!n->is_leaf) {
        int i;
        for (i = 0; i < n->nkids - 1; i++) {
            Node *k = n->kid[i];
            if (ln >= 0 ? ln < l + k->newlines : off < o + k->bytes)
                break;
            l += k->newlines;
            o += k->bytes;
        }
        n = n->kid[i];
    }
    *line0 = l;
    *off0 = o;
    return n;
}

/* Makes a leaf's text owned and able to hold extra more bytes. A borrowed
 * leaf gets a little room for typing (edits scattered over a big file
 * each own a leaf); an owned one that fills up grows by half. */
static void leaf_own(Node *l, size_t extra)
{
    if (l->cap == 0 || l->bytes + extra > l->cap) {
        size_t cap = l->bytes + extra;
        char *t;
        cap += l->cap ? cap / 2 + 64 : 256;
        t = (char *)xmalloc(cap);
        if (l->bytes)
            memcpy(t, l->text, l->bytes);
        if (l->cap)
            free(l->text);
        l->text = t;
        l->cap = cap;
    }
}

/* End of the next piece of d when cutting it into leaves. */
static size_t chunk_end(const char *d, size_t pos, size_t len)
{
    size_t t, i;
    const char *p;

    if (len - pos <= LEAF_MAX)
        return len;
    t = pos + LEAF_TARGET;
    for (i = t; i > pos; i--)
        if (d[i - 1] == '\n')
            return i;
    p = (const char *)memchr(d + t, '\n', len - t);
    return p ? (size_t)(p - d) + 1 : len;
}

/* Replaces the content of leaf with d, splitting into several leaves when
 * large. */
static void replace_leaf(Buffer *b, Node *leaf, const char *d, size_t len)
{
    size_t pos, cut;
    Node *prev = leaf;
    long old_nl = leaf->newlines;
    size_t old_bytes = leaf->bytes;

    cut = chunk_end(d, 0, len);
    leaf_set_empty(leaf);
    leaf_fill(leaf, d, cut);
    add_up(leaf->parent, leaf->newlines - old_nl,
           (long)(leaf->bytes - old_bytes));
    pos = cut;
    while (pos < len) {
        Node *n;
        cut = chunk_end(d, pos, len);
        n = leaf_copy(d + pos, cut - pos);
        insert_after(b, prev, n);
        prev = n;
        pos = cut;
    }
}

/* Restores the leaf invariants around l after an edit: no empty leaves,
 * every non-last leaf ends in '\n', and tiny leaves get merged. */
static void fix_leaf(Buffer *b, Node *l)
{
    Node *next;

    if (l->bytes == 0) {
        remove_leaf(b, l);
        return;
    }
    next = leaf_next(l);
    if (!next)
        return;
    if (l->text[l->bytes - 1] != '\n' ||
        (l->bytes < LEAF_MIN && l->bytes + next->bytes <= LEAF_TARGET)) {
        size_t len = l->bytes + next->bytes;
        char *d = (char *)xmalloc(len);
        memcpy(d, l->text, l->bytes);
        memcpy(d + l->bytes, next->text, next->bytes);
        unlink_node(b, next);
        node_free(next);
        collapse_root(b);
        replace_leaf(b, l, d, len);
        free(d);
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

Buffer *buf_new(void)
{
    Buffer *b = (Buffer *)xmalloc(sizeof *b);
    memset(b, 0, sizeof *b);
    b->root = leaf_new(NULL, 0, 0, 0);
    b->gen = 1;
    b->dirty = -1;
    return b;
}

/* Frees the file mapping (or heap copy), once no leaf borrows from it. */
static void drop_map(Buffer *b)
{
    if (b->map) {
        if (b->map_is_mmap)
#ifdef _WIN32
            UnmapViewOfFile(b->map);
#else
            munmap(b->map, b->map_len);
#endif
        else
            free(b->map);
    }
    b->map = NULL;
    b->map_len = 0;
    b->map_is_mmap = 0;
    b->load_pos = 0;
}

void buf_free(Buffer *b)
{
    if (!b)
        return;
    node_free(b->root);
    drop_map(b);
    free(b);
}

static void append_loaded(Buffer *b, size_t start, size_t len)
{
    Node *last = last_leaf(b->root);
    long nl;

    if (last->bytes == 0) {
        leaf_set_empty(last);
        nl = count_nl(b->map + start, len);
        last->text = b->map + start;
        last->bytes = len;
        last->newlines = nl;
        add_up(last->parent, nl, (long)len);
        return;
    }
    if (last->text[last->bytes - 1] != '\n') {
        /* The user typed on the not-yet-loaded last line: the first line of
         * this chunk continues it. */
        const char *p = (const char *)memchr(b->map + start, '\n', len);
        size_t k = p ? (size_t)(p - (b->map + start)) + 1 : len;
        leaf_own(last, k);
        memcpy(last->text + last->bytes, b->map + start, k);
        last->bytes += k;
        last->newlines += p ? 1 : 0;
        add_up(last->parent, p ? 1 : 0, (long)k);
        start += k;
        len -= k;
        if (!len)
            return;
    }
    insert_after(b, last,
                 leaf_new(b->map + start, len, 0, count_nl(b->map + start, len)));
}

int buf_loading(const Buffer *b)
{
    return b->load_pos < b->map_len;
}

int buf_load_step(Buffer *b, size_t budget)
{
    size_t done = 0;

    while (b->load_pos < b->map_len && done < budget) {
        size_t start = b->load_pos, end;
        if (b->map_len - start <= LOAD_CHUNK) {
            end = b->map_len;
        } else {
            size_t from = start + LOAD_CHUNK - 1;
            const char *p = (const char *)memchr(b->map + from, '\n',
                                                 b->map_len - from);
            end = p ? (size_t)(p - b->map) + 1 : b->map_len;
        }
        append_loaded(b, start, end - start);
        done += end - start;
        b->load_pos = end;
    }
    if (done)
        b->gen++;
    return buf_loading(b);
}

void buf_load_all(Buffer *b)
{
    while (buf_load_step(b, (size_t)1 << 30))
        ;
}

double buf_load_progress(const Buffer *b)
{
    return b->map_len ? (double)b->load_pos / (double)b->map_len : 1.0;
}

#ifdef _WIN32
/* The message for Windows error code e into err, without the final period,
 * as strerror gives it. */
static void win_error(char *err, size_t errlen, DWORD e)
{
    size_t n;
    char num[32];

    if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                        NULL, e, 0, err, (DWORD)errlen, NULL)) {
        sprintf(num, "Error %lu", (unsigned long)e);
        str_copy(err, errlen, num);
        return;
    }
    n = strlen(err);
    while (n && (err[n - 1] == '\n' || err[n - 1] == '\r' || err[n - 1] == ' ' ||
                 err[n - 1] == '.'))
        err[--n] = 0;
}

/* Maps the file at path, or reads it, into b->map. */
static int map_file(Buffer *b, const char *path, int *is_new, char *err,
                    size_t errlen)
{
    DWORD attr = GetFileAttributesA(path), e;
    HANDLE h;
    LARGE_INTEGER size;
    int disk;

    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        str_copy(err, errlen, "Is a directory");
        return -1;
    }
    /* others may still change, rename or delete it */
    h = CreateFileA(path, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) {
            *is_new = 1;
            return 0;
        }
        win_error(err, errlen, e);
        return -1;
    }
    size.QuadPart = 0;
    disk = GetFileType(h) == FILE_TYPE_DISK && GetFileSizeEx(h, &size);
    if (disk && (size_t)size.QuadPart > READ_MAX) {
        /* the mapping lasts after both handles are closed */
        HANDLE m = CreateFileMappingA(h, NULL, PAGE_READONLY, 0, 0, NULL);
        void *v = m ? MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0) : NULL;
        if (m)
            CloseHandle(m);
        if (v) {
            b->map = (char *)v;
            b->map_len = (size_t)size.QuadPart;
            b->map_is_mmap = 1;
        }
    }
    if (!b->map && !(disk && size.QuadPart == 0)) {
        /* small, or not mappable (a pipe, ...): read it into one heap block */
        size_t cap = disk ? (size_t)size.QuadPart + 1 : 65536, len = 0;
        char *d = (char *)xmalloc(cap);
        for (;;) {
            DWORD r, want;
            if (len == cap)
                d = (char *)xrealloc(d, cap *= 2);
            want = cap - len < (1u << 30) ? (DWORD)(cap - len) : 1u << 30;
            if (!ReadFile(h, d + len, want, &r, NULL)) {
                e = GetLastError();
                if (e == ERROR_BROKEN_PIPE)     /* the end of a pipe */
                    break;
                win_error(err, errlen, e);
                free(d);
                CloseHandle(h);
                return -1;
            }
            if (r == 0)
                break;
            len += r;
        }
        if (len) {
            b->map = d;
            b->map_len = len;
        } else {
            free(d);
        }
    }
    CloseHandle(h);
    return 0;
}
#else
/* Maps the file at path, or reads it, into b->map. */
static int map_file(Buffer *b, const char *path, int *is_new, char *err,
                    size_t errlen)
{
    int fd;
    struct stat st;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            *is_new = 1;
            return 0;
        }
        str_copy(err, errlen, strerror(errno));
        return -1;
    }
    if (fstat(fd, &st) < 0) {
        str_copy(err, errlen, strerror(errno));
        close(fd);
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        str_copy(err, errlen, "Is a directory");
        close(fd);
        return -1;
    }
    if (S_ISREG(st.st_mode) && st.st_size > 0) {
        void *m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (m != MAP_FAILED) {
#ifdef POSIX_MADV_SEQUENTIAL
            posix_madvise(m, (size_t)st.st_size, POSIX_MADV_SEQUENTIAL);
#endif
            b->map = (char *)m;
            b->map_len = (size_t)st.st_size;
            b->map_is_mmap = 1;
        }
    }
    if (!b->map && !(S_ISREG(st.st_mode) && st.st_size == 0)) {
        /* Not mappable (pipe, /proc, ...): read it into one heap block. */
        size_t cap = 65536, len = 0;
        char *d = (char *)xmalloc(cap);
        for (;;) {
            ssize_t r;
            if (len == cap)
                d = (char *)xrealloc(d, cap *= 2);
            r = read(fd, d + len, cap - len);
            if (r < 0 && errno == EINTR)
                continue;
            if (r < 0) {
                str_copy(err, errlen, strerror(errno));
                free(d);
                close(fd);
                return -1;
            }
            if (r == 0)
                break;
            len += (size_t)r;
        }
        if (len) {
            b->map = d;
            b->map_len = len;
        } else {
            free(d);
        }
    }
    close(fd);
    return 0;
}
#endif

int buf_open(Buffer *b, const char *path, int *is_new, char *err,
             size_t errlen)
{
    const char *nl;

    *is_new = 0;
    if (map_file(b, path, is_new, err, errlen) < 0)
        return -1;
    if (b->map) {
        nl = (const char *)memchr(b->map, '\n', b->map_len);
        b->crlf = nl && nl > b->map && nl[-1] == '\r';
        buf_load_step(b, FIRST_LOAD);
    }
    return 0;
}

long buf_lines(const Buffer *b)
{
    return b->root->newlines + 1;
}

size_t buf_size(const Buffer *b)
{
    return b->root->bytes;
}

/* Raw line lookup: pointer, length up to (excluding) '\n', whether a '\n'
 * follows, and the line's byte offset. */
static const char *line_raw(Buffer *b, long ln, size_t *len, int *has_nl,
                            size_t *off)
{
    Node *leaf;
    long l0, k;
    size_t o0, pos;
    const char *t, *p;

    while (buf_loading(b) && ln >= b->root->newlines)
        buf_load_step(b, FIRST_LOAD);
    if (ln < 0)
        ln = 0;
    if (ln > b->root->newlines)
        ln = b->root->newlines;

    if (b->c_gen == b->gen && b->c_leaf &&
        ln >= b->c_line0 &&
        (ln < b->c_line0 + b->c_leaf->newlines ||
         (ln == b->c_line0 + b->c_leaf->newlines &&
          ln == b->root->newlines))) {
        leaf = b->c_leaf;
        l0 = b->c_line0;
        o0 = b->c_off0;
    } else {
        leaf = find_leaf(b, ln, 0, &l0, &o0);
        b->c_gen = b->gen;
        b->c_leaf = leaf;
        b->c_line0 = l0;
        b->c_off0 = o0;
        b->c_ln = l0;
        b->c_pos = 0;
    }
    t = leaf->text;
    if (ln >= b->c_ln) {
        k = b->c_ln;
        pos = b->c_pos;
    } else {
        k = l0;
        pos = 0;
    }
    while (k < ln) {
        p = (const char *)memchr(t + pos, '\n', leaf->bytes - pos);
        pos = (size_t)(p - t) + 1;
        k++;
    }
    b->c_ln = ln;
    b->c_pos = pos;
    p = leaf->bytes > pos
            ? (const char *)memchr(t + pos, '\n', leaf->bytes - pos)
            : NULL;
    *len = p ? (size_t)(p - (t + pos)) : leaf->bytes - pos;
    *has_nl = p != NULL;
    *off = o0 + pos;
    return t ? t + pos : "";
}

const char *buf_line(Buffer *b, long ln, size_t *len)
{
    int has_nl;
    size_t off;
    const char *s = line_raw(b, ln, len, &has_nl, &off);
    if (b->crlf && has_nl && *len > 0 && s[*len - 1] == '\r')
        (*len)--;
    return s;
}

size_t buf_line_offset(Buffer *b, long ln)
{
    size_t len, off;
    int has_nl;
    line_raw(b, ln, &len, &has_nl, &off);
    return off;
}

void buf_offset_to_pos(Buffer *b, size_t off, long *ln, size_t *col)
{
    long l0;
    size_t o0, rel, start;
    long n;
    Node *leaf;

    if (off > b->root->bytes)
        off = b->root->bytes;
    leaf = find_leaf(b, -1, off, &l0, &o0);
    rel = off - o0;
    n = count_nl(leaf->text, rel);
    /* the line starts after the last '\n' before off, if there is one */
    for (start = n ? rel : 0; start && leaf->text[start - 1] != '\n'; start--)
        ;
    *ln = l0 + n;
    *col = rel - start;
}

/* Notes that line ln (the first line of the edited leaf) changed. */
static void mark_dirty(Buffer *b, long ln)
{
    if (b->dirty < 0 || ln < b->dirty)
        b->dirty = ln;
}

void buf_insert(Buffer *b, size_t off, const char *s, size_t n)
{
    Node *leaf;
    long l0, nl;
    size_t o0, rel;

    if (!n)
        return;
    b->gen++;
    b->changes++;
    if (off > b->root->bytes)
        off = b->root->bytes;
    leaf = find_leaf(b, -1, off, &l0, &o0);
    mark_dirty(b, l0);
    rel = off - o0;
    /* in place while it fits, or while it couldn't be split anyway: one
     * long line, and no line break inserted into it */
    if (leaf->bytes + n <= LEAF_MAX ||
        ((leaf->newlines == 0 ||
          (leaf->newlines == 1 && leaf->text[leaf->bytes - 1] == '\n')) &&
         !memchr(s, '\n', n))) {
        leaf_own(leaf, n);
        memmove(leaf->text + rel + n, leaf->text + rel, leaf->bytes - rel);
        memcpy(leaf->text + rel, s, n);
        nl = count_nl(s, n);
        leaf->bytes += n;
        leaf->newlines += nl;
        add_up(leaf->parent, nl, (long)n);
    } else {
        size_t len = leaf->bytes + n;
        char *d = (char *)xmalloc(len);
        if (rel)
            memcpy(d, leaf->text, rel);
        memcpy(d + rel, s, n);
        if (leaf->bytes - rel)
            memcpy(d + rel + n, leaf->text + rel, leaf->bytes - rel);
        replace_leaf(b, leaf, d, len);
        free(d);
    }
}

void buf_delete(Buffer *b, size_t off, size_t n)
{
    Node *a, *z;
    long l0;
    size_t oa, oz, rel;

    if (off >= b->root->bytes || !n)
        return;
    if (n > b->root->bytes - off)
        n = b->root->bytes - off;
    b->gen++;
    b->changes++;
    a = find_leaf(b, -1, off, &l0, &oa);
    mark_dirty(b, l0);
    z = find_leaf(b, -1, off + n - 1, &l0, &oz);
    rel = off - oa;

    if (a == z) {
        long dnl;
        leaf_own(a, 0);
        dnl = count_nl(a->text + rel, n);
        memmove(a->text + rel, a->text + rel + n, a->bytes - rel - n);
        a->bytes -= n;
        a->newlines -= dnl;
        add_up(a->parent, -dnl, -(long)n);
    } else {
        size_t zrel = off + n - oz;
        size_t len = rel + (z->bytes - zrel);
        char *d = (char *)xmalloc(len);
        Node *cur = leaf_next(a);

        while (cur != z) {
            Node *next = leaf_next(cur);
            unlink_node(b, cur);
            node_free(cur);
            cur = next;
        }
        if (rel)
            memcpy(d, a->text, rel);
        if (z->bytes - zrel)
            memcpy(d + rel, z->text + zrel, z->bytes - zrel);
        unlink_node(b, z);
        node_free(z);
        collapse_root(b);
        if (len)
            replace_leaf(b, a, d, len);
        else {
            add_up(a->parent, -a->newlines, -(long)a->bytes);
            leaf_set_empty(a);
        }
        free(d);
    }
    fix_leaf(b, a);
}

void buf_copy(Buffer *b, size_t off, size_t n, char *dst)
{
    long l0;
    size_t o0, rel;
    Node *leaf;

    if (!n)
        return;
    leaf = find_leaf(b, -1, off, &l0, &o0);
    rel = off - o0;
    while (n && leaf) {
        size_t k = leaf->bytes - rel;
        if (k > n)
            k = n;
        memcpy(dst, leaf->text + rel, k);
        dst += k;
        n -= k;
        rel = 0;
        leaf = leaf_next(leaf);
    }
}

/* The bytes a match can start with: pat's first, or ignoring case its
 * lowercase and uppercase forms (the same byte if it is not a letter). */
static void first_byte(const char *pat, int icase, int *lo, int *up)
{
    *lo = *up = (unsigned char)pat[0];
    if (icase) {
        *lo = ascii_lower(*lo);
        *up = *lo >= 'a' && *lo <= 'z' ? *lo - 32 : *lo;
    }
}

/* First match in s[0..n), or -1. The candidates are found with memchr: for
 * a letter ignoring case, the nearer of the next lowercase and the next
 * uppercase one. */
static long search_fwd(const char *s, size_t n, const char *pat, size_t plen,
                       int icase)
{
    const char *end, *p, *q, *c;
    int lo, up;

    if (n < plen)
        return -1;
    first_byte(pat, icase, &lo, &up);
    end = s + n - plen + 1;             /* matches start before this */
    p = (const char *)memchr(s, lo, (size_t)(end - s));
    q = up != lo ? (const char *)memchr(s, up, (size_t)(end - s)) : NULL;
    while (p || q) {
        c = p && (!q || p < q) ? p : q;
        if (mem_match(c, pat, plen, icase))
            return (long)(c - s);
        if (c == p)
            p = (const char *)memchr(p + 1, lo, (size_t)(end - p - 1));
        else
            q = (const char *)memchr(q + 1, up, (size_t)(end - q - 1));
    }
    return -1;
}

/* Last match starting in s[lo..n - plen], or -1. */
static long search_bwd(const char *s, size_t n, size_t lo, const char *pat,
                       size_t plen, int icase)
{
    size_t i;
    int c0, c1;

    if (n < plen)
        return -1;
    first_byte(pat, icase, &c0, &c1);
    for (i = n - plen + 1; i > lo; i--) {
        int c = (unsigned char)s[i - 1];
        if ((c == c0 || c == c1) &&
            mem_match(s + i - 1, pat, plen, icase))
            return (long)(i - 1);
    }
    return -1;
}

size_t buf_find(Buffer *b, size_t from, size_t to, const char *pat,
                size_t plen, int icase, int backward)
{
    long l0;
    size_t o0, rel;
    Node *leaf;
    long r;

    if (!plen)
        return (size_t)-1;
    if (from > b->root->bytes)
        from = b->root->bytes;
    leaf = find_leaf(b, -1, from, &l0, &o0);
    rel = from - o0;
    if (!backward) {
        /* a match can't span leaves: all but the last end in '\n' */
        while (leaf && o0 + rel < to) {
            size_t n = leaf->bytes - rel, room = to - (o0 + rel);
            if (room < n && room + plen - 1 < n)
                n = room + plen - 1;
            r = search_fwd(leaf->text + rel, n, pat, plen, icase);
            if (r >= 0)
                return o0 + rel + (size_t)r;
            o0 += leaf->bytes;
            rel = 0;
            leaf = leaf_next(leaf);
        }
    } else {
        size_t lim = rel + plen;
        if (lim > leaf->bytes)
            lim = leaf->bytes;
        while (leaf && o0 + lim >= to) {
            r = search_bwd(leaf->text, lim, to > o0 ? to - o0 : 0, pat, plen,
                           icase);
            if (r >= 0)
                return o0 + (size_t)r;
            if (o0 <= to)
                break;
            leaf = leaf_prev(leaf);
            if (leaf) {
                o0 -= leaf->bytes;
                lim = leaf->bytes;
            }
        }
    }
    return (size_t)-1;
}

/*
 * Saving. The text goes to a temporary file in the same directory, which
 * replaces the target when it is complete, so that a failed save leaves
 * the file as it was. Where no file can be made there, the target is
 * overwritten in place, after copying the leaves that borrow from the
 * mapping of that very file.
 */
enum { SV_LOAD, SV_OWN, SV_WRITE, SV_OVER };

struct BufSave {
    Buffer *b;
    char *target;
    char *tmp;          /* NULL: overwriting target in place */
    int fd;
    int phase;          /* SV_* */
    int ok;             /* the save ended well */
    Node *leaf;         /* the next leaf to copy */
    char err[256];
};

/* Records errno as the save's error and ends it. */
static int save_fail(BufSave *s)
{
    str_copy(s->err, sizeof s->err, strerror(errno));
    s->phase = SV_OVER;
    return -1;
}

#ifdef _WIN32
static char *full_path(const char *path)
{
    return _fullpath(NULL, path, 0);
}

/* Creates the temporary file s->tmp, whose name ends in XXXXXX. Returns
 * its descriptor, or -1. */
static int open_temp(BufSave *s)
{
    /* replacing the target gives it the target's attributes */
    if (_mktemp_s(s->tmp, strlen(s->tmp) + 1) != 0)
        return -1;
    return _open(s->tmp, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                 _S_IREAD | _S_IWRITE);
}

/* Puts the temporary file in place of the target. ReplaceFile keeps the
 * target's attributes, permissions and creation time, as open_temp keeps
 * the mode on POSIX. */
static int replace_target(BufSave *s)
{
    DWORD e;

    if (ReplaceFileA(s->target, s->tmp, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS,
                     NULL, NULL))
        return 0;
    e = GetLastError();
    if (e == ERROR_FILE_NOT_FOUND) {    /* a new file */
        if (MoveFileExA(s->tmp, s->target, MOVEFILE_WRITE_THROUGH))
            return 0;
        e = GetLastError();
    }
    win_error(s->err, sizeof s->err, e);
    s->phase = SV_OVER;
    return -1;
}
#else
static char *full_path(const char *path)
{
    return realpath(path, NULL);
}

/* Creates the temporary file s->tmp, whose name ends in XXXXXX, with the
 * mode the target has, or a new file would get. Returns its descriptor,
 * or -1. */
static int open_temp(BufSave *s)
{
    struct stat st;
    mode_t mode;
    int fd = mkstemp(s->tmp);

    if (fd < 0)
        return -1;
    if (stat(s->target, &st) == 0) {
        mode = st.st_mode & 07777;
    } else {
        mode_t um = umask(0);
        umask(um);
        mode = 0666 & ~um;
    }
    fchmod(fd, mode);
    return fd;
}

static int replace_target(BufSave *s)
{
    return rename(s->tmp, s->target) < 0 ? save_fail(s) : 0;
}
#endif

BufSave *buf_save_begin(Buffer *b, const char *path, char *err, size_t errlen)
{
    BufSave *s = (BufSave *)xmalloc(sizeof *s);
    char *slash;
    size_t dlen;

    memset(s, 0, sizeof *s);
    s->b = b;
    s->fd = -1;
    s->target = full_path(path);
    if (!s->target)
        s->target = xstrdup(path);
    fix_slashes(s->target);
    slash = strrchr(s->target, '/');
    dlen = slash ? (size_t)(slash - s->target) + 1 : 0;
    s->tmp = (char *)xmalloc(strlen(s->target) + 32);
    memcpy(s->tmp, s->target, dlen);
    sprintf(s->tmp + dlen, ".%s.cedit-XXXXXX", s->target + dlen);
    s->fd = open_temp(s);
    if (s->fd < 0) {
        free(s->tmp);
        s->tmp = NULL;
        /* in place: the target must be writable, but is opened (and
         * truncated) only once the text no longer borrows from it */
        if (access(s->target, W_OK) < 0 && errno != ENOENT) {
            str_copy(err, errlen, strerror(errno));
            buf_save_free(s);
            return NULL;
        }
    }
    s->phase = SV_LOAD;
    return s;
}

#ifdef _WIN32
static int write_out(int fd, const char *p, size_t n)
{
    while (n) {
        int w = _write(fd, p, n < (1u << 30) ? (unsigned)n : 1u << 30);
        if (w < 0)
            return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

/* Writes the leaves, gathered a megabyte to a system call (Windows has no
 * writev for files). */
static int write_all(int fd, Node *leaf)
{
    size_t cap = (size_t)1 << 20, n = 0;
    char *buf = (char *)xmalloc(cap);
    int r = 0, e;

    for (; leaf && r == 0; leaf = leaf_next(leaf)) {
        if (n + leaf->bytes > cap) {
            r = write_out(fd, buf, n);
            n = 0;
        }
        if (r == 0 && leaf->bytes > cap)
            r = write_out(fd, leaf->text, leaf->bytes);
        else if (r == 0 && leaf->bytes) {
            memcpy(buf + n, leaf->text, leaf->bytes);
            n += leaf->bytes;
        }
    }
    if (r == 0)
        r = write_out(fd, buf, n);
    e = errno;
    free(buf);
    errno = e;
    return r;
}
#else
/* Writes the leaves, many to a system call. */
static int write_all(int fd, Node *leaf)
{
    struct iovec iov[256];
    int n, k;

    while (leaf) {
        for (n = 0; leaf && n < 256; leaf = leaf_next(leaf))
            if (leaf->bytes) {
                iov[n].iov_base = leaf->text;
                iov[n++].iov_len = leaf->bytes;
            }
        for (k = 0; k < n;) {
            ssize_t w = writev(fd, iov + k, n - k);
            if (w < 0 && errno == EINTR)
                continue;
            if (w < 0)
                return -1;
            for (; k < n && (size_t)w >= iov[k].iov_len; k++)
                w -= (ssize_t)iov[k].iov_len;
            if (k < n) {
                iov[k].iov_base = (char *)iov[k].iov_base + w;
                iov[k].iov_len -= (size_t)w;
            }
        }
    }
    return 0;
}
#endif

int buf_save_step(BufSave *s, size_t budget)
{
    Buffer *b = s->b;
    size_t done = 0;

    if (s->phase == SV_LOAD) {
        if (buf_load_step(b, budget))
            return 1;
        s->leaf = first_leaf(b->root);
        s->phase = s->tmp ? SV_WRITE : SV_OWN;
        return 1;
    }
    if (s->phase == SV_OWN) {
        for (; s->leaf && done < budget; s->leaf = leaf_next(s->leaf))
            if (s->leaf->cap == 0 && s->leaf->bytes) {
                leaf_own(s->leaf, 0);
                done += s->leaf->bytes;
            }
        if (s->leaf)
            return 1;
        /* nothing borrows from the mapping now, and Windows can't
         * truncate a mapped file */
        drop_map(b);
        s->fd = open(s->target, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
        if (s->fd < 0)
            return save_fail(s);
        s->phase = SV_WRITE;
        return 1;
    }
    return s->phase == SV_WRITE ? 0 : -1;
}

int buf_save_end(BufSave *s)
{
    int fd = s->fd, e;

    s->fd = -1;
    if (write_all(fd, first_leaf(s->b->root)) < 0 || (s->tmp && fsync(fd) < 0)) {
        e = errno;
        close(fd);
        errno = e;
        return save_fail(s);
    }
    if (close(fd) < 0)
        return save_fail(s);
    if (s->tmp && replace_target(s) < 0)
        return -1;
    s->phase = SV_OVER;
    s->ok = 1;
    return 0;
}

const char *buf_save_error(const BufSave *s)
{
    return s->err;
}

void buf_save_free(BufSave *s)
{
    if (!s)
        return;
    if (s->fd >= 0)
        close(s->fd);
    if (s->tmp && !s->ok)
        unlink(s->tmp);
    free(s->tmp);
    free(s->target);
    free(s);
}

int buf_save(Buffer *b, const char *path, char *err, size_t errlen)
{
    BufSave *s = buf_save_begin(b, path, err, errlen);
    int r;

    if (!s)
        return -1;
    while ((r = buf_save_step(s, (size_t)1 << 30)) > 0)
        ;
    if (r == 0)
        r = buf_save_end(s);
    if (r < 0)
        str_copy(err, errlen, s->err);
    buf_save_free(s);
    return r;
}

static int check_node(Buffer *b, Node *n, Node *parent, int *depth, int d)
{
    if (n->parent != parent)
        return 1;
    if (n->is_leaf) {
        Node *next;
        if (*depth < 0)
            *depth = d;
        if (*depth != d)
            return 2;
        if (n->newlines != count_nl(n->text ? n->text : "", n->bytes))
            return 3;
        if (n->bytes == 0 && b->root != n)
            return 4;
        next = leaf_next(n);
        if (next && n->text[n->bytes - 1] != '\n')
            return 5;
        if (n->cap && n->bytes > n->cap)
            return 6;
    } else {
        int i, r;
        long nl = 0;
        size_t bytes = 0;
        if (n->nkids < 1 || n->nkids > FANOUT)
            return 7;
        for (i = 0; i < n->nkids; i++) {
            r = check_node(b, n->kid[i], n, depth, d + 1);
            if (r)
                return r;
            nl += n->kid[i]->newlines;
            bytes += n->kid[i]->bytes;
        }
        if (nl != n->newlines || bytes != n->bytes)
            return 8;
        if (!parent && n->nkids < 2)
            return 9;
    }
    return 0;
}

int buf_check(Buffer *b)
{
    int depth = -1;
    return check_node(b, b->root, NULL, &depth, 0);
}
