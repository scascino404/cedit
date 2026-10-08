/*
 * undo.h - linear undo/redo of byte-level insert/delete ops.
 *
 * A step (Vim's u_header) groups the ops (Vim's u_entry) of one user action.
 * undo_boundary() closes the current step, like Vim's u_sync().
 */
#ifndef CEDIT_UNDO_H
#define CEDIT_UNDO_H

#include "buffer.h"

typedef struct UndoOp {
    int ins;            /* 1: insert, 0: delete */
    size_t off;
    char *text;
    size_t len, cap;
} UndoOp;

typedef struct UndoStep {
    UndoOp *ops;
    int nops, cap;
    size_t cur_before, cur_after;
} UndoStep;

typedef struct Undo {
    UndoStep *steps;
    int n, cap;
    int pos;            /* steps[0 .. pos) are applied */
    int saved;          /* pos when last saved, -1 if unreachable */
    int open;           /* steps[pos - 1] still accepts ops */
    size_t mem;
} Undo;

void undo_init(Undo *u);
void undo_free(Undo *u);
void undo_boundary(Undo *u);

/* Perform an edit on b and record it. cursor is the cursor offset before the
 * edit, remembered when the edit starts a new step. */
void undo_insert(Undo *u, Buffer *b, size_t off, const char *s, size_t n,
                 size_t cursor);
void undo_delete(Undo *u, Buffer *b, size_t off, size_t n, size_t cursor);
void undo_set_cursor(Undo *u, size_t cursor);

/* Return 1 and the cursor offset to restore when something was undone. */
int undo_undo(Undo *u, Buffer *b, size_t *cursor);
int undo_redo(Undo *u, Buffer *b, size_t *cursor);

void undo_mark_saved(Undo *u);
int undo_modified(const Undo *u);

#endif
