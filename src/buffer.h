/*
 * buffer.h - text storage: a B-tree of line-aligned leaves, modeled on
 * Vim's memline.
 *
 * The buffer is a plain byte string. Lines are separated by '\n'; a buffer
 * with N newlines has N + 1 lines. Positions are byte offsets. In "dos" mode
 * a '\r' right before a '\n' is hidden from the visible line length.
 */
#ifndef CEDIT_BUFFER_H
#define CEDIT_BUFFER_H

#include <stddef.h>

typedef struct Node Node;     /* tree node, private to buffer.c */

typedef struct Buffer {
    Node *root;
    int crlf;           /* 1: lines end in "\r\n" */
    unsigned long gen;  /* bumped on every change; invalidates caches */
    long dirty;         /* lowest line changed since the highlighter (which
                           resets it) last looked, or -1 */

    /* source file mapping (or heap copy) that borrowed leaves point into */
    char *map;
    size_t map_len;
    int map_is_mmap;
    size_t load_pos;    /* bytes of map already in the tree */

    /* line lookup cache (Vim's ml_locked) */
    unsigned long c_gen;
    Node *c_leaf;
    long c_line0;       /* first line starting in c_leaf */
    size_t c_off0;      /* byte offset of c_leaf */
    long c_ln;          /* last line looked up inside c_leaf ... */
    size_t c_pos;       /* ... and its offset inside the leaf */

    unsigned long changes;  /* bumped on every insert or delete (not on
                               loading, unlike gen) */
} Buffer;

Buffer *buf_new(void);
void buf_free(Buffer *b);

/* Opens path and starts incremental loading. Returns 0 on success, otherwise
 * -1 with a message in err. A missing file is not an error: the buffer is
 * empty and *is_new is set. */
int buf_open(Buffer *b, const char *path, int *is_new, char *err, size_t errlen);

int buf_loading(const Buffer *b);
/* Loads up to ~budget more bytes. Returns 1 while more remains. */
int buf_load_step(Buffer *b, size_t budget);
void buf_load_all(Buffer *b);
double buf_load_progress(const Buffer *b);

long buf_lines(const Buffer *b);
size_t buf_size(const Buffer *b);

/* Visible text of line ln (0-based), without its line terminator. The
 * pointer stays valid until the buffer is modified. While loading, asking for
 * a line past the loaded part loads more. */
const char *buf_line(Buffer *b, long ln, size_t *len);
size_t buf_line_offset(Buffer *b, long ln);
void buf_offset_to_pos(Buffer *b, size_t off, long *ln, size_t *col);

void buf_insert(Buffer *b, size_t off, const char *s, size_t n);
void buf_delete(Buffer *b, size_t off, size_t n);
/* Copies n bytes at off into dst. */
void buf_copy(Buffer *b, size_t off, size_t n, char *dst);

/* Finds pat (no newlines) in the part loaded so far: forward, the first
 * match starting in [from, to); backward, the last one starting in
 * [to, from]. Returns the match offset or (size_t)-1. */
size_t buf_find(Buffer *b, size_t from, size_t to, const char *pat,
                size_t plen, int icase, int backward);

/* Saves the whole buffer (loading the rest first): to a temporary file
 * renamed over path, or by overwriting path where that can't be made. */
int buf_save(Buffer *b, const char *path, char *err, size_t errlen);

/*
 * Saving in steps, which buf_save does all at once. buf_save_step loads
 * the rest of the file (and, to overwrite it in place, copies the text
 * that borrows from it) a slice at a time. buf_save_end writes the file
 * and makes it durable (fsync, rename): the part whose time can't be
 * bounded, since the system can make a writer wait for the disk. It only
 * reads the buffer, so it can run on another thread while the buffer
 * doesn't change.
 */
typedef struct BufSave BufSave;

/* Starts saving b to path. Returns NULL, with a message in err, if it
 * can't. */
BufSave *buf_save_begin(Buffer *b, const char *path, char *err, size_t errlen);
/* Loads or copies about budget more bytes. Returns 1 while there is more
 * to do, 0 when what is left is buf_save_end, -1 on an error. */
int buf_save_step(BufSave *s, size_t budget);
/* Returns 0, or -1 on an error. */
int buf_save_end(BufSave *s);
/* What went wrong, after -1 from buf_save_step or buf_save_end. */
const char *buf_save_error(const BufSave *s);
/* Frees s. A save that didn't end leaves the file as it was (except when
 * overwriting in place, which has started). */
void buf_save_free(BufSave *s);

/* For tests: verifies all tree invariants, returns 0 when consistent. */
int buf_check(Buffer *b);

#endif
