/*
 * editor.h - editing model: cursor, selection, scrolling, edit commands.
 * Knows nothing about SDL; the UI drives it.
 */
#ifndef CEDIT_EDITOR_H
#define CEDIT_EDITOR_H

#include "buffer.h"
#include "undo.h"

enum {
    MV_LEFT, MV_RIGHT, MV_UP, MV_DOWN, MV_HOME, MV_END, MV_PGUP, MV_PGDN,
    MV_DOCSTART, MV_DOCEND, MV_WORDLEFT, MV_WORDRIGHT
};

typedef struct Editor Editor;

/* Settings and search text shared by all the views. */
typedef struct EdOptions {
    int overwrite;
    int autoindent;         /* Enter copies the leading whitespace */
    int tabw;
    int spaces;             /* Tab inserts spaces instead of a tab */
    char find[256];
    char repl[256];
    int icase;
} EdOptions;

/* A file being edited, shown in one or more views (Editors). Each view has
 * its own cursor, selection and scroll position; an edit in one moves the
 * others' along with the text. */
typedef struct Doc {
    Buffer *buf;
    Undo undo;
    char *path;             /* NULL for an untitled buffer */
    Editor *views;          /* linked by Editor.next_view */

    /* undo grouping state */
    Editor *last_ed;        /* the view that edited last */
    int last_kind;
    int last_space;
    unsigned long last_time;

    void *data;             /* the UI's, freed with free_data when the
                               last view lets go of the document */
    void (*free_data)(void *data);
} Doc;

struct Editor {
    Doc *doc;
    Editor *next_view;      /* the next view of doc */
    EdOptions *opt;

    long cy;                /* cursor line */
    size_t cx;              /* cursor byte column (visible part of the line) */
    int sel;                /* selection active */
    long ay;                /* selection anchor */
    size_t ax;
    long want;              /* wanted display column (Vim's w_curswant), -1 none */

    long top;               /* first visible line */
    long left;              /* first visible display column */
    int view_w, view_h;     /* text area size, set by the UI */
    int follow;             /* scroll to the cursor on next draw */
    int moved;              /* moved since the last edit (undo grouping) */

    /* while another view edits: cursor, anchor and top line as offsets */
    size_t o_cur, o_anc, o_top;
};

void ed_options_init(EdOptions *opt);
/* Starts a view of a new empty document. */
void ed_init(Editor *ed, EdOptions *opt);
/* Starts another view of from's document, at the same place. */
void ed_init_view(Editor *ed, const Editor *from);
/* Lets go of the document, freeing it after its last view. */
void ed_free(Editor *ed);
/* How many views show ed's document. */
int ed_views(const Editor *ed);
/* Shows a new empty document, or the file at path, in this view. Other
 * views keep the document shown before. */
void ed_new(Editor *ed);
int ed_open(Editor *ed, const char *path, char *err, size_t errlen);
int ed_save(Editor *ed, const char *path, char *err, size_t errlen);
int ed_modified(const Editor *ed);
const char *ed_name(const Editor *ed);

long ed_lines(Editor *ed);
const char *ed_line(Editor *ed, long ln, size_t *len);

/* display column <-> byte column within a line */
long ed_disp_col(const Editor *ed, const char *s, size_t len, size_t col);
size_t ed_byte_col(const Editor *ed, const char *s, size_t len, long dcol);

void ed_move(Editor *ed, int how, int extend);
void ed_set_cursor(Editor *ed, long ln, size_t col, int extend);
void ed_select_all(Editor *ed);
void ed_select_word(Editor *ed);
void ed_select_line(Editor *ed);
void ed_clear_selection(Editor *ed);
/* Ordered selection bounds; returns 0 if there is no (non-empty) selection. */
int ed_sel_range(const Editor *ed, long *sy, size_t *sx, long *ey, size_t *ex);
int ed_has_selection(const Editor *ed);
void ed_scroll(Editor *ed, long lines);
void ed_scroll_to_cursor(Editor *ed);

/* `now` is a millisecond clock used to group typing into undo steps. */
void ed_type(Editor *ed, const char *s, size_t n, unsigned long now);
void ed_newline(Editor *ed, unsigned long now);
void ed_tab(Editor *ed, int unindent, unsigned long now);
void ed_backspace(Editor *ed, int word, unsigned long now);
void ed_delete(Editor *ed, int word, unsigned long now);
void ed_paste(Editor *ed, const char *s, size_t n);
/* Selected text with '\n' line ends (malloc'd), or NULL. */
char *ed_copy(Editor *ed, size_t *n);
void ed_cut(Editor *ed);
int ed_undo(Editor *ed);
int ed_redo(Editor *ed);

/* Search for opt->find. Returns 1 found, 2 found after wrapping, 0 not found. */
int ed_find(Editor *ed, int backward);
int ed_replace(Editor *ed);
long ed_replace_all(Editor *ed);
void ed_goto(Editor *ed, long line);

#endif
