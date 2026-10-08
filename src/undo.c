/*
 * undo.c - linear undo/redo history.
 */
#include "undo.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

#define MAX_STEPS 1000                          /* Vim's 'undolevels' */
#define MAX_MEM   ((size_t)512 * 1024 * 1024)

static void step_free(Undo *u, UndoStep *s)
{
    int i;
    for (i = 0; i < s->nops; i++) {
        u->mem -= s->ops[i].cap;
        free(s->ops[i].text);
    }
    free(s->ops);
    s->ops = NULL;
    s->nops = s->cap = 0;
}

void undo_init(Undo *u)
{
    memset(u, 0, sizeof *u);
}

void undo_free(Undo *u)
{
    int i;
    for (i = 0; i < u->n; i++)
        step_free(u, &u->steps[i]);
    free(u->steps);
    memset(u, 0, sizeof *u);
}

void undo_boundary(Undo *u)
{
    u->open = 0;
}

static void drop_oldest(Undo *u)
{
    step_free(u, &u->steps[0]);
    memmove(&u->steps[0], &u->steps[1], (size_t)(u->n - 1) * sizeof(UndoStep));
    u->n--;
    u->pos--;
    u->saved = u->saved > 0 ? u->saved - 1 : -1;
}

static UndoStep *current_step(Undo *u, size_t cursor)
{
    UndoStep *s;

    if (u->open && u->pos > 0)
        return &u->steps[u->pos - 1];

    /* New step: forget the redo tail. */
    while (u->n > u->pos)
        step_free(u, &u->steps[--u->n]);
    if (u->saved > u->pos)
        u->saved = -1;
    while (u->n > 0 && (u->n >= MAX_STEPS || u->mem > MAX_MEM))
        drop_oldest(u);
    if (u->n == u->cap) {
        u->cap = u->cap ? u->cap * 2 : 64;
        u->steps = (UndoStep *)xrealloc(u->steps, (size_t)u->cap * sizeof(UndoStep));
    }
    s = &u->steps[u->n++];
    memset(s, 0, sizeof *s);
    s->cur_before = s->cur_after = cursor;
    u->pos = u->n;
    u->open = 1;
    return s;
}

static void op_append(Undo *u, UndoOp *op, const char *s, size_t n, int front)
{
    if (op->len + n > op->cap) {
        size_t cap = (op->len + n) * 2;
        op->text = (char *)xrealloc(op->text, cap);
        u->mem += cap - op->cap;
        op->cap = cap;
    }
    if (front) {
        memmove(op->text + n, op->text, op->len);
        memcpy(op->text, s, n);
    } else {
        memcpy(op->text + op->len, s, n);
    }
    op->len += n;
}

static void record(Undo *u, int ins, size_t off, const char *text, size_t n,
                   size_t cursor)
{
    UndoStep *s = current_step(u, cursor);
    UndoOp *last = s->nops ? &s->ops[s->nops - 1] : NULL;
    UndoOp *op;

    if (last && ins && last->ins && off == last->off + last->len) {
        op_append(u, last, text, n, 0);
        return;
    }
    if (last && !ins && !last->ins && off + n == last->off) {
        op_append(u, last, text, n, 1);        /* backspace */
        last->off = off;
        return;
    }
    if (last && !ins && !last->ins && off == last->off) {
        op_append(u, last, text, n, 0);        /* forward delete */
        return;
    }
    if (s->nops == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 4;
        s->ops = (UndoOp *)xrealloc(s->ops, (size_t)s->cap * sizeof(UndoOp));
    }
    op = &s->ops[s->nops++];
    memset(op, 0, sizeof *op);
    op->ins = ins;
    op->off = off;
    op_append(u, op, text, n, 0);
}

void undo_insert(Undo *u, Buffer *b, size_t off, const char *s, size_t n,
                 size_t cursor)
{
    if (!n)
        return;
    record(u, 1, off, s, n, cursor);
    buf_insert(b, off, s, n);
}

void undo_delete(Undo *u, Buffer *b, size_t off, size_t n, size_t cursor)
{
    char *tmp;

    if (off + n > buf_size(b))
        n = buf_size(b) - off;
    if (!n)
        return;
    tmp = (char *)xmalloc(n);
    buf_copy(b, off, n, tmp);
    record(u, 0, off, tmp, n, cursor);
    free(tmp);
    buf_delete(b, off, n);
}

void undo_set_cursor(Undo *u, size_t cursor)
{
    if (u->open && u->pos > 0)
        u->steps[u->pos - 1].cur_after = cursor;
}

int undo_undo(Undo *u, Buffer *b, size_t *cursor)
{
    UndoStep *s;
    int i;

    u->open = 0;
    if (u->pos == 0)
        return 0;
    s = &u->steps[--u->pos];
    for (i = s->nops - 1; i >= 0; i--) {
        UndoOp *op = &s->ops[i];
        if (op->ins)
            buf_delete(b, op->off, op->len);
        else
            buf_insert(b, op->off, op->text, op->len);
    }
    *cursor = s->cur_before;
    return 1;
}

int undo_redo(Undo *u, Buffer *b, size_t *cursor)
{
    UndoStep *s;
    int i;

    u->open = 0;
    if (u->pos == u->n)
        return 0;
    s = &u->steps[u->pos++];
    for (i = 0; i < s->nops; i++) {
        UndoOp *op = &s->ops[i];
        if (op->ins)
            buf_insert(b, op->off, op->text, op->len);
        else
            buf_delete(b, op->off, op->len);
    }
    *cursor = s->cur_after;
    return 1;
}

void undo_mark_saved(Undo *u)
{
    u->open = 0;
    u->saved = u->pos;
}

int undo_modified(const Undo *u)
{
    return u->pos != u->saved;
}
