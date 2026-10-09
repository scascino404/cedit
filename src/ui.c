/*
 * ui.c - the cedit application: commands, dialogs, the editor window and
 * input handling.
 *
 * Everything is drawn into the Screen's character grid, text-mode style.
 * The menu bar (menu.c) and the dialog boxes (dialog.c) are generic; this
 * file gives them their content and acts on what the user picks.
 */
#define _XOPEN_SOURCE 700
#include "ui.h"
#include "config.h"
#include "utf8.h"
#include "util.h"

#include <dirent.h>
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
#define LEX_SLICE  ((size_t)8 * 1024 * 1024)

/* actions that wait for "save changes?" */
enum { P_NONE, P_QUIT, P_NEW, P_OPEN, P_OPEN_PATH };

enum {
    DLG_OPEN = DLG_NONE + 1, DLG_SAVEAS, DLG_FIND, DLG_REPLACE, DLG_GOTO,
    DLG_CONFIRM, DLG_MESSAGE, DLG_HELP
};

enum {
    ID_YES = ID_USER, ID_NO, ID_NAME, ID_FIND, ID_REPL, ID_CASE, ID_FINDNEXT,
    ID_REPLACE, ID_REPLALL, ID_LINE, ID_DIR
};

static const char *const help_lines[] = {
    "Ctrl+N  New            Ctrl+O  Open...",
    "Ctrl+S  Save           Ctrl+Shift+S  Save As...",
    "Ctrl+Q  Exit           F10 / Alt  Menu bar",
    "Ctrl+P  Folders and files in the current directory",
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
    "Ctrl+0  Default (medium) text",
    "Ctrl+L  Line numbers",
    NULL
};

/* The editor window's text area, in cells. */
typedef struct TextArea {
    int x, y, w, h;
    int gutter;             /* line number columns left of x */
} TextArea;

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static unsigned long now_ms(void)
{
    return (unsigned long)SDL_GetTicks();
}

static void set_msg(App *a, const char *s1, const char *s2)
{
    str_copy(a->msg, sizeof a->msg, s1);
    if (s2)
        str_cat(a->msg, sizeof a->msg, s2);
    a->msg_until = now_ms() + MSG_MS;
}

static void update_title(App *a)
{
    char t[512];
    str_copy(t, sizeof t, ed_modified(&a->ed) ? "* " : "");
    str_cat(t, sizeof t, ed_name(&a->ed));
    str_cat(t, sizeof t, " - cedit");
    if (strcmp(t, a->title) != 0) {
        str_copy(a->title, sizeof a->title, t);
        SDL_SetWindowTitle(a->scr.win, t);
    }
}

static void wake_cursor(App *a)
{
    a->blink_on = 1;
    a->blink_next = now_ms() + BLINK_MS;
}

static TextArea text_area(App *a)
{
    TextArea ta;
    ta.gutter = 0;
    if (a->show_lnum) {
        long n = ed_lines(&a->ed);
        int digits = 1;
        while (n >= 10) {
            n /= 10;
            digits++;
        }
        ta.gutter = (digits < 4 ? 4 : digits) + 1;
    }
    ta.x = 1 + ta.gutter;
    ta.y = 2;
    ta.w = a->scr.cols - 2 - ta.gutter;
    ta.h = a->scr.rows - 3;
    if (ta.w < 1)
        ta.w = 1;
    if (ta.h < 1)
        ta.h = 1;
    return ta;
}

/* Tells the editor the size of its view, and returns the text area. */
static TextArea sync_view(App *a)
{
    TextArea ta = text_area(a);
    a->ed.view_w = ta.w;
    a->ed.view_h = ta.h;
    return ta;
}

/* The cell of the text cursor, scrolling it into view first. */
static void cursor_cell(App *a, int *cx, int *cy)
{
    TextArea ta = text_area(a);
    Editor *ed = &a->ed;
    size_t len;
    const char *l = ed_line(ed, ed->cy, &len);

    ed_scroll_to_cursor(ed);
    *cx = ta.x + (int)(ed_disp_col(ed, l, len, ed->cx) - ed->left);
    *cy = ta.y + (int)(ed->cy - ed->top);
}

/* Sets a->dir to the current file's directory, or to the working directory
 * for an untitled buffer. */
static void current_dir(App *a)
{
    if (a->ed.path) {
        char *rp = realpath(a->ed.path, NULL), *slash;
        str_copy(a->dir, sizeof a->dir, rp ? rp : a->ed.path);
        free(rp);
        slash = strrchr(a->dir, '/');
        if (slash && slash != a->dir)
            *slash = 0;
        else if (slash)
            slash[1] = 0;
        else if (!getcwd(a->dir, sizeof a->dir))
            str_copy(a->dir, sizeof a->dir, ".");
    } else if (!getcwd(a->dir, sizeof a->dir)) {
        str_copy(a->dir, sizeof a->dir, ".");
    }
}

/* Loads the rest of a file that is still being indexed. */
static void finish_loading(App *a)
{
    if (buf_loading(a->ed.buf))
        buf_load_all(a->ed.buf);
}

/* ------------------------------------------------------------------ */
/* dialogs                                                             */
/* ------------------------------------------------------------------ */

static void close_dialog(App *a)
{
    dlg_close(&a->dlg);
    wake_cursor(a);
}

/* Directories first, "../" on top, then case-insensitive by name. */
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
        int c = ascii_lower((unsigned char)*x) - ascii_lower((unsigned char)*y);
        if (c)
            return c;
    }
    return (unsigned char)*x - (unsigned char)*y;
}

/* Fills the file dialog's list with the entries of a->dir. */
static void load_dir(App *a)
{
    Dialog *d = &a->dlg;
    size_t n = strlen(a->dir);
    DIR *dir;
    struct dirent *e;

    /* the path label shows the tail of long paths */
    field_set(dlg_find(d, ID_DIR), n < FIELD_MAX ? a->dir : a->dir + n - (FIELD_MAX - 1));
    dlg_list_clear(d);
    d->sel = 0;
    dir = opendir(a->dir);
    if (!dir) {
        str_copy(d->msg, sizeof d->msg, "Cannot read this directory.");
        return;
    }
    while ((e = readdir(dir)) != NULL) {
        char full[sizeof a->dir + 256], name[258];
        struct stat st;
        if (strcmp(e->d_name, ".") == 0)
            continue;
        if (strcmp(e->d_name, "..") == 0 && strcmp(a->dir, "/") == 0)
            continue;
        if (e->d_name[0] == '.' && strcmp(e->d_name, "..") != 0)
            continue;
        str_copy(full, sizeof full, a->dir);
        str_cat(full, sizeof full, "/");
        str_cat(full, sizeof full, e->d_name);
        str_copy(name, sizeof name - 1, e->d_name);
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
            str_cat(name, sizeof name, "/");
        dlg_list_add(d, name);
    }
    closedir(dir);
    qsort(d->items, (size_t)d->nitems, sizeof(char *), cmp_items);
}

static void file_dialog(App *a, int save)
{
    Dialog *d = &a->dlg;
    int w = 70, h = 24;
    static const int ids[] = {ID_OK, ID_CANCEL};
    const char *labels[2];
    Widget *f;

    dlg_begin(d, save ? DLG_SAVEAS : DLG_OPEN, save ? "Save As" : "Open", w, h);
    d->min_w = 30;
    d->min_h = 12;
    labels[0] = save ? "&Save" : "&Open";
    labels[1] = "Cancel";
    dlg_add(d, W_PATH, ID_DIR, 2, 1, w - 4, "Dir: ")->stretch = 1;
    dlg_add(d, W_LABEL, ID_NONE, 2, 3, 10, "File name:");
    f = dlg_add(d, W_FIELD, ID_NAME, 13, 3, w - 15, NULL);
    f->stretch = 1;
    dlg_list(d, 3, 6, w - 6, h - 10, 1, 0);
    d->list_field = ID_NAME;
    dlg_buttons(d, h - 3, ids, labels, 2);
    d->def_id = ID_OK;
    a->overwrite[0] = 0;
    current_dir(a);
    if (save && a->ed.path)
        field_set(f, ed_name(&a->ed));
    load_dir(a);
}

static void message_dialog(App *a, const char *title, const char *l1,
                           const char *l2)
{
    Dialog *d = &a->dlg;
    int w = 40, w1 = utf8_width(l1) + 6, w2 = l2 ? utf8_width(l2) + 6 : 0;
    static const int ids[] = {ID_OK};
    static const char *const labels[] = {"OK"};
    if (w1 > w)
        w = w1;
    if (w2 > w)
        w = w2;
    dlg_begin(d, DLG_MESSAGE, title, w, l2 ? 8 : 7);
    d->min_w = 20;                      /* long lines are cut off */
    dlg_add(d, W_LABEL, ID_NONE, 3, 2, w - 6, l1);
    if (l2)
        dlg_add(d, W_LABEL, ID_NONE, 3, 3, w - 6, l2);
    dlg_buttons(d, d->h - 3, ids, labels, 1);
    d->def_id = ID_OK;
}

static void help_dialog(App *a)
{
    Dialog *d = &a->dlg;
    int i, w = 4, h;
    static const int ids[] = {ID_OK};
    static const char *const labels[] = {"OK"};
    for (i = 0; help_lines[i]; i++)
        if (utf8_width(help_lines[i]) + 6 > w)
            w = utf8_width(help_lines[i]) + 6;
    h = i + 6;
    dlg_begin(d, DLG_HELP, "Keyboard", w, h);
    /* on a small screen, lines are cut off and the list scrolls */
    d->min_w = 20;
    d->min_h = 8;
    dlg_list(d, 2, 2, w - 4, h - 6, 0, -1);
    for (i = 0; help_lines[i]; i++)
        dlg_list_add(d, help_lines[i]);
    dlg_buttons(d, d->h - 3, ids, labels, 1);
    d->def_id = ID_OK;
}

static void confirm_dialog(App *a)
{
    Dialog *d = &a->dlg;
    char line[300];
    static const int ids[] = {ID_YES, ID_NO, ID_CANCEL};
    static const char *const labels[] = {"&Yes", "&No", "Cancel"};
    int w;

    str_copy(line, sizeof line, "Save changes to ");
    str_cat(line, sizeof line, ed_name(&a->ed));
    str_cat(line, sizeof line, "?");
    w = utf8_width(line) + 8;
    if (w < 40)
        w = 40;
    dlg_begin(d, DLG_CONFIRM, "cedit", w, 7);
    d->min_w = 30;
    dlg_add(d, W_LABEL, ID_NONE, (w - utf8_width(line)) / 2, 2, w - 4, line);
    dlg_buttons(d, 4, ids, labels, 3);
    d->def_id = ID_YES;
}

static void find_dialog(App *a, int replace)
{
    Dialog *d = &a->dlg;
    Widget *f;
    int w = 60, y = 3;
    static const int fids[] = {ID_FINDNEXT, ID_CANCEL};
    static const char *const flabels[] = {"Find &Next", "Cancel"};
    static const int rids[] = {ID_FINDNEXT, ID_REPLACE, ID_REPLALL, ID_CANCEL};
    static const char *const rlabels[] = {"Find &Next", "&Replace", "Replace &All", "Close"};
    long sy, ey;
    size_t sx, ex;

    dlg_begin(d, replace ? DLG_REPLACE : DLG_FIND, replace ? "Replace" : "Find",
              w, replace ? 9 : 8);
    d->min_w = replace ? 54 : 40;       /* room for the buttons */
    d->at_bottom = 1;
    dlg_add(d, W_LABEL, ID_NONE, 2, 1, 13, "Find what:");
    f = dlg_add(d, W_FIELD, ID_FIND, 16, 1, w - 18, NULL);
    f->stretch = 1;
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
    if (replace) {
        dlg_add(d, W_LABEL, ID_NONE, 2, 2, 13, "Replace with:");
        f = dlg_add(d, W_FIELD, ID_REPL, 16, 2, w - 18, NULL);
        f->stretch = 1;
        field_set(f, a->ed.repl);
        y = 4;
    }
    dlg_add(d, W_CHECK, ID_CASE, 2, y, 20, "Match &case")->checked = !a->ed.icase;
    if (replace)
        dlg_buttons(d, d->h - 3, rids, rlabels, 4);
    else
        dlg_buttons(d, d->h - 3, fids, flabels, 2);
    d->def_id = ID_FINDNEXT;
}

static void goto_dialog(App *a)
{
    Dialog *d = &a->dlg;
    char num[32];
    static const int ids[] = {ID_OK, ID_CANCEL};
    static const char *const labels[] = {"OK", "Cancel"};

    dlg_begin(d, DLG_GOTO, "Go to Line", 36, 7);
    dlg_add(d, W_LABEL, ID_NONE, 3, 2, 13, "Line number:");
    sprintf(num, "%ld", a->ed.cy + 1);
    field_set(dlg_add(d, W_FIELD, ID_LINE, 17, 2, 14, NULL), num);
    dlg_buttons(d, 4, ids, labels, 2);
    d->def_id = ID_OK;
}

/* ------------------------------------------------------------------ */
/* file operations and pending actions                                 */
/* ------------------------------------------------------------------ */

/* Picks the highlighting language for the file being edited. */
static void detect_syntax(App *a)
{
    size_t len;
    const char *l = ed_line(&a->ed, 0, &len);
    hl_set(&a->hl, a->ed.buf, syn_detect(a->ed.path, l, len));
}

static void do_open_path(App *a, const char *path)
{
    char err[256];
    int r = ed_open(&a->ed, path, err, sizeof err);
    if (r < 0) {
        message_dialog(a, "Error", path, err);
        return;
    }
    detect_syntax(a);
    set_msg(a, r == 1 ? "New file: " : "Opened ", ed_name(&a->ed));
}

static int do_save_path(App *a, const char *path)
{
    char err[256], info[64];
    if (ed_save(&a->ed, path, err, sizeof err) < 0) {
        message_dialog(a, "Cannot save", path, err);
        return -1;
    }
    detect_syntax(a);
    sprintf(info, " (%ld lines)", ed_lines(&a->ed));
    set_msg(a, "Saved ", ed_name(&a->ed));
    str_cat(a->msg, sizeof a->msg, info);
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
        detect_syntax(a);
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
    str_copy(a->pending_path, sizeof a->pending_path, path ? path : "");
    if (ed_modified(&a->ed))
        confirm_dialog(a);
    else
        run_pending(a);
}

/* Saves, then runs the pending action if the save worked. */
static void save_then_pending(App *a, const char *path)
{
    if (do_save_path(a, path) == 0)
        run_pending(a);
    else
        a->pending = P_NONE;
}

static void do_save(App *a)
{
    if (!a->ed.path)
        file_dialog(a, 1);
    else
        do_save_path(a, a->ed.path);
}

/* A file size as ls -h shows it: "980", "4.2K", "17M". */
static void format_size(char *out, double n)
{
    static const char units[] = " KMGT";
    int u = 0;
    while (n >= 1000 && u < 4) {
        n /= 1024;
        u++;
    }
    if (u == 0)
        sprintf(out, "%d", (int)n);
    else if (n < 9.95)
        sprintf(out, "%.1f%c", n, units[u]);
    else
        sprintf(out, "%d%c", (int)(n + 0.5), units[u]);
}

/* A directory entry for the file menu. */
typedef struct DirEntry {
    char *name;             /* folders end in '/', as in the Open dialog */
    char size[16];
    int flags;              /* LI_* */
} DirEntry;

static int cmp_entries(const void *pa, const void *pb)
{
    return cmp_items(&((const DirEntry *)pa)->name, &((const DirEntry *)pb)->name);
}

/* Inserts the folders and then the files of directory path into the file
 * menu, from item at on, depth levels deep. The open file is checked.
 * Dotfiles are left out, as in the Open dialog. Returns how many items it
 * added, or -1 if the directory can't be read. */
static int add_dir(App *a, const char *path, int at, int depth)
{
    DirEntry *ents = NULL;
    int n = 0, i;
    struct stat cur;
    int have_cur = a->ed.path && stat(a->ed.path, &cur) == 0;
    DIR *dir = opendir(path);
    struct dirent *e;

    if (!dir)
        return -1;
    while ((e = readdir(dir)) != NULL) {
        char full[sizeof a->pending_path + 256];
        struct stat st;
        DirEntry *d;
        if (e->d_name[0] == '.')
            continue;
        str_copy(full, sizeof full, path);
        str_cat(full, sizeof full, "/");
        str_cat(full, sizeof full, e->d_name);
        if (stat(full, &st) != 0 || !(S_ISDIR(st.st_mode) || S_ISREG(st.st_mode)))
            continue;
        /* the capacity is n rounded up to a power of two */
        if ((n & (n - 1)) == 0)
            ents = (DirEntry *)xrealloc(ents, (size_t)(n ? 2 * n : 1) * sizeof *ents);
        d = &ents[n++];
        d->name = (char *)xmalloc(strlen(e->d_name) + 2);
        str_copy(d->name, strlen(e->d_name) + 2, e->d_name);
        d->size[0] = 0;
        d->flags = 0;
        if (S_ISDIR(st.st_mode)) {
            strcat(d->name, "/");
            d->flags = LI_FOLDER;
        } else {
            format_size(d->size, (double)st.st_size);
            if (have_cur && st.st_dev == cur.st_dev && st.st_ino == cur.st_ino)
                d->flags = LI_CHECKED;
        }
    }
    closedir(dir);
    qsort(ents, (size_t)n, sizeof *ents, cmp_entries);
    for (i = 0; i < n; i++) {
        menu_list_insert(&a->menu, at + i, ents[i].name, ents[i].size, depth, ents[i].flags);
        free(ents[i].name);
    }
    free(ents);
    return n;
}

/* The path of file menu item i: a->dir, then the folders it is in. */
static void item_path(App *a, int i, char *out, size_t size)
{
    int p = menu_list_parent(&a->menu, i);
    if (p >= 0) {
        item_path(a, p, out, size);         /* ends in '/' */
    } else {
        str_copy(out, size, a->dir);
        if (strcmp(a->dir, "/") != 0)
            str_cat(out, size, "/");
    }
    str_cat(out, size, a->menu.list[i].label);
}

/* Pops up the folders and files of the current directory under the text
 * cursor, with the open file highlighted. */
static void file_menu(App *a)
{
    int i, n, cx, cy, cur = -1;

    menu_list_clear(&a->menu);
    current_dir(a);
    n = add_dir(a, a->dir, 0, 0);
    if (n <= 0) {
        set_msg(a, n < 0 ? "Cannot read " : "No files in ", a->dir);
        return;
    }
    for (i = 0; i < n; i++)
        if (a->menu.list[i].flags & LI_CHECKED)
            cur = i;
    cursor_cell(a, &cx, &cy);
    menu_popup_list(&a->menu, cx, cy + 1, cur);
}

/* Opens or closes folder i of the file menu. */
static void toggle_folder(App *a, int i)
{
    char path[sizeof a->pending_path];
    const char *rel;                        /* below the menu's top folder */
    int n;

    if (a->menu.list[i].flags & LI_OPEN) {
        menu_list_collapse(&a->menu, i);
        return;
    }
    item_path(a, i, path, sizeof path);
    rel = path + strlen(a->dir) + (strcmp(a->dir, "/") != 0);
    n = add_dir(a, path, i + 1, a->menu.list[i].depth + 1);
    if (n < 0) {
        set_msg(a, "Cannot read ", rel);
        return;
    }
    a->menu.list[i].flags |= LI_OPEN;
    if (n == 0)
        set_msg(a, "No files in ", rel);
}

/* Opens file i of the file menu, unless it is the open one. */
static void open_listed(App *a, int i)
{
    char path[sizeof a->pending_path];
    if (a->menu.list[i].flags & LI_CHECKED)
        return;
    item_path(a, i, path, sizeof path);
    guard(a, P_OPEN_PATH, path);
}

/* Enter in a file dialog: opens a directory, or opens / saves the file. */
static void file_accept(App *a)
{
    Dialog *d = &a->dlg;
    Widget *f = dlg_find(d, ID_NAME);
    char path[sizeof a->dir + FIELD_MAX];
    struct stat st;
    int exists;

    if (d->dirty)                       /* a new name needs a new "replace?" */
        a->overwrite[0] = 0;
    d->dirty = 0;
    if (!f->len && d->sel < d->nitems)
        field_set(f, d->items[d->sel]);
    if (!f->len)
        return;
    if (f->text[0] == '/') {
        str_copy(path, sizeof path, f->text);
    } else if (f->text[0] == '~' && (f->text[1] == '/' || !f->text[1]) && getenv("HOME")) {
        str_copy(path, sizeof path, getenv("HOME"));
        str_cat(path, sizeof path, f->text + 1);
    } else {
        str_copy(path, sizeof path, a->dir);
        if (strcmp(a->dir, "/") != 0)
            str_cat(path, sizeof path, "/");
        str_cat(path, sizeof path, f->text);
    }
    exists = stat(path, &st) == 0;
    if (exists && S_ISDIR(st.st_mode)) {
        char *rp = realpath(path, NULL);
        str_copy(a->dir, sizeof a->dir, rp ? rp : path);
        free(rp);
        field_set(f, "");
        d->msg[0] = 0;
        load_dir(a);
        return;
    }
    if (d->kind == DLG_OPEN) {
        if (!exists) {
            str_copy(d->msg, sizeof d->msg, "File not found.");
            return;
        }
        close_dialog(a);
        do_open_path(a, path);
        return;
    }
    if (exists && strcmp(a->overwrite, path) != 0) {
        str_copy(d->msg, sizeof d->msg, "File exists. Press Enter again to replace it.");
        str_copy(a->overwrite, sizeof a->overwrite, path);
        return;
    }
    close_dialog(a);
    save_then_pending(a, path);
}

static void do_find(App *a, int backward)
{
    int r, h;
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
    h = sync_view(a).h;
    ed_scroll_to_cursor(&a->ed);
    if (a->ed.cy < a->ed.top || a->ed.cy > a->ed.top + h / 2) {
        a->ed.top = a->ed.cy - h / 3;
        if (a->ed.top < 0)
            a->ed.top = 0;
    }
}

/* Acts on a dialog button (ID_CANCEL also stands for Escape). */
static void dialog_button(App *a, int id)
{
    Dialog *d = &a->dlg;
    Widget *w;

    switch (d->kind) {
    case DLG_OPEN:
    case DLG_SAVEAS:
        if (id == ID_CANCEL) {
            a->pending = P_NONE;
            close_dialog(a);
        } else {
            file_accept(a);
        }
        break;
    case DLG_FIND:
    case DLG_REPLACE:
        if (id == ID_CANCEL) {
            close_dialog(a);
            break;
        }
        str_copy(a->ed.find, sizeof a->ed.find, dlg_find(d, ID_FIND)->text);
        a->ed.icase = !dlg_find(d, ID_CASE)->checked;
        if ((w = dlg_find(d, ID_REPL)) != NULL)
            str_copy(a->ed.repl, sizeof a->ed.repl, w->text);
        if (!a->ed.find[0])
            break;
        if (d->kind == DLG_FIND)
            close_dialog(a);
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
    case DLG_GOTO: {
        long n = atol(dlg_find(d, ID_LINE)->text);
        close_dialog(a);
        if (id == ID_OK && n > 0)
            ed_goto(&a->ed, n);
        break;
    }
    case DLG_CONFIRM:
        close_dialog(a);
        if (id == ID_YES) {
            if (!a->ed.path)
                file_dialog(a, 1);
            else
                save_then_pending(a, a->ed.path);
        } else if (id == ID_NO) {
            run_pending(a);
        } else {
            a->pending = P_NONE;
        }
        break;
    default:
        close_dialog(a);
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
    a->theme = a->dark ? &theme_dark : &theme_light;
    a->scr.cur_bg = a->theme->cursor_bg;
    a->scr.cur_ink = a->theme->cursor_ink;
    a->scr.cur_box = a->theme->cursor_box;
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
    c.highlight = a->highlight;
    c.indent_spaces = a->ed.spaces;
    config_save(&c);
}

static void set_size(App *a, int size)
{
    screen_set_size(&a->scr, size);
    save_settings(a);
}

static int cmd_enabled(App *a, int cmd)
{
    switch (cmd) {
    case CMD_UNDO:
        return a->ed.undo.pos > 0;
    case CMD_REDO:
        return a->ed.undo.pos < a->ed.undo.n;
    case CMD_CUT:
    case CMD_COPY:
    case CMD_DELETE:
        return ed_has_selection(&a->ed);
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
    case CMD_SIZE_M:
        return a->scr.size == SIZE_MEDIUM;
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
    case CMD_SPACES:
        return a->ed.spaces;
    case CMD_HIGHLIGHT:
        return a->highlight;
    }
    return 0;
}

/* CmdState for the menus. */
static int cmd_state(void *ctx, int cmd)
{
    App *a = (App *)ctx;
    return (cmd_enabled(a, cmd) ? CMD_ENABLED : 0) |
           (cmd_checked(a, cmd) ? CMD_CHECKED : 0);
}

static void command(App *a, int cmd)
{
    unsigned long t = now_ms();
    wake_cursor(a);
    if (cmd >= CMD_LIST) {
        open_listed(a, cmd - CMD_LIST);
        return;
    }
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
    case CMD_SIZE_M:    set_size(a, SIZE_MEDIUM); break;
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
    case CMD_SPACES:
        a->ed.spaces = !a->ed.spaces;
        save_settings(a);
        break;
    case CMD_HIGHLIGHT:
        a->highlight = !a->highlight;
        save_settings(a);
        break;
    case CMD_FILES:     file_menu(a); break;
    case CMD_HELP:      help_dialog(a); break;
    case CMD_ABOUT:
        message_dialog(a, "About", "cedit 0.1 - Classic Text Editor",
                       "C89 + SDL2. Hand-drawn fonts. Public domain spirit.");
        break;
    }
}

/* Runs a command picked from a menu, unless it is disabled. */
static void menu_command(App *a, int cmd)
{
    if (cmd == CMD_NONE || !cmd_enabled(a, cmd))
        return;
    if (cmd >= CMD_LIST && (a->menu.list[cmd - CMD_LIST].flags & LI_FOLDER)) {
        toggle_folder(a, cmd - CMD_LIST);   /* the menu stays open */
        return;
    }
    menu_close(&a->menu);
    command(a, cmd);
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */

static void draw_text(App *a, TextArea ta)
{
    Screen *s = &a->scr;
    Editor *ed = &a->ed;
    const Theme *th = a->theme;
    long nlines = ed_lines(ed), row, sy = 0, ey = 0;
    size_t sx = 0, ex = 0;
    int have_sel = ed_sel_range(ed, &sy, &sx, &ey, &ex);

    for (row = 0; row < ta.h; row++) {
        long ln = ed->top + row, d = 0;
        size_t len, i = 0;
        const char *l;
        const unsigned char *cls;
        int y = ta.y + (int)row;

        if (ln >= nlines)
            break;
        if (ta.gutter) {
            char num[32];
            int n = sprintf(num, "%ld", ln + 1);
            screen_puts(s, ta.x - 1 - n, y, num, ln == ed->cy ? th->cur_lnum : th->lnum,
                        th->text_bg, n);
        }
        cls = a->highlight ? hl_line(&a->hl, ln) : NULL;
        l = ed_line(ed, ln, &len);
        while (i < len && d < ed->left + ta.w) {
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
            fg = sel ? th->sel_fg : cls ? th->hl[cls[i]] : th->text_fg;
            bg = sel ? th->sel_bg : th->text_bg;
            if (cp < 0x20 || cp == 0x7F) {
                /* control characters: inverse ^X letter */
                cp = cp == 0x7F ? '?' : cp + '@';
                fg = th->special_fg;
                bg = sel ? th->special_sel_bg : th->special_bg;
            } else if (cp == 0xFFFD || !font_has(s->font, cp)) {
                fg = sel ? th->sel_hot : th->bad;
            }
            for (c = 0; c < width; c++) {
                long dc = d + c;
                if (dc >= ed->left && dc < ed->left + ta.w)
                    screen_put(s, ta.x + (int)(dc - ed->left), y, c ? ' ' : cp, fg, bg);
            }
            d += width;
            i += k;
        }
        /* selected line break */
        if (have_sel && ln >= sy && ln < ey && i >= len && d >= ed->left &&
            d < ed->left + ta.w && (ln > sy || len >= sx))
            screen_put(s, ta.x + (int)(d - ed->left), y, ' ', th->sel_fg, th->sel_bg);
    }

    /* text cursor */
    if (a->dlg.kind == DLG_NONE && a->menu.open < 0 && (a->blink_on || !a->focused) &&
        ed->cy >= ed->top && ed->cy < ed->top + ta.h) {
        size_t len;
        const char *l = ed_line(ed, ed->cy, &len);
        long dc = ed_disp_col(ed, l, len, ed->cx) - ed->left;
        if (dc >= 0 && dc < ta.w)
            screen_cursor(s, ta.x + (int)dc, ta.y + (int)(ed->cy - ed->top),
                          ed->overwrite ? TCUR_OVERWRITE : TCUR_INSERT);
    }
}

/* The scrollbar for a text area h rows tall: the track between the arrows,
 * and the thumb's position and length in it. */
static void scrollbar_geometry(App *a, int h, int *track, int *tpos, int *tlen)
{
    long total = ed_lines(&a->ed), range = total - h;
    *track = h - 2;
    if (*track < 1)
        *track = 1;
    *tlen = total > h ? (int)((long)*track * h / total) : *track;
    if (*tlen < 1)
        *tlen = 1;
    *tpos = range > 0 ? (int)((*track - *tlen) * a->ed.top / range) : 0;
    if (*tpos > *track - *tlen)
        *tpos = *track - *tlen;
}

static void draw_window(App *a, TextArea ta)
{
    Screen *s = &a->scr;
    Editor *ed = &a->ed;
    const Theme *th = a->theme;
    int i, x, track, tpos, tlen, bottom = s->rows - 1;
    char title[300], pos[96], line[300];

    screen_fill(s, 0, 1, s->cols, s->rows - 1, ' ', th->text_fg, th->text_bg);
    screen_frame(s, 0, 1, s->cols, s->rows - 1, 1, th->frame, th->text_bg);

    /* title, inverse, centered on the top border */
    str_copy(title, sizeof title, " ");
    str_cat(title, sizeof title, ed_name(ed));
    str_cat(title, sizeof title, ed_modified(ed) ? " * " : " ");
    i = utf8_width(title);
    if (i > s->cols - 4)
        i = s->cols - 4;
    screen_puts(s, (s->cols - i) / 2, 1, title, th->title_fg, th->title_bg, i);

    /* scrollbar on the right border */
    scrollbar_geometry(a, ta.h, &track, &tpos, &tlen);
    screen_put(s, s->cols - 1, ta.y, 0x25B2, th->frame, th->text_bg);
    screen_put(s, s->cols - 1, ta.y + ta.h - 1, 0x25BC, th->frame, th->text_bg);
    for (i = 0; i < track && ta.h > 2; i++)
        screen_put(s, s->cols - 1, ta.y + 1 + i,
                   i >= tpos && i < tpos + tlen ? 0x2588 : 0x2591, th->frame, th->text_bg);

    /* status in the bottom border, TempleOS style. A message takes all
     * of it, inverse like the title, until it times out. */
    if (a->msg[0] && now_ms() < a->msg_until) {
        str_copy(line, sizeof line, " ");
        str_cat(line, sizeof line, a->msg);
        str_cat(line, sizeof line, " ");
        screen_puts(s, 2, bottom, line, th->title_fg, th->title_bg, s->cols - 4);
    } else {
        /* the position on the right, and on the left what fits of the mode,
         * line ends, encoding and loading progress */
        const char *left[4];
        char load[32];
        size_t len;
        const char *l = ed_line(ed, ed->cy, &len);
        long col = ed_disp_col(ed, l, len, ed->cx) + 1;
        int end;
        sprintf(pos, " Line:%04ld/%04ld%s Col:%03ld ", ed->cy + 1, ed_lines(ed),
                buf_loading(ed->buf) ? "+" : "", col);
        i = utf8_width(pos);
        if (i > s->cols - 4)
            i = s->cols - 4;
        end = s->cols - 2 - i;
        screen_puts(s, end, bottom, pos, th->frame, th->text_bg, i);
        left[0] = ed->overwrite ? " OVR " : " INS ";
        left[1] = ed->buf->crlf ? " CRLF " : " LF ";
        left[2] = " UTF-8 ";
        left[3] = NULL;
        if (buf_loading(ed->buf)) {
            sprintf(load, " Loading %d%% ", (int)(buf_load_progress(ed->buf) * 100));
            left[3] = load;
        }
        /* each part and a border cell after it */
        for (i = 0, x = 2; i < 4 && left[i] && x + utf8_width(left[i]) < end; i++)
            x += screen_puts(s, x, bottom, left[i], th->frame, th->text_bg,
                             utf8_width(left[i])) + 1;
    }

    draw_text(a, ta);
}

/* The clock at the right end of the menu bar, if there is room for it. */
static void draw_clock(App *a)
{
    char clock[32];
    time_t t = time(NULL);
    int x;
    strftime(clock, sizeof clock, "%a %m/%d %H:%M", localtime(&t));
    x = a->scr.cols - utf8_width(clock) - 1;
    if (x > menu_bar_width())
        screen_puts(&a->scr, x, 0, clock, a->theme->bar_fg, a->theme->bar_bg, 32);
}

/* Fits the menus to the screen. */
static void layout_menus(App *a)
{
    menu_layout(&a->menu, a->scr.cols, a->scr.rows);
}

void app_draw(App *a)
{
    TextArea ta = sync_view(a);

    layout_menus(a);
    if (a->ed.follow)
        ed_scroll_to_cursor(&a->ed);
    draw_window(a, ta);
    menu_draw(&a->menu, &a->scr, a->theme, cmd_state, a);
    draw_clock(a);
    dlg_draw(&a->dlg, &a->scr, a->theme, a->blink_on);
    update_title(a);
    screen_present(&a->scr);
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */

/* Converts a cell in the text area to a buffer position. */
static void cell_to_pos(App *a, int cx, int cy, long *ln, size_t *col)
{
    TextArea ta = text_area(a);
    size_t len;
    const char *l;
    *ln = a->ed.top + (cy - ta.y);
    if (*ln < 0)
        *ln = 0;
    if (*ln >= ed_lines(&a->ed))
        *ln = ed_lines(&a->ed) - 1;
    l = ed_line(&a->ed, *ln, &len);
    *col = ed_byte_col(&a->ed, l, len, a->ed.left + (cx - ta.x));
}

static void scrollbar_click(App *a, int cy, TextArea ta)
{
    int track, tpos, tlen, r = cy - ta.y - 1;
    scrollbar_geometry(a, ta.h, &track, &tpos, &tlen);
    if (cy == ta.y)
        ed_scroll(&a->ed, -1);
    else if (cy == ta.y + ta.h - 1)
        ed_scroll(&a->ed, 1);
    else if (r < tpos)
        ed_scroll(&a->ed, -(ta.h - 1));
    else if (r >= tpos + tlen)
        ed_scroll(&a->ed, ta.h - 1);
    else {
        a->drag = 2;
        a->drag_grab = r - tpos;
    }
}

static void scrollbar_drag(App *a, int cy)
{
    TextArea ta = text_area(a);
    int track, tpos, tlen;
    long range = ed_lines(&a->ed) - ta.h;
    scrollbar_geometry(a, ta.h, &track, &tpos, &tlen);
    if (range <= 0 || track - tlen <= 0)
        return;
    tpos = cy - ta.y - 1 - a->drag_grab;
    if (tpos < 0)
        tpos = 0;
    if (tpos > track - tlen)
        tpos = track - tlen;
    /* round up, so scrollbar_geometry's rounding down puts the thumb back
     * under the pointer */
    a->ed.top = ((long)tpos * range + track - tlen - 1) / (track - tlen);
    ed_scroll(&a->ed, 0);
}

/* A right click on the text opens the context menu there. It moves the
 * cursor to the click, unless the click is inside the selection. In an open
 * menu, it works like a left click. */
static void right_click(App *a, int cx, int cy)
{
    TextArea ta = text_area(a);
    long ln, sy, ey;
    size_t col, sx, ex;

    if (a->dlg.kind != DLG_NONE || a->drag)
        return;
    if (a->menu.open >= 0) {
        menu_command(a, menu_click(&a->menu, cx, cy));
        return;
    }
    if (cy < ta.y || cy >= ta.y + ta.h || cx < ta.x - ta.gutter || cx >= ta.x + ta.w)
        return;
    cell_to_pos(a, cx < ta.x ? ta.x : cx, cy, &ln, &col);
    if (!ed_sel_range(&a->ed, &sy, &sx, &ey, &ex) ||
        ln < sy || (ln == sy && col < sx) || ln > ey || (ln == ey && col >= ex)) {
        ed_set_cursor(&a->ed, ln, col, 0);
        a->ed.follow = 0;
    }
    menu_popup(&a->menu, cx, cy);
}

/* The Menu key or Shift+F10 opens the context menu below the text cursor,
 * scrolling it into view first. */
static void cursor_popup(App *a)
{
    int cx, cy;
    cursor_cell(a, &cx, &cy);
    menu_popup(&a->menu, cx, cy + 1);
}

static void mouse_down(App *a, const SDL_MouseButtonEvent *b)
{
    TextArea ta;
    int cx, cy, id;
    unsigned long t = now_ms();

    screen_cell_at(&a->scr, b->x, b->y, &cx, &cy);
    if (b->button == SDL_BUTTON_RIGHT)
        right_click(a, cx, cy);
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
        if ((id = dlg_click(&a->dlg, cx, cy, a->click_count)) != ID_NONE)
            dialog_button(a, id);
        return;
    }
    if (a->menu.open >= 0 || cy == 0) {
        menu_command(a, menu_click(&a->menu, cx, cy));
        return;
    }
    ta = text_area(a);
    if (cy < ta.y || cy >= ta.y + ta.h)
        return;
    if (cx == a->scr.cols - 1) {
        scrollbar_click(a, cy, ta);
    } else if (cx >= ta.x - ta.gutter) {
        long ln;
        size_t col;
        cell_to_pos(a, cx < ta.x ? ta.x : cx, cy, &ln, &col);
        ed_set_cursor(&a->ed, ln, col, (SDL_GetModState() & KMOD_SHIFT) != 0);
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
    TextArea ta = text_area(a);
    int cx, cy;

    screen_pointer(&a->scr, m->x, m->y, 1);
    screen_cell_at(&a->scr, m->x, m->y, &cx, &cy);
    a->mouse_x = cx;
    a->mouse_y = cy;

    if (a->drag == 1) {
        long ln;
        size_t col;
        int x = cx < ta.x ? ta.x : cx;
        int y = cy < ta.y ? ta.y : cy >= ta.y + ta.h ? ta.y + ta.h - 1 : cy;
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
    menu_hover(&a->menu, cx, cy);
}

static void mouse_wheel(App *a, const SDL_MouseWheelEvent *w)
{
    int dy = w->y, dx = w->x;
    SDL_Keymod mod = SDL_GetModState();
    if (w->direction == SDL_MOUSEWHEEL_FLIPPED) {
        dy = -dy;
        dx = -dx;
    }
    if (mod & KMOD_CTRL) {
        if (dy)
            set_size(a, a->scr.size + (dy > 0 ? 1 : -1));
        return;
    }
    if (a->dlg.kind != DLG_NONE) {
        dlg_wheel(&a->dlg, -dy * 3);
        return;
    }
    if (a->menu.open >= 0) {
        menu_wheel(&a->menu, -dy * 3);
        return;
    }
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
        case SDLK_p: command(a, CMD_FILES); return;
        case SDLK_INSERT: command(a, CMD_COPY); return;
        case SDLK_MINUS:
        case SDLK_KP_MINUS: set_size(a, a->scr.size - 1); return;
        case SDLK_EQUALS:
        case SDLK_PLUS:
        case SDLK_KP_PLUS: set_size(a, a->scr.size + 1); return;
        case SDLK_0:
        case SDLK_KP_0: set_size(a, SIZE_MEDIUM); return;
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
    case SDLK_INSERT:   command(a, shift ? CMD_PASTE : CMD_OVERWRITE); break;
    case SDLK_TAB:      ed_tab(ed, shift, t); break;
    case SDLK_ESCAPE:   ed_clear_selection(ed); break;
    case SDLK_F1:       command(a, CMD_HELP); break;
    case SDLK_F3:       command(a, shift ? CMD_FINDPREV : CMD_FINDNEXT); break;
    case SDLK_F10:
        if (shift)
            cursor_popup(a);
        else
            menu_open(&a->menu, 0);
        break;
    case SDLK_APPLICATION:
    case SDLK_MENU:     cursor_popup(a); break;
    }
}

static void key_down(App *a, const SDL_KeyboardEvent *k)
{
    SDL_Keycode sym = k->keysym.sym;
    int id;

    if (sym == SDLK_LALT || sym == SDLK_RALT) {
        a->alt_tap = !k->repeat;
        return;
    }
    a->alt_tap = 0;
    if (a->dlg.kind != DLG_NONE) {
        wake_cursor(a);
        if ((id = dlg_key(&a->dlg, k)) != ID_NONE)
            dialog_button(a, id);
    } else if (a->menu.open >= 0) {
        menu_command(a, menu_key(&a->menu, sym));
    } else if ((k->keysym.mod & KMOD_LALT) && !(k->keysym.mod & KMOD_CTRL)) {
        int i = menu_with_hotkey(sym);
        if (i >= 0)
            menu_open(&a->menu, i);
    } else {
        editor_key(a, k);
    }
}

static void window_event(App *a, const SDL_WindowEvent *w)
{
    int x, y;
    switch (w->event) {
    case SDL_WINDOWEVENT_SIZE_CHANGED:
    case SDL_WINDOWEVENT_RESIZED:
        screen_layout(&a->scr);
        layout_menus(a);
        a->ed.follow = 1;
        break;
    case SDL_WINDOWEVENT_EXPOSED:
        a->scr.full = 1;
        break;
    case SDL_WINDOWEVENT_ENTER:
        SDL_GetMouseState(&x, &y);
        screen_pointer(&a->scr, x, y, 1);
        break;
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
}

void app_event(App *a, const SDL_Event *e)
{
    switch (e->type) {
    case SDL_QUIT:
        if (a->dlg.kind == DLG_CONFIRM)
            break;
        if (a->dlg.kind != DLG_NONE)
            close_dialog(a);
        menu_close(&a->menu);
        guard(a, P_QUIT, NULL);
        break;
    case SDL_WINDOWEVENT:
        window_event(a, &e->window);
        break;
    case SDL_RENDER_TARGETS_RESET:
    case SDL_RENDER_DEVICE_RESET:
        screen_layout(&a->scr);
        break;
    case SDL_KEYDOWN:
        key_down(a, &e->key);
        break;
    case SDL_KEYUP:
        if ((e->key.keysym.sym == SDLK_LALT || e->key.keysym.sym == SDLK_RALT) &&
            a->alt_tap && a->dlg.kind == DLG_NONE) {
            if (a->menu.open >= 0)
                menu_close(&a->menu);
            else
                menu_open(&a->menu, 0);
        }
        a->alt_tap = 0;
        break;
    case SDL_TEXTINPUT:
        /* Ctrl/Alt chords are commands, not text (AltGr is allowed) */
        if (SDL_GetModState() & (KMOD_CTRL | KMOD_LALT))
            break;
        wake_cursor(a);
        if (a->dlg.kind != DLG_NONE)
            dlg_text(&a->dlg, e->text.text);
        else if (a->menu.open < 0)  /* menus take letters as key presses */
            ed_type(&a->ed, e->text.text, strlen(e->text.text), now_ms());
        break;
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

/* The last line the text area shows. */
static long last_shown(App *a)
{
    return a->ed.top + text_area(a).h - 1;
}

void app_tick(App *a)
{
    unsigned long t = now_ms();

    if (buf_loading(a->ed.buf))
        buf_load_step(a->ed.buf, LOAD_SLICE);
    if (a->highlight)
        hl_fill(&a->hl, last_shown(a), LEX_SLICE);
    if (t >= a->blink_next) {
        a->blink_on = !a->blink_on;
        a->blink_next = t + BLINK_MS;
    }
    menu_tick(&a->menu, t);
    /* keep selecting while dragging past the top or bottom edge */
    if (a->drag == 1) {
        TextArea ta = text_area(a);
        if (a->mouse_y < ta.y || a->mouse_y >= ta.y + ta.h)
            ed_move(&a->ed, a->mouse_y < ta.y ? MV_UP : MV_DOWN, 1);
    }
}

int app_timeout(App *a)
{
    unsigned long t = now_ms(), next = a->blink_next;
    unsigned long clock_ms = (unsigned long)(60 - time(NULL) % 60) * 1000;

    if (buf_loading(a->ed.buf) || (a->highlight && hl_behind(&a->hl, last_shown(a))))
        return 0;
    if (a->drag == 1)
        return 40;
    if (a->msg_until > t && a->msg_until < next)
        next = a->msg_until;
    if (a->menu.tick_item >= 0 && a->menu.tick_next < next)
        next = a->menu.tick_next;
    if (t + clock_ms < next)
        next = t + clock_ms;
    return next > t ? (int)(next - t) : 0;
}

int app_init(App *a, int argc, char **argv)
{
    Config cfg;

    memset(a, 0, sizeof *a);
    menu_close(&a->menu);
    a->running = 1;
    a->focused = 1;
    config_load(&cfg);
    font_init();
    if (screen_init(&a->scr, cfg.size) < 0)
        return -1;
    ed_init(&a->ed);
    a->ed.autoindent = cfg.autoindent;
    a->ed.tabw = cfg.tab_width;
    a->ed.spaces = cfg.indent_spaces;
    a->show_lnum = cfg.line_numbers;
    a->dark = cfg.dark;
    a->highlight = cfg.highlight;
    hl_init(&a->hl);
    detect_syntax(a);
    apply_theme(a);
    /* the system pointer stays hidden over the window; we draw our own */
    SDL_ShowCursor(SDL_DISABLE);
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
    dlg_close(&a->dlg);
    menu_list_clear(&a->menu);
    ed_free(&a->ed);
    hl_free(&a->hl);
    screen_quit(&a->scr);
}
