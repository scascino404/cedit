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
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define FANOUT      64
#define LOAD_CHUNK  8192        /* loaded leaves borrow ~this many bytes */
#define LEAF_TARGET 8192        /* rebuilt leaves are cut near this size */
#define LEAF_MAX    16384       /* an owned leaf is split above this size */
#define LEAF_MIN    1024        /* a leaf below this is merged with its neighbor */
#define FIRST_LOAD  (1024 * 1024)

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
 * 20 ms per GB, 5-12x faster than the naive loop; see README.md). */
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

/* Makes a leaf's text owned and able to hold extra more bytes. */
static void leaf_own(Node *l, size_t extra)
{
    if (l->cap == 0 || l->bytes + extra > l->cap) {
        size_t cap = l->bytes + extra;
        char *t;
        cap += cap / 2 + 64;
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
    return b;
}

void buf_free(Buffer *b)
{
    if (!b)
        return;
    node_free(b->root);
    if (b->map) {
        if (b->map_is_mmap)
            munmap(b->map, b->map_len);
        else
            free(b->map);
    }
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

int buf_open(Buffer *b, const char *path, int *is_new, char *err,
             size_t errlen)
{
    int fd;
    struct stat st;
    const char *nl;

    *is_new = 0;
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
            if (len == cap) {
                char *nd = (char *)xmalloc(cap * 2);
                memcpy(nd, d, len);
                free(d);
                d = nd;
                cap *= 2;
            }
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
    size_t o0, rel, i, start = 0;
    long n = 0;
    Node *leaf;

    if (off > b->root->bytes)
        off = b->root->bytes;
    leaf = find_leaf(b, -1, off, &l0, &o0);
    rel = off - o0;
    for (i = 0; i < rel; i++) {
        if (leaf->text[i] == '\n') {
            n++;
            start = i + 1;
        }
    }
    *ln = l0 + n;
    *col = rel - start;
}

void buf_insert(Buffer *b, size_t off, const char *s, size_t n)
{
    Node *leaf;
    long l0, nl;
    size_t o0, rel;

    if (!n)
        return;
    b->gen++;
    if (off > b->root->bytes)
        off = b->root->bytes;
    leaf = find_leaf(b, -1, off, &l0, &o0);
    rel = off - o0;
    if (leaf->bytes + n <= LEAF_MAX) {
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
    a = find_leaf(b, -1, off, &l0, &oa);
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

/* First match in s[0..n), or -1. */
static long search_fwd(const char *s, size_t n, const char *pat, size_t plen,
                       int icase)
{
    size_t i = 0;
    int c0 = (unsigned char)pat[0];
    int letter = (c0 | 32) >= 'a' && (c0 | 32) <= 'z';

    if (n < plen)
        return -1;
    if (!icase || !letter) {
        while (i + plen <= n) {
            const char *p = (const char *)memchr(s + i, c0, n - plen + 1 - i);
            if (!p)
                return -1;
            i = (size_t)(p - s);
            if (mem_match(s + i, pat, plen, icase))
                return (long)i;
            i++;
        }
        return -1;
    }
    c0 = ascii_lower(c0);
    for (; i + plen <= n; i++)
        if (ascii_lower((unsigned char)s[i]) == c0 &&
            mem_match(s + i, pat, plen, 1))
            return (long)i;
    return -1;
}

/* Last match starting in s[0..n - plen], or -1. */
static long search_bwd(const char *s, size_t n, const char *pat, size_t plen,
                       int icase)
{
    size_t i;
    if (n < plen)
        return -1;
    for (i = n - plen + 1; i > 0; i--)
        if (mem_match(s + i - 1, pat, plen, icase))
            return (long)(i - 1);
    return -1;
}

size_t buf_find(Buffer *b, size_t from, const char *pat, size_t plen,
                int icase, int backward)
{
    long l0;
    size_t o0, rel;
    Node *leaf;
    long r;

    if (!plen)
        return (size_t)-1;
    buf_load_all(b);
    if (from > b->root->bytes)
        from = b->root->bytes;
    leaf = find_leaf(b, -1, from, &l0, &o0);
    rel = from - o0;
    if (!backward) {
        while (leaf) {
            r = search_fwd(leaf->text + rel, leaf->bytes - rel, pat, plen,
                           icase);
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
        while (leaf) {
            r = search_bwd(leaf->text, lim, pat, plen, icase);
            if (r >= 0)
                return o0 + (size_t)r;
            leaf = leaf_prev(leaf);
            if (leaf) {
                o0 -= leaf->bytes;
                lim = leaf->bytes;
            }
        }
    }
    return (size_t)-1;
}

static int write_all(int fd, Buffer *b)
{
    Node *leaf = first_leaf(b->root);
    for (; leaf; leaf = leaf_next(leaf)) {
        const char *p = leaf->text;
        size_t n = leaf->bytes;
        while (n) {
            ssize_t w = write(fd, p, n);
            if (w < 0 && errno == EINTR)
                continue;
            if (w < 0)
                return -1;
            p += w;
            n -= (size_t)w;
        }
    }
    return 0;
}

int buf_save(Buffer *b, const char *path, char *err, size_t errlen)
{
    char *target, *tmp, *slash;
    struct stat st;
    int have_st, fd;
    size_t dlen;

    buf_load_all(b);
    target = realpath(path, NULL);
    if (!target)
        target = xstrdup(path);
    have_st = stat(target, &st) == 0;

    /* 1. temp file in the same directory, then rename over the target */
    slash = strrchr(target, '/');
    dlen = slash ? (size_t)(slash - target) + 1 : 0;
    tmp = (char *)xmalloc(strlen(target) + 32);
    memcpy(tmp, target, dlen);
    sprintf(tmp + dlen, ".%s.cedit-XXXXXX", target + dlen);
    fd = mkstemp(tmp);
    if (fd >= 0) {
        mode_t mode;
        if (have_st) {
            mode = st.st_mode & 07777;
        } else {
            mode_t um = umask(0);
            umask(um);
            mode = 0666 & ~um;
        }
        fchmod(fd, mode);
        if (write_all(fd, b) == 0 && fsync(fd) == 0 && close(fd) == 0 &&
            rename(tmp, target) == 0) {
            free(tmp);
            free(target);
            return 0;
        }
        str_copy(err, errlen, strerror(errno));
        close(fd);
        unlink(tmp);
        free(tmp);
        free(target);
        return -1;
    }
    free(tmp);

    /* 2. no temp file possible: overwrite in place. Copy borrowed leaves
     * first, since they may point into the very file we overwrite. */
    {
        Node *leaf;
        for (leaf = first_leaf(b->root); leaf; leaf = leaf_next(leaf))
            if (leaf->cap == 0 && leaf->bytes)
                leaf_own(leaf, 0);
    }
    fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0 || write_all(fd, b) < 0 || close(fd) < 0) {
        str_copy(err, errlen, strerror(errno));
        if (fd >= 0)
            close(fd);
        free(target);
        return -1;
    }
    free(target);
    return 0;
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
