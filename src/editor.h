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

#define ED_ROWS 4           /* lines whose rows a view remembers */

/* Where the rows of a line start with word wrap, for the buffer generation,
 * wrap width and tab width they were found with (see editor.c, find_rows). */
typedef struct WrapRows {
    long ln;
    unsigned long gen;      /* 0: unused */
    int w, tabw;
    size_t *start;
    long n, cap;
} WrapRows;

/* Settings and search text shared by all the views. */
typedef struct EdOptions {
    int overwrite;
    int autoindent;         /* Enter copies the leading whitespace */
    int tabw;
    int spaces;             /* Tab inserts spaces instead of a tab */
    int wrap;               /* word wrap: long lines go on in more rows */
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
    long top_row;           /* with word wrap: its first visible row */
    long left;              /* first visible display column */
    int view_w, view_h;     /* text area size, set by the UI */
    int follow;             /* scroll to the cursor on next draw */
    int moved;              /* moved since the last edit (undo grouping) */

    /* while another view edits: cursor, anchor and top line as offsets */
    size_t o_cur, o_anc, o_top;

    /* the rows of the lines asked about last: [0] for short lines, the
     * others for long ones, which are slow to break into rows */
    WrapRows rows[ED_ROWS];
    int rows_next;
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
/* Marks the document saved as path, once a save (buf_save_begin) is over. */
void ed_set_saved(Editor *ed, const char *path);
int ed_modified(const Editor *ed);
const char *ed_name(const Editor *ed);

long ed_lines(Editor *ed);
const char *ed_line(Editor *ed, long ln, size_t *len);

/* display column <-> byte column within a line */
long ed_disp_col(const Editor *ed, const char *s, size_t len, size_t col);
size_t ed_byte_col(const Editor *ed, const char *s, size_t len, long dcol);

/*
 * Word wrap. A line shows as rows of at most ed_wrap_width display columns,
 * broken after the last space that fits, or between characters if none
 * does. The column after them holds the wrap mark, or the cursor at the end
 * of the line. Tabs align within their row. Without word wrap, a line is
 * one row.
 */
int ed_wrap_width(const Editor *ed);
/* The end of the row that starts at byte start: the next row's start, or
 * len for the last row. */
size_t ed_row_end(const Editor *ed, const char *s, size_t len, size_t start);
/* The start of row `row` of a line (of its last row if it has fewer). */
size_t ed_row_start(const Editor *ed, const char *s, size_t len, long row);
/* The row that byte col shows in, and that row's start. A position at a
 * row's end shows at the start of the next one. */
long ed_row_of(const Editor *ed, const char *s, size_t len, size_t col, size_t *start);
/* ed_row_start for line ln of the document, remembering the line's rows. */
size_t ed_line_row_start(Editor *ed, long ln, long row);
/* The cursor's place in the text area: its row from the top and display
 * column from the left. A cursor line above or below the view gives a row
 * of -1 or view_h. */
void ed_cursor_spot(Editor *ed, long *row, long *x);
/* The position shown at a row and display column of the text area. Past
 * the end of a wrapped row it is before the row's last character. */
void ed_pos_at(Editor *ed, long row, long x, long *ln, size_t *col);

/* Up and Down go by rows, Page Up and Page Down by screens of rows. */
void ed_move(Editor *ed, int how, int extend);
void ed_set_cursor(Editor *ed, long ln, size_t col, int extend);
void ed_select_all(Editor *ed);
void ed_select_word(Editor *ed);
void ed_select_line(Editor *ed);
void ed_clear_selection(Editor *ed);
/* Ordered selection bounds; returns 0 if there is no (non-empty) selection. */
int ed_sel_range(const Editor *ed, long *sy, size_t *sx, long *ey, size_t *ex);
int ed_has_selection(const Editor *ed);
/* Scrolls by rows (lines without word wrap), and keeps the view in the
 * document. */
void ed_scroll(Editor *ed, long rows);
void ed_scroll_to_cursor(Editor *ed);
/* Scrolls so the cursor shows at a row of the text area, if the document
 * reaches up that far. */
void ed_scroll_cursor_to(Editor *ed, long row);

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
/* Replaces the selection with opt->repl if it is a match. Returns 1 if it
 * was. */
int ed_replace_selection(Editor *ed);
long ed_replace_all(Editor *ed);
void ed_goto(Editor *ed, long line);

/*
 * ed_find and ed_replace_all a slice at a time, for big files: each step
 * searches about `bytes` more, loading more of the file when it gets to
 * the end of what is loaded. The document must not change between steps
 * except by the steps themselves.
 */
typedef struct EdSearch {
    char pat[256];          /* opt->find when it started */
    size_t plen;
    int icase, backward;
    int pass;               /* 0 from the start on, 1 the rest after
                               wrapping, 2 over */
    size_t start;           /* where it started */
    size_t pos;             /* where the next slice starts */
    size_t done;            /* bytes searched */
} EdSearch;

void ed_find_begin(Editor *ed, EdSearch *s, int backward);
/* Returns -1 while the search goes on, else as ed_find. */
int ed_find_step(Editor *ed, EdSearch *s, size_t bytes);

typedef struct EdReplace {
    char pat[256], repl[256];
    size_t plen, rlen;
    int icase;
    size_t pos;             /* where the next slice searches from */
    size_t mark;            /* the end of the last replacement */
    size_t cur;             /* the cursor when it started, for undo */
    long count;             /* replacements so far */
    int over;
} EdReplace;

/* Starts the undo step that all the replacements go to. */
void ed_replace_all_begin(Editor *ed, EdReplace *r);
/* Returns 1 while there is more to search, 0 when all is replaced. */
int ed_replace_all_step(Editor *ed, EdReplace *r, size_t bytes);

/* The share of the document searched by a step-wise search or Replace All,
 * from 0 to 1. */
double ed_search_progress(Editor *ed, size_t done);

#endif
