/*
 * ui.c - the cedit user interface: menu bar, editor window, dialogs.
 *
 * Everything is drawn into the Screen's character grid, text-mode style.
 */
#define _XOPEN_SOURCE 700
#include "ui.h"
#include "utf8.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BLINK_MS   530
#define MSG_MS     4000
#define DCLICK_MS  400
#define LOAD_SLICE ((size_t)32 * 1024 * 1024)

/* Color roles, as indices into the 16-color palette. */
typedef struct Theme {
    unsigned char text_fg, text_bg;     /* document, menus, dialogs */
    unsigned char sel_fg, sel_bg;       /* selection, highlighted item */
    unsigned char frame;                /* borders and scrollbar */
    unsigned char title_fg, title_bg;   /* window and dialog titles */
    unsigned char bar_fg, bar_bg, bar_hot;  /* menu bar and status bar */
    unsigned char hot, sel_hot;         /* hotkey letters */
    unsigned char disabled, lnum, cur_lnum;
    unsigned char special_fg, special_bg, special_sel_bg;  /* ^X controls */
    unsigned char bad;                  /* placeholder glyphs, errors */
    unsigned char field_fg, field_bg;
    unsigned char focus_fg, focus_bg;   /* focused field or checkbox */
    unsigned char dir;                  /* directories in the file list */
    unsigned char cursor_bg, cursor_ink, cursor_box;
} Theme;

/* TempleOS: blue ink on white paper */
static const Theme theme_light = {
    BLUE, WHITE,                    /* text */
    WHITE, BLUE,                    /* selection */
    BLUE,                           /* frame */
    WHITE, BLUE,                    /* title */
    WHITE, BLUE, YELLOW,            /* bars */
    RED, YELLOW,                    /* hotkeys */
    LIGHTGRAY, DARKGRAY, BLUE,      /* disabled, line numbers */
    WHITE, RED, MAGENTA,            /* control characters */
    RED,                            /* bad */
    BLUE, LIGHTCYAN,                /* field */
    BLUE, YELLOW,                   /* focus */
    RED,                            /* directories */
    YELLOW, BLUE, RED               /* cursor */
};

/* Dark mode: light gray on black, same blue bars and yellow block cursor */
static const Theme theme_dark = {
    LIGHTGRAY, BLACK,               /* text */
    WHITE, BLUE,                    /* selection */
    LIGHTBLUE,                      /* frame */
    WHITE, BLUE,                    /* title */
    WHITE, BLUE, YELLOW,            /* bars */
    LIGHTRED, YELLOW,               /* hotkeys */
    DARKGRAY, DARKGRAY, WHITE,      /* disabled, line numbers */
    WHITE, RED, MAGENTA,            /* control characters */
    LIGHTRED,                       /* bad */
    WHITE, BLUE,                    /* field */
    BLACK, YELLOW,                  /* focus */
    LIGHTCYAN,                      /* directories */
    YELLOW, BLACK, LIGHTRED         /* cursor */
};

static const Theme *T = &theme_light;

enum {
    CMD_NONE, CMD_NEW, CMD_OPEN, CMD_SAVE, CMD_SAVEAS, CMD_EXIT,
    CMD_UNDO, CMD_REDO, CMD_CUT, CMD_COPY, CMD_PASTE, CMD_DELETE, CMD_SELALL,
    CMD_OVERWRITE, CMD_FIND, CMD_FINDNEXT, CMD_FINDPREV, CMD_REPLACE, CMD_GOTO,
    CMD_SIZE_S, CMD_SIZE_N, CMD_SIZE_L, CMD_LINENUM, CMD_TAB4, CMD_TAB8,
    CMD_DARK, CMD_AUTOINDENT, CMD_HELP, CMD_ABOUT
};

enum { P_NONE, P_QUIT, P_NEW, P_OPEN, P_OPEN_PATH };

enum {
    DLG_NONE, DLG_OPEN, DLG_SAVEAS, DLG_FIND, DLG_REPLACE, DLG_GOTO,
    DLG_CONFIRM, DLG_MESSAGE, DLG_HELP
};

enum { W_LABEL, W_FIELD, W_CHECK, W_BUTTON };

enum {
    ID_NONE, ID_OK, ID_CANCEL, ID_YES, ID_NO, ID_NAME, ID_FIND, ID_REPL,
    ID_CASE, ID_FINDNEXT, ID_REPLACE, ID_REPLALL, ID_LINE, ID_DIR
};

typedef struct MenuItem {
    const char *label;      /* '&' marks the hotkey; NULL = separator */
    const char *keys;
    int cmd;
} MenuItem;

static const MenuItem m_file[] = {
    {"&New", "Ctrl+N", CMD_NEW},
    {"&Open...", "Ctrl+O", CMD_OPEN},
    {"&Save", "Ctrl+S", CMD_SAVE},
    {"Save &As...", "Ctrl+Shift+S", CMD_SAVEAS},
    {NULL, NULL, 0},
    {"E&xit", "Ctrl+Q", CMD_EXIT}
};
static const MenuItem m_edit[] = {
    {"&Undo", "Ctrl+Z", CMD_UNDO},
    {"&Redo", "Ctrl+Y", CMD_REDO},
    {NULL, NULL, 0},
    {"Cu&t", "Ctrl+X", CMD_CUT},
    {"&Copy", "Ctrl+C", CMD_COPY},
    {"&Paste", "Ctrl+V", CMD_PASTE},
    {"&Delete", "Del", CMD_DELETE},
    {NULL, NULL, 0},
    {"Select &All", "Ctrl+A", CMD_SELALL},
    {"&Overwrite Mode", "Ins", CMD_OVERWRITE},
    {"Auto &Indent", "", CMD_AUTOINDENT}
};
static const MenuItem m_search[] = {
    {"&Find...", "Ctrl+F", CMD_FIND},
    {"Find &Next", "F3", CMD_FINDNEXT},
    {"Find &Previous", "Shift+F3", CMD_FINDPREV},
    {"&Replace...", "Ctrl+H", CMD_REPLACE},
    {NULL, NULL, 0},
    {"&Go to Line...", "Ctrl+G", CMD_GOTO}
};
static const MenuItem m_view[] = {
    {"&Small Text", "", CMD_SIZE_S},
    {"&Normal Text", "", CMD_SIZE_N},
    {"&Large Text", "Ctrl+0", CMD_SIZE_L},
    {NULL, NULL, 0},
    {"&Dark Mode", "", CMD_DARK},
    {"Line N&umbers", "Ctrl+L", CMD_LINENUM},
    {"Tab Width &4", "", CMD_TAB4},
    {"Tab Width &8", "", CMD_TAB8}
};
static const MenuItem m_help[] = {
    {"&Keyboard...", "F1", CMD_HELP},
    {"&About cedit...", "", CMD_ABOUT}
};

static const struct {
    const char *title;
    const MenuItem *items;
    int n;
} menus[] = {
    {"&File", m_file, sizeof m_file / sizeof m_file[0]},
    {"&Edit", m_edit, sizeof m_edit / sizeof m_edit[0]},
    {"&Search", m_search, sizeof m_search / sizeof m_search[0]},
    {"&View", m_view, sizeof m_view / sizeof m_view[0]},
    {"&Help", m_help, sizeof m_help / sizeof m_help[0]}
};
#define NMENUS ((int)(sizeof menus / sizeof menus[0]))

static const char *const help_lines[] = {
    "Ctrl+N  New            Ctrl+O  Open...",
    "Ctrl+S  Save           Ctrl+Shift+S  Save As...",
    "Ctrl+Q  Exit           F10 / Alt  Menu bar",
    "",
    "Ctrl+Z  Undo           Ctrl+Y  Redo",
    "Ctrl+X  Cut            Ctrl+C  Copy",
    "Ctrl+V  Paste          Ctrl+A  Select all",
    "Tab / Shift+Tab   Indent / unindent selection",
    "Ins     Insert / overwrite mode",
    "",
    "Ctrl+F  Find           F3 / Shift+F3  Next / previous",
    "Ctrl+H  Replace        Ctrl+G  Go to line",
    "",
    "Shift+arrows  Select   Ctrl+arrows  Word / scroll",
    "Home / End    Line     Ctrl+Home / End  File",
    "Ctrl+Backspace / Ctrl+Del   Delete word",
    "",
    "Ctrl+- / Ctrl+=  Smaller / larger text (or Ctrl+wheel)",
    "Ctrl+0  Default (large) text",
    "Ctrl+L  Line numbers",
    NULL
};

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static unsigned long now_ms(void)
{
    return (unsigned long)SDL_GetTicks();
}

static void copy_str(char *dst, size_t size, const char *src)
{
    size_t n = strlen(src);
    if (n >= size)
        n = size - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void cat_str(char *dst, size_t size, const char *src)
{
    size_t l = strlen(dst);
    if (l < size)
        copy_str(dst + l, size - l, src);
}

static void set_msg(App *a, const char *s1, const char *s2)
{
    copy_str(a->msg, sizeof a->msg, s1);
    if (s2)
        cat_str(a->msg, sizeof a->msg, s2);
    a->msg_until = now_ms() + MSG_MS;
}

/* Display width of a UTF-8 string, ignoring '&' markers if amp is set. */
static int str_width(const char *s, int amp)
{
    size_t n = strlen(s), i = 0;
    int w = 0;
    while (i < n) {
        unsigned long cp;
        i += utf8_decode(s + i, n - i, &cp);
        if (!(amp && cp == '&'))
            w++;
    }
    return w;
}

/* Draws a label with an '&'-marked hotkey; returns the width. */
static int put_label(Screen *s, int x, int y, const char *str, int fg, int hot,
                     int bg)
{
    size_t n = strlen(str), i = 0;
    int w = 0, next_hot = 0;
    while (i < n) {
        unsigned long cp;
        i += utf8_decode(str + i, n - i, &cp);
        if (cp == '&') {
            next_hot = 1;
            continue;
        }
        screen_put(s, x + w, y, cp, next_hot ? hot : fg, bg);
        next_hot = 0;
        w++;
    }
    return w;
}

static int hotkey_of(const char *label)
{
    const char *p = strchr(label, '&');
    int c = p ? (unsigned char)p[1] : 0;
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static void put_frame(Screen *s, int x, int y, int w, int h, int dbl, int fg,
                      int bg)
{
    int i;
    unsigned long hz = dbl ? 0x2550 : 0x2500, vt = dbl ? 0x2551 : 0x2502;
    screen_put(s, x, y, dbl ? 0x2554 : 0x250C, fg, bg);
    screen_put(s, x + w - 1, y, dbl ? 0x2557 : 0x2510, fg, bg);
    screen_put(s, x, y + h - 1, dbl ? 0x255A : 0x2514, fg, bg);
    screen_put(s, x + w - 1, y + h - 1, dbl ? 0x255D : 0x2518, fg, bg);
    for (i = 1; i < w - 1; i++) {
        screen_put(s, x + i, y, hz, fg, bg);
        screen_put(s, x + i, y + h - 1, hz, fg, bg);
    }
    for (i = 1; i < h - 1; i++) {
        screen_put(s, x, y + i, vt, fg, bg);
        screen_put(s, x + w - 1, y + i, vt, fg, bg);
    }
}

static void update_title(App *a)
{
    char t[512];
    copy_str(t, sizeof t, ed_modified(&a->ed) ? "* " : "");
    cat_str(t, sizeof t, ed_name(&a->ed));
    cat_str(t, sizeof t, " - cedit");
    if (strcmp(t, a->title) != 0) {
        copy_str(a->title, sizeof a->title, t);
        SDL_SetWindowTitle(a->scr.win, t);
    }
}

/* cedit draws the pointer itself (see screen.h), so this only picks the
 * image. */
static void set_pointer(App *a, int kind)
{
    a->scr.ptr_kind = kind;
}

static void wake_cursor(App *a)
{
    a->blink_on = 1;
    a->blink_next = now_ms() + BLINK_MS;
}

/* Text area geometry. */
static void text_area(App *a, int *x, int *y, int *w, int *h, int *gutter)
{
    int g = 0;
    if (a->show_lnum) {
        long n = ed_lines(&a->ed);
        int digits = 1;
        while (n >= 10) {
            n /= 10;
            digits++;
        }
        g = (digits < 4 ? 4 : digits) + 1;
    }
    *gutter = g;
    *x = 1 + g;
    *y = 2;
    *w = a->scr.cols - 2 - g;
    *h = a->scr.rows - 4;
    if (*w < 1)
        *w = 1;
    if (*h < 1)
        *h = 1;
}

/* Loads the rest of a file that is still being indexed, showing the busy
 * pointer for the moment it takes. */
static void finish_loading(App *a)
{
    if (buf_loading(a->ed.buf)) {
        set_pointer(a, PTR_WAIT);
        screen_present(&a->scr);        /* show the hourglass now */
        buf_load_all(a->ed.buf);
        set_pointer(a, PTR_ARROW);
    }
}

/* ------------------------------------------------------------------ */
/* dialogs                                                             */
/* ------------------------------------------------------------------ */

static void free_items(Dialog *d)
{
    int i;
    for (i = 0; d->items && i < d->nitems; i++)
        free(d->items[i]);
    free(d->items);
    d->items = NULL;
    d->nitems = 0;
}

static void dlg_close(App *a)
{
    free_items(&a->dlg);
    a->dlg.kind = DLG_NONE;
    wake_cursor(a);
}

static void dlg_begin(App *a, int kind, const char *title, int w, int h)
{
    Dialog *d = &a->dlg;
    free_items(d);
    memset(d, 0, sizeof *d);
    d->kind = kind;
    copy_str(d->title, sizeof d->title, title);
    d->w = w;
    d->h = h;
    d->focus = -1;
}

static Widget *dlg_add(Dialog *d, int kind, int id, int x, int y, int w,
                       const char *label)
{
    Widget *wd = &d->wd[d->n++];
    memset(wd, 0, sizeof *wd);
    wd->kind = kind;
    wd->id = id;
    wd->x = x;
    wd->y = y;
    wd->w = w;
    copy_str(wd->label, sizeof wd->label, label ? label : "");
    if (kind == W_BUTTON)
        wd->w = str_width(wd->label, 1) + 4;
    if (d->focus < 0 && kind != W_LABEL)
        d->focus = d->n - 1;
    return wd;
}

static void dlg_buttons(Dialog *d, int y, const int *ids, const char *const *labels,
                        int n)
{
    int i, total = 0, x, gap = 2;
    for (i = 0; i < n; i++)
        total += str_width(labels[i], 1) + 4 + (i ? gap : 0);
    if (total > d->w - 2) {         /* narrow screen: tighter spacing */
        total -= n - 1;
        gap = 1;
    }
    x = (d->w - total) / 2;
    if (x < 1)
        x = 1;
    for (i = 0; i < n; i++) {
        Widget *b = dlg_add(d, W_BUTTON, ids[i], x, y, 0, labels[i]);
        x += b->w + gap;
    }
}

static Widget *dlg_find(Dialog *d, int id)
{
    int i;
    for (i = 0; i < d->n; i++)
        if (d->wd[i].id == id)
            return &d->wd[i];
    return NULL;
}

static void field_set(Widget *f, const char *s)
{
    copy_str(f->text, sizeof f->text, s);
    f->len = f->cur = strlen(f->text);
}

static void field_insert(Widget *f, const char *s, size_t n)
{
    size_t i, m = 0;
    char clean[FIELD_MAX];
    for (i = 0; i < n && m < sizeof clean; i++)
        if (s[i] != '\n' && s[i] != '\r')
            clean[m++] = s[i];
    if (f->len + m >= sizeof f->text)
        return;
    memmove(f->text + f->cur + m, f->text + f->cur, f->len - f->cur + 1);
    memcpy(f->text + f->cur, clean, m);
    f->len += m;
    f->cur += m;
}

static int field_key(Widget *f, SDL_Keycode k, int ctrl)
{
    size_t p;
    switch (k) {
    case SDLK_BACKSPACE:
        if (ctrl) {
            memmove(f->text, f->text + f->cur, f->len - f->cur + 1);
            f->len -= f->cur;
            f->cur = 0;
        } else if (f->cur > 0) {
            p = utf8_prev(f->text, f->cur);
            memmove(f->text + p, f->text + f->cur, f->len - f->cur + 1);
            f->len -= f->cur - p;
            f->cur = p;
        }
        return 1;
    case SDLK_DELETE:
        if (f->cur < f->len) {
            p = utf8_next(f->text, f->len, f->cur);
            memmove(f->text + f->cur, f->text + p, f->len - p + 1);
            f->len -= p - f->cur;
        }
        return 1;
    case SDLK_LEFT:
        f->cur = utf8_prev(f->text, f->cur);
        return 1;
    case SDLK_RIGHT:
        f->cur = utf8_next(f->text, f->len, f->cur);
        return 1;
    case SDLK_HOME:
        f->cur = 0;
        return 1;
    case SDLK_END:
        f->cur = f->len;
        return 1;
    }
    return 0;
}

static int cmp_items(const void *pa, const void *pb)
{
    const char *x = *(const char *const *)pa, *y = *(const char *const *)pb;
    size_t lx = strlen(x), ly = strlen(y);
    int dx = x[lx - 1] == '/', dy = y[ly - 1] == '/';
    if (strcmp(x, "../") == 0)
        return -1;
    if (strcmp(y, "../") == 0)
        return 1;
    if (dx != dy)
        return dy - dx;
    for (; *x && *y; x++, y++) {
        int a = (unsigned char)*x, b = (unsigned char)*y;
        if (a >= 'A' && a <= 'Z')
            a += 32;
        if (b >= 'A' && b <= 'Z')
            b += 32;
        if (a != b)
            return a - b;
    }
    return (unsigned char)*x - (unsigned char)*y;
}

static void load_dir(Dialog *d)
{
    DIR *dir;
    struct dirent *e;
    int cap = 64;

    free_items(d);
    d->items = (char **)malloc((size_t)cap * sizeof(char *));
    d->sel = 0;
    d->scroll = 0;
    dir = opendir(d->dir);
    if (!dir) {
        copy_str(d->msg, sizeof d->msg, "Cannot read this directory.");
        return;
    }
    while ((e = readdir(dir)) != NULL) {
        char full[4096 + 256];
        struct stat st;
        int is_dir;
        size_t n;
        if (strcmp(e->d_name, ".") == 0)
            continue;
        if (strcmp(e->d_name, "..") == 0 && strcmp(d->dir, "/") == 0)
            continue;
        if (e->d_name[0] == '.' && strcmp(e->d_name, "..") != 0)
            continue;
        copy_str(full, sizeof full, d->dir);
        cat_str(full, sizeof full, "/");
        cat_str(full, sizeof full, e->d_name);
        is_dir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        if (d->nitems == cap) {
            cap *= 2;
            d->items = (char **)realloc(d->items, (size_t)cap * sizeof(char *));
        }
        n = strlen(e->d_name);
        d->items[d->nitems] = (char *)malloc(n + 2);
        memcpy(d->items[d->nitems], e->d_name, n);
        d->items[d->nitems][n] = is_dir ? '/' : 0;
        d->items[d->nitems][n + 1] = 0;
        d->nitems++;
    }
    closedir(dir);
    qsort(d->items, (size_t)d->nitems, sizeof(char *), cmp_items);
}

static void file_dialog(App *a, int save)
{
    Dialog *d;
    int w = a->scr.cols - 8, h = a->scr.rows - 4;
    static const int ids[] = {ID_OK, ID_CANCEL};
    const char *labels[2];
    Widget *f;

    if (w > 70)
        w = 70;
    if (h > 24)
        h = 24;
    if (w < 30)
        w = 30;
    if (h < 12)
        h = 12;
    dlg_begin(a, save ? DLG_SAVEAS : DLG_OPEN, save ? "Save As" : "Open", w, h);
    d = &a->dlg;
    labels[0] = save ? "&Save" : "&Open";
    labels[1] = "Cancel";
    dlg_add(d, W_LABEL, ID_DIR, 2, 1, w - 4, "");
    dlg_add(d, W_LABEL, ID_NONE, 2, 3, 10, "File name:");
    f = dlg_add(d, W_FIELD, ID_NAME, 13, 3, w - 15, NULL);
    d->list_x = 2;
    d->list_y = 5;
    d->list_w = w - 4;
    d->list_h = h - 10;
    dlg_buttons(d, h - 3, ids, labels, 2);
    d->def_id = ID_OK;
    d->focus = 2;

    /* start in the current file's directory */
    if (a->ed.path) {
        char *rp = realpath(a->ed.path, NULL), *slash;
        copy_str(d->dir, sizeof d->dir, rp ? rp : a->ed.path);
        free(rp);
        slash = strrchr(d->dir, '/');
        if (slash && slash != d->dir)
            *slash = 0;
        else if (slash)
            slash[1] = 0;
        else if (!getcwd(d->dir, sizeof d->dir))
            copy_str(d->dir, sizeof d->dir, ".");
        if (save)
            field_set(f, ed_name(&a->ed));
    } else if (!getcwd(d->dir, sizeof d->dir)) {
        copy_str(d->dir, sizeof d->dir, ".");
    }
    load_dir(d);
}

static void message_dialog(App *a, const char *title, const char *l1,
                           const char *l2)
{
    int w = 40, w1 = str_width(l1, 0) + 6, w2 = l2 ? str_width(l2, 0) + 6 : 0;
    static const int ids[] = {ID_OK};
    static const char *const labels[] = {"OK"};
    Dialog *d;
    if (w1 > w)
        w = w1;
    if (w2 > w)
        w = w2;
    if (w > a->scr.cols - 4)
        w = a->scr.cols - 4;
    dlg_begin(a, DLG_MESSAGE, title, w, l2 ? 8 : 7);
    d = &a->dlg;
    dlg_add(d, W_LABEL, ID_NONE, 3, 2, w - 6, l1);
    if (l2)
        dlg_add(d, W_LABEL, ID_NONE, 3, 3, w - 6, l2);
    dlg_buttons(d, d->h - 3, ids, labels, 1);
    d->def_id = ID_OK;
}

static void help_dialog(App *a)
{
    int i, w = 4, h;
    static const int ids[] = {ID_OK};
    static const char *const labels[] = {"OK"};
    Dialog *d;
    for (i = 0; help_lines[i]; i++)
        if (str_width(help_lines[i], 0) + 6 > w)
            w = str_width(help_lines[i], 0) + 6;
    /* scrollable when the screen is too small for all of it */
    h = i + 6;
    if (h > a->scr.rows - 2)
        h = a->scr.rows - 2;
    if (w > a->scr.cols - 4)
        w = a->scr.cols - 4;
    dlg_begin(a, DLG_HELP, "Keyboard", w, h);
    d = &a->dlg;
    d->nitems = i;      /* lines; items stays NULL */
    d->list_h = h - 6;
    dlg_buttons(d, d->h - 3, ids, labels, 1);
    d->def_id = ID_OK;
}

static void confirm_dialog(App *a)
{
    char line[300];
    static const int ids[] = {ID_YES, ID_NO, ID_CANCEL};
    static const char *const labels[] = {"&Yes", "&No", "Cancel"};
    int w;
    Dialog *d;

    copy_str(line, sizeof line, "Save changes to ");
    cat_str(line, sizeof line, ed_name(&a->ed));
    cat_str(line, sizeof line, "?");
    w = str_width(line, 0) + 8;
    if (w < 40)
        w = 40;
    dlg_begin(a, DLG_CONFIRM, "cedit", w, 7);
    d = &a->dlg;
    dlg_add(d, W_LABEL, ID_NONE, (w - str_width(line, 0)) / 2, 2, w - 4, line);
    dlg_buttons(d, 4, ids, labels, 3);
    d->def_id = ID_YES;
}

static void find_dialog(App *a, int replace)
{
    Dialog *d;
    Widget *f;
    int w = a->scr.cols - 4 < 60 ? a->scr.cols - 4 : 60, y;
    static const int fids[] = {ID_FINDNEXT, ID_CANCEL};
    static const char *const flabels[] = {"Find &Next", "Cancel"};
    static const int rids[] = {ID_FINDNEXT, ID_REPLACE, ID_REPLALL, ID_CANCEL};
    static const char *const rlabels[] = {"Find &Next", "&Replace", "Replace &All", "Close"};
    long sy, ey;
    size_t sx, ex;

    dlg_begin(a, replace ? DLG_REPLACE : DLG_FIND, replace ? "Replace" : "Find",
              w, replace ? 9 : 8);
    d = &a->dlg;
    d->at_bottom = 1;
    dlg_add(d, W_LABEL, ID_NONE, 2, 1, 13, "Find what:");
    f = dlg_add(d, W_FIELD, ID_FIND, 16, 1, w - 18, NULL);
    field_set(f, a->ed.find);
    /* a short single-line selection becomes the search text */
    if (ed_sel_range(&a->ed, &sy, &sx, &ey, &ex) && sy == ey && ex - sx < 200) {
        size_t len;
        const char *l = ed_line(&a->ed, sy, &len);
        char t[256];
        memcpy(t, l + sx, ex - sx);
        t[ex - sx] = 0;
        field_set(f, t);
    }
    y = 3;
    if (replace) {
        dlg_add(d, W_LABEL, ID_NONE, 2, 2, 13, "Replace with:");
        field_set(dlg_add(d, W_FIELD, ID_REPL, 16, 2, w - 18, NULL), a->ed.repl);
        y = 4;
    }
    dlg_add(d, W_CHECK, ID_CASE, 2, y, 20, "Match &case")->checked = !a->ed.icase;
    if (replace)
        dlg_buttons(d, d->h - 3, rids, rlabels, 4);
    else
        dlg_buttons(d, d->h - 3, fids, flabels, 2);
    d->def_id = ID_FINDNEXT;
    d->focus = 1;
}

static void goto_dialog(App *a)
{
    Dialog *d;
    Widget *f;
    char num[32];
    static const int ids[] = {ID_OK, ID_CANCEL};
    static const char *const labels[] = {"OK", "Cancel"};

    dlg_begin(a, DLG_GOTO, "Go to Line", 36, 7);
    d = &a->dlg;
    dlg_add(d, W_LABEL, ID_NONE, 3, 2, 13, "Line number:");
    f = dlg_add(d, W_FIELD, ID_LINE, 17, 2, 14, NULL);
    sprintf(num, "%ld", a->ed.cy + 1);
    field_set(f, num);
    dlg_buttons(d, 4, ids, labels, 2);
    d->def_id = ID_OK;
    d->focus = 1;
}

/* ------------------------------------------------------------------ */
/* file operations and pending actions                                 */
/* ------------------------------------------------------------------ */

static void do_open_path(App *a, const char *path)
{
    char err[256];
    int r = ed_open(&a->ed, path, err, sizeof err);
    if (r < 0) {
        message_dialog(a, "Error", path, err);
        return;
    }
    set_msg(a, r == 1 ? "New file: " : "Opened ", ed_name(&a->ed));
}

static int do_save_path(App *a, const char *path)
{
    char err[256], info[64];
    if (ed_save(&a->ed, path, err, sizeof err) < 0) {
        message_dialog(a, "Cannot save", path, err);
        return -1;
    }
    sprintf(info, " (%ld lines)", ed_lines(&a->ed));
    set_msg(a, "Saved ", ed_name(&a->ed));
    cat_str(a->msg, sizeof a->msg, info);
    return 0;
}

static void run_pending(App *a)
{
    int p = a->pending;
    a->pending = P_NONE;
    switch (p) {
    case P_QUIT:
        a->running = 0;
        break;
    case P_NEW:
        ed_new(&a->ed);
        break;
    case P_OPEN:
        file_dialog(a, 0);
        break;
    case P_OPEN_PATH:
        do_open_path(a, a->pending_path);
        break;
    }
}

/* Runs an action that would discard the buffer, asking to save first. */
static void guard(App *a, int action, const char *path)
{
    a->pending = action;
    copy_str(a->pending_path, sizeof a->pending_path, path ? path : "");
    if (ed_modified(&a->ed))
        confirm_dialog(a);
    else
        run_pending(a);
}

static void do_save(App *a)
{
    if (!a->ed.path)
        file_dialog(a, 1);
    else
        do_save_path(a, a->ed.path);
}

static void file_accept(App *a)
{
    Dialog *d = &a->dlg;
    Widget *f = dlg_find(d, ID_NAME);
    char path[4096 + FIELD_MAX];
    struct stat st;
    int save = d->kind == DLG_SAVEAS, exists;

    if (!f->len && d->sel < d->nitems)
        field_set(f, d->items[d->sel]);
    if (!f->len)
        return;
    if (f->text[0] == '/') {
        copy_str(path, sizeof path, f->text);
    } else if (f->text[0] == '~' && (f->text[1] == '/' || !f->text[1]) && getenv("HOME")) {
        copy_str(path, sizeof path, getenv("HOME"));
        cat_str(path, sizeof path, f->text + 1);
    } else {
        copy_str(path, sizeof path, d->dir);
        if (strcmp(d->dir, "/") != 0)
            cat_str(path, sizeof path, "/");
        cat_str(path, sizeof path, f->text);
    }
    exists = stat(path, &st) == 0;
    if (exists && S_ISDIR(st.st_mode)) {
        char *rp = realpath(path, NULL);
        copy_str(d->dir, sizeof d->dir, rp ? rp : path);
        free(rp);
        field_set(f, "");
        d->msg[0] = 0;
        load_dir(d);
        return;
    }
    if (!save) {
        if (!exists) {
            copy_str(d->msg, sizeof d->msg, "File not found.");
            return;
        }
        dlg_close(a);
        do_open_path(a, path);
        return;
    }
    if (exists && strcmp(d->overwrite, path) != 0) {
        copy_str(d->msg, sizeof d->msg, "File exists. Press Enter again to replace it.");
        copy_str(d->overwrite, sizeof d->overwrite, path);
        return;
    }
    dlg_close(a);
    if (do_save_path(a, path) == 0 && a->pending)
        run_pending(a);
    else
        a->pending = P_NONE;
}

static void do_find(App *a, int backward)
{
    int r, tx, ty, tw, th, g;
    if (!a->ed.find[0]) {
        find_dialog(a, 0);
        return;
    }
    finish_loading(a);
    r = ed_find(&a->ed, backward);
    if (!r) {
        set_msg(a, "Not found: ", a->ed.find);
        return;
    }
    if (r == 2)
        set_msg(a, backward ? "Search wrapped to the end" : "Search wrapped to the top", NULL);
    /* keep matches in the upper part, above a find/replace dialog */
    text_area(a, &tx, &ty, &tw, &th, &g);
    a->ed.view_h = th;
    a->ed.view_w = tw;
    ed_scroll_to_cursor(&a->ed, now_ms());
    if (a->ed.cy < a->ed.top || a->ed.cy > a->ed.top + th / 2) {
        a->ed.top = a->ed.cy - th / 3;
        if (a->ed.top < 0)
            a->ed.top = 0;
    }
}

static void dlg_activate(App *a, int id)
{
    Dialog *d = &a->dlg;
    Widget *w;

    switch (d->kind) {
    case DLG_OPEN:
    case DLG_SAVEAS:
        if (id == ID_CANCEL) {
            a->pending = P_NONE;
            dlg_close(a);
        } else {
            file_accept(a);
        }
        break;
    case DLG_FIND:
    case DLG_REPLACE:
        if (id == ID_CANCEL) {
            dlg_close(a);
            break;
        }
        copy_str(a->ed.find, sizeof a->ed.find, dlg_find(d, ID_FIND)->text);
        a->ed.icase = !dlg_find(d, ID_CASE)->checked;
        if ((w = dlg_find(d, ID_REPL)) != NULL)
            copy_str(a->ed.repl, sizeof a->ed.repl, w->text);
        if (!a->ed.find[0])
            break;
        if (d->kind == DLG_FIND)
            dlg_close(a);
        if (id == ID_REPLACE) {
            finish_loading(a);
            if (!ed_replace(&a->ed))
                set_msg(a, "No more matches for ", a->ed.find);
        } else if (id == ID_REPLALL) {
            char num[64];
            long n;
            finish_loading(a);
            n = ed_replace_all(&a->ed);
            sprintf(num, "Replaced %ld occurrence%s", n, n == 1 ? "" : "s");
            set_msg(a, num, NULL);
        } else {
            do_find(a, 0);
        }
        break;
    case DLG_GOTO:
        if (id == ID_OK) {
            long n = atol(dlg_find(d, ID_LINE)->text);
            dlg_close(a);
            if (n > 0)
                ed_goto(&a->ed, n);
        } else {
            dlg_close(a);
        }
        break;
    case DLG_CONFIRM:
        dlg_close(a);
        if (id == ID_YES) {
            if (!a->ed.path)
                file_dialog(a, 1);
            else if (do_save_path(a, a->ed.path) == 0)
                run_pending(a);
            else
                a->pending = P_NONE;
        } else if (id == ID_NO) {
            run_pending(a);
        } else {
            a->pending = P_NONE;
        }
        break;
    default:
        dlg_close(a);
    }
}

/* ------------------------------------------------------------------ */
/* commands                                                            */
/* ------------------------------------------------------------------ */

static void do_copy(App *a, int cut)
{
    size_t n;
    char *t = ed_copy(&a->ed, &n);
    if (!t)
        return;
    SDL_SetClipboardText(t);
    free(t);
    if (cut)
        ed_cut(&a->ed);
}

static void do_paste(App *a)
{
    char *t = SDL_GetClipboardText();
    if (t && *t)
        ed_paste(&a->ed, t, strlen(t));
    SDL_free(t);
}

static void apply_theme(App *a)
{
    T = a->dark ? &theme_dark : &theme_light;
    a->scr.cur_bg = T->cursor_bg;
    a->scr.cur_ink = T->cursor_ink;
    a->scr.cur_box = T->cursor_box;
    a->scr.full = 1;
}

/* Stores the current settings in the config file. */
static void save_settings(App *a)
{
    Config c;
    c.size = a->scr.size;
    c.dark = a->dark;
    c.autoindent = a->ed.autoindent;
    c.line_numbers = a->show_lnum;
    c.tab_width = a->ed.tabw;
    config_save(&c);
}

static void set_size(App *a, int size)
{
    screen_set_size(&a->scr, size);
    set_pointer(a, PTR_ARROW);
    save_settings(a);
}

static int cmd_enabled(App *a, int cmd)
{
    long sy, ey;
    size_t sx, ex;
    switch (cmd) {
    case CMD_UNDO:
        return a->ed.undo.pos > 0;
    case CMD_REDO:
        return a->ed.undo.pos < a->ed.undo.n;
    case CMD_CUT:
    case CMD_COPY:
    case CMD_DELETE:
        return ed_sel_range(&a->ed, &sy, &sx, &ey, &ex);
    case CMD_PASTE:
        return SDL_HasClipboardText();
    case CMD_FINDNEXT:
    case CMD_FINDPREV:
        return a->ed.find[0] != 0;
    }
    return 1;
}

static int cmd_checked(App *a, int cmd)
{
    switch (cmd) {
    case CMD_OVERWRITE:
        return a->ed.overwrite;
    case CMD_SIZE_S:
        return a->scr.size == SIZE_SMALL;
    case CMD_SIZE_N:
        return a->scr.size == SIZE_NORMAL;
    case CMD_SIZE_L:
        return a->scr.size == SIZE_LARGE;
    case CMD_LINENUM:
        return a->show_lnum;
    case CMD_TAB4:
        return a->ed.tabw == 4;
    case CMD_TAB8:
        return a->ed.tabw == 8;
    case CMD_DARK:
        return a->dark;
    case CMD_AUTOINDENT:
        return a->ed.autoindent;
    }
    return 0;
}

static void command(App *a, int cmd)
{
    unsigned long t = now_ms();
    wake_cursor(a);
    switch (cmd) {
    case CMD_NEW:       guard(a, P_NEW, NULL); break;
    case CMD_OPEN:      guard(a, P_OPEN, NULL); break;
    case CMD_SAVE:      do_save(a); break;
    case CMD_SAVEAS:    file_dialog(a, 1); break;
    case CMD_EXIT:      guard(a, P_QUIT, NULL); break;
    case CMD_UNDO:
        if (!ed_undo(&a->ed))
            set_msg(a, "Nothing to undo", NULL);
        break;
    case CMD_REDO:
        if (!ed_redo(&a->ed))
            set_msg(a, "Nothing to redo", NULL);
        break;
    case CMD_CUT:       do_copy(a, 1); break;
    case CMD_COPY:      do_copy(a, 0); break;
    case CMD_PASTE:     do_paste(a); break;
    case CMD_DELETE:    ed_delete(&a->ed, 0, t); break;
    case CMD_SELALL:    finish_loading(a); ed_select_all(&a->ed); break;
    case CMD_OVERWRITE: a->ed.overwrite = !a->ed.overwrite; break;
    case CMD_FIND:      find_dialog(a, 0); break;
    case CMD_FINDNEXT:  do_find(a, 0); break;
    case CMD_FINDPREV:  do_find(a, 1); break;
    case CMD_REPLACE:   find_dialog(a, 1); break;
    case CMD_GOTO:      goto_dialog(a); break;
    case CMD_SIZE_S:    set_size(a, SIZE_SMALL); break;
    case CMD_SIZE_N:    set_size(a, SIZE_NORMAL); break;
    case CMD_SIZE_L:    set_size(a, SIZE_LARGE); break;
    case CMD_LINENUM:
        a->show_lnum = !a->show_lnum;
        a->ed.follow = 1;
        save_settings(a);
        break;
    case CMD_TAB4:
    case CMD_TAB8:
        a->ed.tabw = cmd == CMD_TAB4 ? 4 : 8;
        a->ed.follow = 1;
        save_settings(a);
        break;
    case CMD_DARK:
        a->dark = !a->dark;
        apply_theme(a);
        save_settings(a);
        break;
    case CMD_AUTOINDENT:
        a->ed.autoindent = !a->ed.autoindent;
        save_settings(a);
        break;
    case CMD_HELP:      help_dialog(a); break;
    case CMD_ABOUT:
        message_dialog(a, "About", "cedit 0.1 - Classic Text Editor",
                       "C89 + SDL2. Hand-drawn fonts. Public domain spirit.");
        break;
    }
}

/* ------------------------------------------------------------------ */
/* menus                                                               */
/* ------------------------------------------------------------------ */

static int menu_x(int i)
{
    int x = 1, j;
    for (j = 0; j < i; j++)
        x += str_width(menus[j].title, 1) + 2;
    return x;
}

static void menu_geometry(int m, int *x, int *y, int *w, int *h)
{
    int i, lw = 0, kw = 0;
    for (i = 0; i < menus[m].n; i++) {
        const MenuItem *it = &menus[m].items[i];
        if (!it->label)
            continue;
        if (str_width(it->label, 1) > lw)
            lw = str_width(it->label, 1);
        if (str_width(it->keys, 0) > kw)
            kw = str_width(it->keys, 0);
    }
    *x = menu_x(m) - 1;
    *y = 1;
    *w = lw + kw + 7;
    *h = menus[m].n + 2;
}

static void menu_open(App *a, int m)
{
    a->menu = (m + NMENUS) % NMENUS;
    a->menu_item = 0;
}

static void menu_step(App *a, int d)
{
    int n = menus[a->menu].n, i = a->menu_item, k;
    for (k = 0; k < n; k++) {
        i = (i + d + n) % n;
        if (menus[a->menu].items[i].label)
            break;
    }
    a->menu_item = i;
}

static void menu_run(App *a, int item)
{
    const MenuItem *it = &menus[a->menu].items[item];
    if (!it->label || !cmd_enabled(a, it->cmd))
        return;
    a->menu = -1;
    command(a, it->cmd);
}

static void draw_menus(App *a)
{
    Screen *s = &a->scr;
    int i, x, y, w, h, m = a->menu;
    char clock[32];
    time_t t = time(NULL);

    screen_fill(s, 0, 0, s->cols, 1, ' ', T->bar_fg, T->bar_bg);
    for (i = 0; i < NMENUS; i++) {
        int open = i == m;
        x = menu_x(i);
        screen_put(s, x - 1, 0, ' ', T->bar_fg, open ? T->text_bg : T->bar_bg);
        x += put_label(s, x, 0, menus[i].title, open ? T->text_fg : T->bar_fg,
                       open ? T->hot : T->bar_hot, open ? T->text_bg : T->bar_bg);
        screen_put(s, x, 0, ' ', T->bar_fg, open ? T->text_bg : T->bar_bg);
    }
    strftime(clock, sizeof clock, "%a %m/%d %H:%M", localtime(&t));
    screen_puts(s, s->cols - str_width(clock, 0) - 1, 0, clock, T->bar_fg,
                T->bar_bg, 32);

    if (m < 0)
        return;
    menu_geometry(m, &x, &y, &w, &h);
    screen_fill(s, x, y, w, h, ' ', T->text_fg, T->text_bg);
    put_frame(s, x, y, w, h, 0, T->frame, T->text_bg);
    for (i = 0; i < menus[m].n; i++) {
        const MenuItem *it = &menus[m].items[i];
        int sel = i == a->menu_item, en, fg, bg;
        if (!it->label) {
            int k;
            screen_put(s, x, y + 1 + i, 0x251C, T->frame, T->text_bg);
            for (k = 1; k < w - 1; k++)
                screen_put(s, x + k, y + 1 + i, 0x2500, T->frame, T->text_bg);
            screen_put(s, x + w - 1, y + 1 + i, 0x2524, T->frame, T->text_bg);
            continue;
        }
        en = cmd_enabled(a, it->cmd);
        fg = !en ? T->disabled : sel ? T->sel_fg : T->text_fg;
        bg = sel ? T->sel_bg : T->text_bg;
        screen_fill(s, x + 1, y + 1 + i, w - 2, 1, ' ', fg, bg);
        if (cmd_checked(a, it->cmd))
            screen_put(s, x + 2, y + 1 + i, 0x2713, sel ? T->sel_hot : T->hot, bg);
        put_label(s, x + 3, y + 1 + i, it->label, fg,
                  !en ? T->disabled : sel ? T->sel_hot : T->hot, bg);
        screen_puts(s, x + w - 2 - str_width(it->keys, 0), y + 1 + i, it->keys,
                    fg, bg, 20);
    }
    screen_shadow(s, x + w, y + 1, 2, h);
    screen_shadow(s, x + 2, y + h, w - 2, 1);
}

static int menu_hit(App *a, int cx, int cy, int *item)
{
    int x, y, w, h;
    if (a->menu < 0)
        return 0;
    menu_geometry(a->menu, &x, &y, &w, &h);
    if (cx > x && cx < x + w - 1 && cy > y && cy < y + h - 1) {
        *item = cy - y - 1;
        return 1;
    }
    return 0;
}

static int menubar_hit(int cx)
{
    int i;
    for (i = 0; i < NMENUS; i++) {
        int x = menu_x(i);
        if (cx >= x - 1 && cx <= x + str_width(menus[i].title, 1))
            return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */

static void draw_text(App *a, int tx, int ty, int tw, int th, int gutter)
{
    Screen *s = &a->scr;
    Editor *ed = &a->ed;
    long nlines = ed_lines(ed), row, sy = 0, ey = 0;
    size_t sx = 0, ex = 0;
    int have_sel = ed_sel_range(ed, &sy, &sx, &ey, &ex);

    for (row = 0; row < th; row++) {
        long ln = ed->top + row, d = 0;
        size_t len, i = 0;
        const char *l;
        int y = ty + (int)row;

        if (ln >= nlines)
            break;
        if (gutter) {
            char num[32];
            int n = sprintf(num, "%ld", ln + 1);
            screen_puts(s, tx - 1 - n, y, num, ln == ed->cy ? T->cur_lnum : T->lnum,
                        T->text_bg, n);
        }
        l = ed_line(ed, ln, &len);
        while (i < len && d < ed->left + tw) {
            unsigned long cp;
            size_t k;
            long width, c;
            int fg, bg, sel = have_sel && (ln > sy || (ln == sy && i >= sx)) &&
                                  (ln < ey || (ln == ey && i < ex));
            if (l[i] == '\t') {
                width = ed->tabw - d % ed->tabw;
                cp = ' ';
                k = 1;
            } else {
                k = utf8_decode(l + i, len - i, &cp);
                width = 1;
            }
            fg = sel ? T->sel_fg : T->text_fg;
            bg = sel ? T->sel_bg : T->text_bg;
            if (cp < 0x20 || cp == 0x7F) {
                /* control characters: inverse ^X letter */
                cp = cp == 0x7F ? '?' : cp + '@';
                fg = T->special_fg;
                bg = sel ? T->special_sel_bg : T->special_bg;
            } else if (cp == 0xFFFD || !font_has(s->font, cp)) {
                fg = sel ? T->sel_hot : T->bad;
            }
            for (c = 0; c < width; c++) {
                long dc = d + c;
                if (dc >= ed->left && dc < ed->left + tw)
                    screen_put(s, tx + (int)(dc - ed->left), y, c ? ' ' : cp, fg, bg);
            }
            d += width;
            i += k;
        }
        /* selected line break */
        if (have_sel && ln >= sy && ln < ey && i >= len && d >= ed->left &&
            d < ed->left + tw && (ln > sy || len >= sx))
            screen_put(s, tx + (int)(d - ed->left), y, ' ', T->sel_fg, T->sel_bg);
    }

    /* text cursor */
    if (a->dlg.kind == DLG_NONE && a->menu < 0 && (a->blink_on || !a->focused) &&
        ed->cy >= ed->top && ed->cy < ed->top + th) {
        size_t len;
        const char *l = ed_line(ed, ed->cy, &len);
        long dc = ed_disp_col(ed, l, len, ed->cx) - ed->left;
        if (dc >= 0 && dc < tw)
            screen_cursor(s, tx + (int)dc, ty + (int)(ed->cy - ed->top),
                          ed->overwrite ? TCUR_OVERWRITE : TCUR_INSERT);
    }
}

static void scrollbar_geometry(App *a, int th, int *track, int *tpos, int *tlen)
{
    long total = ed_lines(&a->ed), range = total - th;
    *track = th - 2;
    if (*track < 1)
        *track = 1;
    *tlen = total > th ? (int)((long)*track * th / total) : *track;
    if (*tlen < 1)
        *tlen = 1;
    *tpos = range > 0 ? (int)((*track - *tlen) * a->ed.top / range) : 0;
    if (*tpos > *track - *tlen)
        *tpos = *track - *tlen;
}

static void draw_window(App *a)
{
    Screen *s = &a->scr;
    Editor *ed = &a->ed;
    int tx, ty, tw, th, g, i, x, track, tpos, tlen;
    char title[300], pos[96];
    int bottom = s->rows - 2;

    text_area(a, &tx, &ty, &tw, &th, &g);
    screen_fill(s, 0, 1, s->cols, s->rows - 2, ' ', T->text_fg, T->text_bg);
    put_frame(s, 0, 1, s->cols, s->rows - 2, 1, T->frame, T->text_bg);

    /* title, inverse, centered on the top border */
    copy_str(title, sizeof title, " ");
    cat_str(title, sizeof title, ed_name(ed));
    cat_str(title, sizeof title, ed_modified(ed) ? " * " : " ");
    i = str_width(title, 0);
    if (i > s->cols - 4)
        i = s->cols - 4;
    screen_puts(s, (s->cols - i) / 2, 1, title, T->title_fg, T->title_bg, i);

    /* scrollbar on the right border */
    scrollbar_geometry(a, th, &track, &tpos, &tlen);
    screen_put(s, s->cols - 1, ty, 0x25B2, T->frame, T->text_bg);
    screen_put(s, s->cols - 1, ty + th - 1, 0x25BC, T->frame, T->text_bg);
    for (i = 0; i < track && th > 2; i++)
        screen_put(s, s->cols - 1, ty + 1 + i,
                   i >= tpos && i < tpos + tlen ? 0x2588 : 0x2591, T->frame, T->text_bg);

    /* status in the bottom border, TempleOS style */
    {
        size_t len;
        const char *l = ed_line(ed, ed->cy, &len);
        long col = ed_disp_col(ed, l, len, ed->cx) + 1;
        sprintf(pos, " Line:%04ld/%04ld%s Col:%03ld ", ed->cy + 1, ed_lines(ed),
                buf_loading(ed->buf) ? "+" : "", col);
    }
    i = str_width(pos, 0);
    screen_puts(s, s->cols - 2 - i, bottom, pos, T->frame, T->text_bg, i);
    x = 2;
    x += screen_puts(s, x, bottom, ed->overwrite ? " OVR " : " INS ", T->frame, T->text_bg, 5);
    x++;
    x += screen_puts(s, x, bottom, ed->buf->crlf ? " CRLF " : " LF ", T->frame, T->text_bg, 6);
    x++;
    screen_puts(s, x, bottom, " UTF-8 ", T->frame, T->text_bg, 7);

    draw_text(a, tx, ty, tw, th, g);
}

static void draw_status(App *a)
{
    Screen *s = &a->scr;
    int y = s->rows - 1, x = 1;
    static const char *const hints[] = {
        "F1", "Help", "F10", "Menu", "^O", "Open", "^S", "Save",
        "^F", "Find", "^Z", "Undo", "^Q", "Quit", NULL
    };

    screen_fill(s, 0, y, s->cols, 1, ' ', T->bar_fg, T->bar_bg);
    if (a->msg[0] && now_ms() < a->msg_until) {
        screen_puts(s, 1, y, a->msg, T->bar_fg, T->bar_bg, s->cols - 2);
    } else {
        int i;
        for (i = 0; hints[i] && x < s->cols - 20; i += 2) {
            x += screen_puts(s, x, y, hints[i], T->bar_hot, T->bar_bg, 8);
            x += screen_puts(s, x + 1, y, hints[i + 1], T->bar_fg, T->bar_bg, 8) + 3;
        }
    }
    if (buf_loading(a->ed.buf)) {
        char p[32];
        int n = sprintf(p, "Loading %d%% ", (int)(buf_load_progress(a->ed.buf) * 100));
        screen_puts(s, s->cols - n - 1, y, p, T->bar_hot, T->bar_bg, n);
    }
}

static void dlg_place(App *a)
{
    Dialog *d = &a->dlg;
    Screen *s = &a->scr;
    if (d->w > s->cols - 2)
        d->w = s->cols - 2;
    d->x = (s->cols - d->w) / 2;
    d->y = d->at_bottom ? s->rows - d->h - 2 : (s->rows - d->h) / 2;
    if (d->y < 1)
        d->y = 1;
}

static void draw_field(App *a, Widget *f, int x, int y, int focused)
{
    Screen *s = &a->scr;
    size_t i = 0, start = 0;
    int ci = 0, col = 0, k = 0;
    int bg = focused ? T->focus_bg : T->field_bg;
    int fg = focused ? T->focus_fg : T->field_fg;

    /* character index of the cursor, then scroll to keep it visible */
    while (i < f->cur) {
        i = utf8_next(f->text, f->len, i);
        ci++;
    }
    i = 0;
    while (ci - k >= f->w) {
        i = utf8_next(f->text, f->len, i);
        k++;
    }
    start = i;
    screen_fill(s, x, y, f->w, 1, ' ', fg, bg);
    for (i = start; i < f->len && col < f->w;) {
        unsigned long cp;
        i += utf8_decode(f->text + i, f->len - i, &cp);
        screen_put(s, x + col++, y, cp, fg, bg);
    }
    if (focused && a->blink_on)
        screen_cursor(s, x + ci - k, y, TCUR_INSERT);
}

static void draw_dialog(App *a)
{
    Screen *s = &a->scr;
    Dialog *d = &a->dlg;
    int i, tw;

    if (d->kind == DLG_NONE)
        return;
    dlg_place(a);
    screen_fill(s, d->x, d->y, d->w, d->h, ' ', T->text_fg, T->text_bg);
    put_frame(s, d->x, d->y, d->w, d->h, 1, T->frame, T->text_bg);
    {
        char t[80];
        copy_str(t, sizeof t, " ");
        cat_str(t, sizeof t, d->title);
        cat_str(t, sizeof t, " ");
        tw = str_width(t, 0);
        screen_puts(s, d->x + (d->w - tw) / 2, d->y, t, T->title_fg, T->title_bg, tw);
    }
    screen_shadow(s, d->x + d->w, d->y + 1, 2, d->h);
    screen_shadow(s, d->x + 2, d->y + d->h, d->w - 2, 1);

    for (i = 0; i < d->n; i++) {
        Widget *w = &d->wd[i];
        int x = d->x + w->x, y = d->y + w->y, foc = i == d->focus;
        switch (w->kind) {
        case W_LABEL:
            if (w->id == ID_DIR) {
                /* show the tail of long directory paths */
                const char *p = d->dir;
                int over = str_width(p, 0) - (w->w - 5);
                while (over-- > 0 && *p)
                    p++;
                screen_puts(s, x, y, "Dir: ", T->lnum, T->text_bg, 5);
                screen_puts(s, x + 5, y, p, T->text_fg, T->text_bg, w->w - 5);
            } else {
                /* clip to the dialog interior */
                char clip[128];
                int room = d->x + d->w - 1 - x, k = 0;
                size_t j = 0, len = strlen(w->label);
                while (j < len && k < room) {
                    j = utf8_next(w->label, len, j);
                    k++;
                }
                memcpy(clip, w->label, j);
                clip[j] = 0;
                put_label(s, x, y, clip, T->text_fg, T->hot, T->text_bg);
            }
            break;
        case W_FIELD:
            draw_field(a, w, x, y, foc);
            break;
        case W_CHECK:
            screen_puts(s, x, y, w->checked ? "[\xe2\x9c\x93]" : "[ ]", foc ? T->focus_fg : T->text_fg,
                        foc ? T->focus_bg : T->text_bg, 3);
            put_label(s, x + 4, y, w->label, foc ? T->focus_fg : T->text_fg, T->hot,
                      foc ? T->focus_bg : T->text_bg);
            break;
        case W_BUTTON:
            screen_put(s, x, y, '[', foc ? T->sel_fg : T->text_fg, foc ? T->sel_bg : T->text_bg);
            screen_put(s, x + 1, y, ' ', T->text_fg, foc ? T->sel_bg : T->text_bg);
            put_label(s, x + 2, y, w->label, foc ? T->sel_fg : T->text_fg,
                      foc ? T->sel_hot : T->hot, foc ? T->sel_bg : T->text_bg);
            screen_put(s, x + w->w - 2, y, ' ', T->text_fg, foc ? T->sel_bg : T->text_bg);
            screen_put(s, x + w->w - 1, y, ']', foc ? T->sel_fg : T->text_fg, foc ? T->sel_bg : T->text_bg);
            break;
        }
    }

    if (d->kind == DLG_HELP) {
        int r;
        for (r = 0; r < d->list_h && d->scroll + r < d->nitems; r++)
            screen_puts(s, d->x + 3, d->y + 2 + r, help_lines[d->scroll + r],
                        T->text_fg, T->text_bg, d->w - 6);
        if (d->scroll > 0)
            screen_put(s, d->x + d->w - 2, d->y + 2, 0x25B2, T->frame, T->text_bg);
        if (d->scroll + d->list_h < d->nitems)
            screen_put(s, d->x + d->w - 2, d->y + 1 + d->list_h, 0x25BC,
                       T->frame, T->text_bg);
    }
    if (d->kind == DLG_OPEN || d->kind == DLG_SAVEAS) {
        int lx = d->x + d->list_x, ly = d->y + d->list_y, r;
        put_frame(s, lx, ly, d->list_w, d->list_h + 2, 0, T->frame, T->text_bg);
        if (d->sel < d->scroll)
            d->scroll = d->sel;
        if (d->sel >= d->scroll + d->list_h)
            d->scroll = d->sel - d->list_h + 1;
        for (r = 0; r < d->list_h && d->scroll + r < d->nitems; r++) {
            int idx = d->scroll + r, sel = idx == d->sel;
            const char *name = d->items[idx];
            int is_dir = name[strlen(name) - 1] == '/';
            screen_fill(s, lx + 1, ly + 1 + r, d->list_w - 2, 1, ' ',
                        sel ? T->sel_fg : T->text_fg, sel ? T->sel_bg : T->text_bg);
            screen_puts(s, lx + 2, ly + 1 + r, name,
                        sel ? T->sel_fg : is_dir ? T->dir : T->text_fg,
                        sel ? T->sel_bg : T->text_bg,
                        d->list_w - 4);
        }
        if (d->scroll > 0)
            screen_put(s, lx + d->list_w - 1, ly + 1, 0x25B2, T->frame, T->text_bg);
        if (d->scroll + d->list_h < d->nitems)
            screen_put(s, lx + d->list_w - 1, ly + d->list_h, 0x25BC, T->frame, T->text_bg);
        if (d->msg[0])
            screen_puts(s, d->x + 2, d->y + d->h - 4, d->msg, T->bad, T->text_bg, d->w - 4);
    }
}

void app_draw(App *a)
{
    int tx, ty, tw, th, g;

    text_area(a, &tx, &ty, &tw, &th, &g);
    a->ed.view_w = tw;
    a->ed.view_h = th;
    if (a->ed.follow)
        ed_scroll_to_cursor(&a->ed, now_ms());
    draw_window(a);
    draw_status(a);
    draw_menus(a);
    draw_dialog(a);
    update_title(a);
    screen_present(&a->scr);
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */

static void help_scroll(Dialog *d, int by)
{
    d->scroll += by;
    if (d->scroll > d->nitems - d->list_h)
        d->scroll = d->nitems - d->list_h;
    if (d->scroll < 0)
        d->scroll = 0;
}

static int focusable(const Widget *w)
{
    return w->kind != W_LABEL;
}

static void dlg_focus_step(Dialog *d, int dir)
{
    int i = d->focus, k;
    for (k = 0; k < d->n; k++) {
        i = (i + dir + d->n) % d->n;
        if (focusable(&d->wd[i]))
            break;
    }
    d->focus = i;
}

static void list_select(Dialog *d, int i)
{
    Widget *f = dlg_find(d, ID_NAME);
    if (d->nitems == 0)
        return;
    if (i < 0)
        i = 0;
    if (i >= d->nitems)
        i = d->nitems - 1;
    d->sel = i;
    if (f)
        field_set(f, d->items[i]);
    d->overwrite[0] = 0;
}

static void dlg_key(App *a, const SDL_KeyboardEvent *k)
{
    Dialog *d = &a->dlg;
    Widget *w = d->focus >= 0 ? &d->wd[d->focus] : NULL;
    int ctrl = (k->keysym.mod & KMOD_CTRL) != 0;
    int shift = (k->keysym.mod & KMOD_SHIFT) != 0;
    int alt = (k->keysym.mod & KMOD_ALT) != 0;
    int is_file = d->kind == DLG_OPEN || d->kind == DLG_SAVEAS;
    SDL_Keycode sym = k->keysym.sym;
    int i;

    wake_cursor(a);
    if (sym == SDLK_ESCAPE) {
        dlg_activate(a, ID_CANCEL);
        return;
    }
    if (d->kind == DLG_HELP && (sym == SDLK_UP || sym == SDLK_DOWN ||
                                sym == SDLK_PAGEUP || sym == SDLK_PAGEDOWN ||
                                sym == SDLK_HOME || sym == SDLK_END)) {
        help_scroll(d, sym == SDLK_UP ? -1 : sym == SDLK_DOWN ? 1
                     : sym == SDLK_PAGEUP ? -d->list_h : sym == SDLK_PAGEDOWN ? d->list_h
                     : sym == SDLK_HOME ? -d->nitems : d->nitems);
        return;
    }
    if (sym == SDLK_TAB) {
        dlg_focus_step(d, shift ? -1 : 1);
        return;
    }
    if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
        dlg_activate(a, w && w->kind == W_BUTTON ? w->id : d->def_id);
        return;
    }
    /* Alt+letter (or a plain letter when no field has focus) presses the
     * matching button or toggles the matching checkbox */
    if (alt || (w && w->kind != W_FIELD && sym < 128 && sym > ' ')) {
        for (i = 0; i < d->n; i++) {
            Widget *b = &d->wd[i];
            if ((b->kind == W_BUTTON || b->kind == W_CHECK) &&
                hotkey_of(b->label) == (int)sym) {
                if (b->kind == W_CHECK) {
                    b->checked = !b->checked;
                    d->focus = i;
                } else {
                    dlg_activate(a, b->id);
                }
                return;
            }
        }
    }
    if (is_file && (sym == SDLK_UP || sym == SDLK_DOWN || sym == SDLK_PAGEUP ||
                    sym == SDLK_PAGEDOWN)) {
        int step = sym == SDLK_UP ? -1 : sym == SDLK_DOWN ? 1
                 : sym == SDLK_PAGEUP ? -d->list_h : d->list_h;
        list_select(d, d->sel + step);
        return;
    }
    if (!w)
        return;
    switch (w->kind) {
    case W_FIELD:
        if (ctrl && sym == SDLK_v) {
            char *t = SDL_GetClipboardText();
            if (t)
                field_insert(w, t, strlen(t));
            SDL_free(t);
        } else if (ctrl && sym == SDLK_a) {
            w->cur = w->len;
        } else {
            field_key(w, sym, ctrl);
        }
        if (is_file)
            d->overwrite[0] = 0;
        break;
    case W_CHECK:
        if (sym == SDLK_SPACE)
            w->checked = !w->checked;
        break;
    case W_BUTTON:
        if (sym == SDLK_SPACE)
            dlg_activate(a, w->id);
        else if (sym == SDLK_LEFT || sym == SDLK_UP)
            dlg_focus_step(d, -1);
        else if (sym == SDLK_RIGHT || sym == SDLK_DOWN)
            dlg_focus_step(d, 1);
        break;
    }
}

static void dlg_text(App *a, const char *text)
{
    Dialog *d = &a->dlg;
    Widget *w = d->focus >= 0 ? &d->wd[d->focus] : NULL;
    if (!w || w->kind != W_FIELD) {
        if (d->kind != DLG_OPEN && d->kind != DLG_SAVEAS)
            return;
        w = dlg_find(d, ID_NAME);
        d->focus = (int)(w - d->wd);
    }
    field_insert(w, text, strlen(text));
    d->overwrite[0] = 0;
}

static void dlg_click(App *a, int cx, int cy, int clicks)
{
    Dialog *d = &a->dlg;
    int rx = cx - d->x, ry = cy - d->y, i;

    if (d->kind == DLG_OPEN || d->kind == DLG_SAVEAS) {
        int r = ry - d->list_y - 1;
        if (rx > d->list_x && rx < d->list_x + d->list_w - 1 && r >= 0 &&
            r < d->list_h && d->scroll + r < d->nitems) {
            list_select(d, d->scroll + r);
            if (clicks >= 2)
                file_accept(a);
            return;
        }
    }
    for (i = 0; i < d->n; i++) {
        Widget *w = &d->wd[i];
        int ww = w->kind == W_CHECK ? str_width(w->label, 1) + 4 : w->w;
        if (!focusable(w) || ry != w->y || rx < w->x || rx >= w->x + ww)
            continue;
        d->focus = i;
        if (w->kind == W_BUTTON)
            dlg_activate(a, w->id);
        else if (w->kind == W_CHECK)
            w->checked = !w->checked;
        else if (w->kind == W_FIELD) {
            /* place the cursor near the click */
            size_t p = 0;
            int c = rx - w->x;
            while (c-- > 0 && p < w->len)
                p = utf8_next(w->text, w->len, p);
            w->cur = p;
        }
        return;
    }
}

/* Converts a cell in the text area to a buffer position. */
static void cell_to_pos(App *a, int cx, int cy, long *ln, size_t *col)
{
    int tx, ty, tw, th, g;
    size_t len;
    const char *l;
    text_area(a, &tx, &ty, &tw, &th, &g);
    *ln = a->ed.top + (cy - ty);
    if (*ln < 0)
        *ln = 0;
    if (*ln >= ed_lines(&a->ed))
        *ln = ed_lines(&a->ed) - 1;
    l = ed_line(&a->ed, *ln, &len);
    *col = ed_byte_col(&a->ed, l, len, a->ed.left + (cx - tx));
}

static void scrollbar_click(App *a, int cy, int th, int ty)
{
    int track, tpos, tlen, r = cy - ty - 1;
    scrollbar_geometry(a, th, &track, &tpos, &tlen);
    if (cy == ty)
        ed_scroll(&a->ed, -1);
    else if (cy == ty + th - 1)
        ed_scroll(&a->ed, 1);
    else if (r < tpos)
        ed_scroll(&a->ed, -(th - 1));
    else if (r >= tpos + tlen)
        ed_scroll(&a->ed, th - 1);
    else {
        a->drag = 2;
        a->drag_grab = r - tpos;
    }
}

static void scrollbar_drag(App *a, int cy)
{
    int tx, ty, tw, th, g, track, tpos, tlen;
    long range;
    text_area(a, &tx, &ty, &tw, &th, &g);
    scrollbar_geometry(a, th, &track, &tpos, &tlen);
    range = ed_lines(&a->ed) - th;
    if (range <= 0 || track - tlen <= 0)
        return;
    tpos = cy - ty - 1 - a->drag_grab;
    a->ed.top = (long)((double)tpos * range / (track - tlen) + 0.5);
    ed_scroll(&a->ed, 0);
}

static void mouse_down(App *a, const SDL_MouseButtonEvent *b)
{
    int cx, cy, tx, ty, tw, th, g, item;
    unsigned long t = now_ms();
    SDL_Keymod mod = SDL_GetModState();

    screen_cell_at(&a->scr, b->x, b->y, &cx, &cy);
    if (b->button != SDL_BUTTON_LEFT)
        return;
    if (t - a->click_time < DCLICK_MS && cx == a->click_x && cy == a->click_y)
        a->click_count++;
    else
        a->click_count = 1;
    a->click_time = t;
    a->click_x = cx;
    a->click_y = cy;
    wake_cursor(a);

    if (a->dlg.kind != DLG_NONE) {
        dlg_click(a, cx, cy, a->click_count);
        return;
    }
    if (a->menu >= 0) {
        if (menu_hit(a, cx, cy, &item)) {
            menu_run(a, item);
            return;
        }
        if (cy == 0 && menubar_hit(cx) >= 0 && menubar_hit(cx) != a->menu) {
            menu_open(a, menubar_hit(cx));
            return;
        }
        a->menu = -1;
        return;
    }
    if (cy == 0) {
        if (menubar_hit(cx) >= 0)
            menu_open(a, menubar_hit(cx));
        return;
    }
    text_area(a, &tx, &ty, &tw, &th, &g);
    if (cx == a->scr.cols - 1 && cy >= ty && cy < ty + th) {
        scrollbar_click(a, cy, th, ty);
        return;
    }
    if (cy >= ty && cy < ty + th && cx >= tx - g) {
        long ln;
        size_t col;
        if (cx < tx)
            cx = tx;
        cell_to_pos(a, cx, cy, &ln, &col);
        ed_set_cursor(&a->ed, ln, col, (mod & KMOD_SHIFT) != 0);
        if (a->click_count == 2)
            ed_select_word(&a->ed);
        else if (a->click_count >= 3)
            ed_select_line(&a->ed);
        a->ed.follow = 0;
        a->drag = 1;
    }
}

static void mouse_motion(App *a, const SDL_MouseMotionEvent *m)
{
    int cx, cy, tx, ty, tw, th, g, item;
    screen_pointer(&a->scr, m->x, m->y, 1);
    screen_cell_at(&a->scr, m->x, m->y, &cx, &cy);
    a->mouse_x = cx;
    a->mouse_y = cy;
    text_area(a, &tx, &ty, &tw, &th, &g);

    if (a->drag == 1) {
        long ln;
        size_t col;
        int x = cx < tx ? tx : cx, y = cy < ty ? ty : cy >= ty + th ? ty + th - 1 : cy;
        cell_to_pos(a, x, y, &ln, &col);
        if (a->click_count == 1)
            ed_set_cursor(&a->ed, ln, col, 1);
        a->ed.follow = 1;
        return;
    }
    if (a->drag == 2) {
        scrollbar_drag(a, cy);
        return;
    }
    if (a->menu >= 0) {
        if (menu_hit(a, cx, cy, &item) && menus[a->menu].items[item].label)
            a->menu_item = item;
        else if (cy == 0 && menubar_hit(cx) >= 0 && menubar_hit(cx) != a->menu)
            menu_open(a, menubar_hit(cx));
    }
    set_pointer(a, a->dlg.kind == DLG_NONE && a->menu < 0 && cy >= ty &&
                       cy < ty + th && cx >= tx && cx < tx + tw
                       ? PTR_IBEAM : PTR_ARROW);
}

static void mouse_wheel(App *a, const SDL_MouseWheelEvent *w)
{
    SDL_Keymod mod = SDL_GetModState();
    int dy = w->y, dx = w->x;
    if (w->direction == SDL_MOUSEWHEEL_FLIPPED) {
        dy = -dy;
        dx = -dx;
    }
    if (mod & KMOD_CTRL) {
        if (dy)
            set_size(a, a->scr.size + (dy > 0 ? 1 : -1));
        return;
    }
    if (a->dlg.kind == DLG_HELP) {
        help_scroll(&a->dlg, -dy * 3);
        return;
    }
    if (a->dlg.kind == DLG_OPEN || a->dlg.kind == DLG_SAVEAS) {
        Dialog *d = &a->dlg;
        d->scroll -= dy * 3;
        if (d->scroll > d->nitems - d->list_h)
            d->scroll = d->nitems - d->list_h;
        if (d->scroll < 0)
            d->scroll = 0;
        if (d->sel < d->scroll)
            d->sel = d->scroll;
        if (d->sel >= d->scroll + d->list_h)
            d->sel = d->scroll + d->list_h - 1;
        return;
    }
    if (a->dlg.kind != DLG_NONE || a->menu >= 0)
        return;
    if (mod & KMOD_SHIFT) {
        dx = dy;
        dy = 0;
    }
    if (dy)
        ed_scroll(&a->ed, -dy * 3);
    if (dx) {
        a->ed.left -= dx * 4;
        if (a->ed.left < 0)
            a->ed.left = 0;
    }
}

static void menu_key(App *a, SDL_Keycode sym)
{
    int i;
    switch (sym) {
    case SDLK_ESCAPE:
    case SDLK_F10:
        a->menu = -1;
        return;
    case SDLK_LEFT:
        menu_open(a, a->menu - 1);
        return;
    case SDLK_RIGHT:
        menu_open(a, a->menu + 1);
        return;
    case SDLK_UP:
        menu_step(a, -1);
        return;
    case SDLK_DOWN:
        menu_step(a, 1);
        return;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        menu_run(a, a->menu_item);
        return;
    }
    for (i = 0; i < menus[a->menu].n; i++) {
        const char *l = menus[a->menu].items[i].label;
        if (l && hotkey_of(l) == (int)sym) {
            menu_run(a, i);
            return;
        }
    }
}

static void editor_key(App *a, const SDL_KeyboardEvent *k)
{
    Editor *ed = &a->ed;
    SDL_Keycode sym = k->keysym.sym;
    int ctrl = (k->keysym.mod & KMOD_CTRL) != 0;
    int shift = (k->keysym.mod & KMOD_SHIFT) != 0;
    unsigned long t = now_ms();

    wake_cursor(a);
    if (ctrl) {
        switch (sym) {
        case SDLK_n: command(a, CMD_NEW); return;
        case SDLK_o: command(a, CMD_OPEN); return;
        case SDLK_s: command(a, shift ? CMD_SAVEAS : CMD_SAVE); return;
        case SDLK_q: command(a, CMD_EXIT); return;
        case SDLK_z: command(a, shift ? CMD_REDO : CMD_UNDO); return;
        case SDLK_y: command(a, CMD_REDO); return;
        case SDLK_x: command(a, CMD_CUT); return;
        case SDLK_c: command(a, CMD_COPY); return;
        case SDLK_v: command(a, CMD_PASTE); return;
        case SDLK_a: command(a, CMD_SELALL); return;
        case SDLK_f: command(a, CMD_FIND); return;
        case SDLK_h: command(a, CMD_REPLACE); return;
        case SDLK_g: command(a, CMD_GOTO); return;
        case SDLK_l: command(a, CMD_LINENUM); return;
        case SDLK_INSERT: command(a, CMD_COPY); return;
        case SDLK_MINUS:
        case SDLK_KP_MINUS: set_size(a, a->scr.size - 1); return;
        case SDLK_EQUALS:
        case SDLK_PLUS:
        case SDLK_KP_PLUS: set_size(a, a->scr.size + 1); return;
        case SDLK_0:
        case SDLK_KP_0: set_size(a, SIZE_LARGE); return;
        case SDLK_UP: ed_scroll(ed, -1); return;
        case SDLK_DOWN: ed_scroll(ed, 1); return;
        }
    }
    switch (sym) {
    case SDLK_LEFT:     ed_move(ed, ctrl ? MV_WORDLEFT : MV_LEFT, shift); break;
    case SDLK_RIGHT:    ed_move(ed, ctrl ? MV_WORDRIGHT : MV_RIGHT, shift); break;
    case SDLK_UP:       ed_move(ed, MV_UP, shift); break;
    case SDLK_DOWN:     ed_move(ed, MV_DOWN, shift); break;
    case SDLK_HOME:     ed_move(ed, ctrl ? MV_DOCSTART : MV_HOME, shift); break;
    case SDLK_END:
        if (ctrl)
            finish_loading(a);
        ed_move(ed, ctrl ? MV_DOCEND : MV_END, shift);
        break;
    case SDLK_PAGEUP:   ed_move(ed, MV_PGUP, shift); break;
    case SDLK_PAGEDOWN: ed_move(ed, MV_PGDN, shift); break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: ed_newline(ed, t); break;
    case SDLK_BACKSPACE: ed_backspace(ed, ctrl, t); break;
    case SDLK_DELETE:
        if (shift)
            command(a, CMD_CUT);
        else
            ed_delete(ed, ctrl, t);
        break;
    case SDLK_INSERT:
        if (shift)
            command(a, CMD_PASTE);
        else
            command(a, CMD_OVERWRITE);
        break;
    case SDLK_TAB:      ed_tab(ed, shift, t); break;
    case SDLK_ESCAPE:   ed_clear_selection(ed); break;
    case SDLK_F1:       command(a, CMD_HELP); break;
    case SDLK_F3:       command(a, shift ? CMD_FINDPREV : CMD_FINDNEXT); break;
    case SDLK_F10:      menu_open(a, 0); break;
    }
}

void app_event(App *a, const SDL_Event *e)
{
    switch (e->type) {
    case SDL_QUIT:
        if (a->dlg.kind == DLG_CONFIRM)
            break;
        if (a->dlg.kind != DLG_NONE)
            dlg_close(a);
        a->menu = -1;
        guard(a, P_QUIT, NULL);
        break;
    case SDL_WINDOWEVENT:
        switch (e->window.event) {
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        case SDL_WINDOWEVENT_RESIZED:
            screen_layout(&a->scr);
            a->ed.follow = 1;
            break;
        case SDL_WINDOWEVENT_EXPOSED:
            a->scr.full = 1;
            break;
        case SDL_WINDOWEVENT_ENTER: {
            int x, y;
            SDL_GetMouseState(&x, &y);
            screen_pointer(&a->scr, x, y, 1);
            break;
        }
        case SDL_WINDOWEVENT_LEAVE:
            screen_pointer(&a->scr, 0, 0, 0);
            break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            a->focused = 1;
            wake_cursor(a);
            break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            a->focused = 0;
            a->alt_tap = 0;
            break;
        }
        break;
    case SDL_RENDER_TARGETS_RESET:
    case SDL_RENDER_DEVICE_RESET:
        screen_layout(&a->scr);
        break;
    case SDL_KEYDOWN: {
        SDL_Keycode sym = e->key.keysym.sym;
        int alt = (e->key.keysym.mod & KMOD_LALT) != 0;
        if (sym == SDLK_LALT || sym == SDLK_RALT) {
            a->alt_tap = !e->key.repeat;
            break;
        }
        a->alt_tap = 0;
        if (a->dlg.kind != DLG_NONE) {
            dlg_key(a, &e->key);
        } else if (a->menu >= 0) {
            menu_key(a, sym);
        } else if (alt && !(e->key.keysym.mod & KMOD_CTRL)) {
            int i;
            for (i = 0; i < NMENUS; i++)
                if (hotkey_of(menus[i].title) == (int)sym)
                    menu_open(a, i);
        } else {
            editor_key(a, &e->key);
        }
        break;
    }
    case SDL_KEYUP:
        if ((e->key.keysym.sym == SDLK_LALT || e->key.keysym.sym == SDLK_RALT) &&
            a->alt_tap && a->dlg.kind == DLG_NONE) {
            if (a->menu >= 0)
                a->menu = -1;
            else
                menu_open(a, 0);
        }
        a->alt_tap = 0;
        break;
    case SDL_TEXTINPUT: {
        SDL_Keymod mod = SDL_GetModState();
        /* Ctrl/Alt chords are commands, not text (AltGr is allowed) */
        if (mod & (KMOD_CTRL | KMOD_LALT))
            break;
        wake_cursor(a);
        if (a->dlg.kind != DLG_NONE)
            dlg_text(a, e->text.text);
        else if (a->menu < 0)   /* menus take letters as key presses */
            ed_type(&a->ed, e->text.text, strlen(e->text.text), now_ms());
        break;
    }
    case SDL_MOUSEBUTTONDOWN:
        mouse_down(a, &e->button);
        break;
    case SDL_MOUSEBUTTONUP:
        if (e->button.button == SDL_BUTTON_LEFT)
            a->drag = 0;
        break;
    case SDL_MOUSEMOTION:
        mouse_motion(a, &e->motion);
        break;
    case SDL_MOUSEWHEEL:
        mouse_wheel(a, &e->wheel);
        break;
    case SDL_DROPFILE:
        if (a->dlg.kind == DLG_NONE)
            guard(a, P_OPEN_PATH, e->drop.file);
        SDL_free(e->drop.file);
        break;
    }
}

void app_tick(App *a)
{
    unsigned long t = now_ms();

    if (buf_loading(a->ed.buf))
        buf_load_step(a->ed.buf, LOAD_SLICE);
    if (t >= a->blink_next) {
        a->blink_on = !a->blink_on;
        a->blink_next = t + BLINK_MS;
    }
    /* keep selecting while dragging past the top or bottom edge */
    if (a->drag == 1) {
        int tx, ty, tw, th, g;
        text_area(a, &tx, &ty, &tw, &th, &g);
        if (a->mouse_y < ty || a->mouse_y >= ty + th) {
            ed_move(&a->ed, a->mouse_y < ty ? MV_UP : MV_DOWN, 1);
        }
    }
}

int app_timeout(App *a)
{
    unsigned long t = now_ms(), next = a->blink_next;
    time_t now = time(NULL);
    unsigned long clock_ms = (unsigned long)(60 - now % 60) * 1000;

    if (buf_loading(a->ed.buf))
        return 0;
    if (a->drag == 1)
        return 40;
    if (a->msg_until > t && a->msg_until < next)
        next = a->msg_until;
    if (t + clock_ms < next)
        next = t + clock_ms;
    return next > t ? (int)(next - t) : 0;
}

int app_init(App *a, int argc, char **argv)
{
    Config cfg;

    memset(a, 0, sizeof *a);
    a->menu = -1;
    a->running = 1;
    a->focused = 1;
    config_load(&cfg);
    font_init();
    if (screen_init(&a->scr, cfg.size) < 0)
        return -1;
    ed_init(&a->ed);
    a->ed.autoindent = cfg.autoindent;
    a->ed.tabw = cfg.tab_width;
    a->show_lnum = cfg.line_numbers;
    a->dark = cfg.dark;
    apply_theme(a);
    /* the system pointer stays hidden over the window; we draw our own */
    SDL_ShowCursor(SDL_DISABLE);
    set_pointer(a, PTR_ARROW);
    if (SDL_GetMouseFocus() == a->scr.win) {
        int x, y;
        SDL_GetMouseState(&x, &y);
        screen_pointer(&a->scr, x, y, 1);
    }
    wake_cursor(a);
    SDL_StartTextInput();
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    if (argc > 1 && argv[1][0])
        do_open_path(a, argv[1]);
    return 0;
}

void app_quit(App *a)
{
    free_items(&a->dlg);
    ed_free(&a->ed);
    screen_quit(&a->scr);
}
