/*
 * menu.c - menu tables, drawing and input.
 */
#include "menu.h"
#include "utf8.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

#define TICK_MS     160     /* a label too long for its row moves a cell */
#define TICK_PAUSE  1200    /* and rests this long at its start */
#define TICK_GAP    4       /* blank cells before it comes around again */

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
    {"Auto &Indent", "", CMD_AUTOINDENT},
    {"Indent with &Spaces", "", CMD_SPACES}
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
    {"&Medium Text", "Ctrl+0", CMD_SIZE_M},
    {"&Large Text", "", CMD_SIZE_L},
    {NULL, NULL, 0},
    {"&Dark Mode", "", CMD_DARK},
    {"Line N&umbers", "Ctrl+L", CMD_LINENUM},
    {"&Word Wrap", "", CMD_WRAP},
    {"Syntax &Highlighting", "", CMD_HIGHLIGHT},
    {"Tab Width &4", "", CMD_TAB4},
    {"Tab Width &8", "", CMD_TAB8},
    {NULL, NULL, 0},
    {"&Files in Directory", "Ctrl+P", CMD_FILES}
};
static const MenuItem m_window[] = {
    {"Split &Vertically", "Ctrl+\\", CMD_SPLIT_V},
    {"Split &Horizontally", "Ctrl+Shift+\\", CMD_SPLIT_H},
    {NULL, NULL, 0},
    {"&Close Window", "Ctrl+W", CMD_CLOSEWIN},
    {NULL, NULL, 0},
    {"&Next Window", "F6", CMD_NEXTWIN},
    {"&Previous Window", "Shift+F6", CMD_PREVWIN}
};
static const MenuItem m_help[] = {
    {"&Keyboard...", "F1", CMD_HELP},
    {"&About...", "", CMD_ABOUT}
};
static const MenuItem m_context[] = {
    {"&Undo", "Ctrl+Z", CMD_UNDO},
    {NULL, NULL, 0},
    {"Cu&t", "Ctrl+X", CMD_CUT},
    {"&Copy", "Ctrl+C", CMD_COPY},
    {"&Paste", "Ctrl+V", CMD_PASTE},
    {"&Delete", "Del", CMD_DELETE},
    {NULL, NULL, 0},
    {"Select &All", "Ctrl+A", CMD_SELALL},
    {NULL, NULL, 0},
    {"Split &Vertically", "Ctrl+\\", CMD_SPLIT_V},
    {"Split &Horizontally", "Ctrl+Shift+\\", CMD_SPLIT_H},
    {"Close &Window", "Ctrl+W", CMD_CLOSEWIN}
};

static const struct {
    const char *title;
    const MenuItem *items;
    int n;
} menus[] = {
    {"&File", m_file, NELEM(m_file)},
    {"&Edit", m_edit, NELEM(m_edit)},
    {"&Search", m_search, NELEM(m_search)},
    {"&View", m_view, NELEM(m_view)},
    {"&Window", m_window, NELEM(m_window)},
    {"&Help", m_help, NELEM(m_help)},
    {NULL, m_context, NELEM(m_context)},
    {NULL, NULL, 0}         /* the list menu, from MenuBar.list */
};
/* The bar shows all menus but the last two, the popups. */
#define NMENUS (NELEM(menus) - 2)
#define CONTEXT NMENUS
#define LIST (NMENUS + 1)

static int count(const MenuBar *m)
{
    return m->open == LIST ? m->nlist : menus[m->open].n;
}

/* Is item i of the open menu an item, not a separator? */
static int selectable(const MenuBar *m, int i)
{
    return m->open == LIST || menus[m->open].items[i].label != NULL;
}

static int cmd_at(const MenuBar *m, int i)
{
    return m->open == LIST ? CMD_LIST + i : menus[m->open].items[i].cmd;
}

/* Cells before a list item's label: its indent and a folder's arrow. */
static int indent(const ListItem *it)
{
    return 2 * it->depth + 2;
}

/* Column of menu i's title. */
static int title_x(int i)
{
    int x = 1, j;
    for (j = 0; j < i; j++)
        x += label_width(menus[j].title) + 2;
    return x;
}

/* Index of the menu title at column cx, or -1. */
static int title_at(int cx)
{
    int i;
    for (i = 0; i < NMENUS; i++) {
        int x = title_x(i);
        if (cx >= x - 1 && cx <= x + label_width(menus[i].title))
            return i;
    }
    return -1;
}

static void geometry(const MenuBar *mb, int *x, int *y, int *w, int *h)
{
    int i, lw = 0, kw = 0, m = mb->open, n = count(mb);
    if (m == LIST) {
        kw = mb->list_kw;
        lw = mb->list_lw < mb->cols - kw - 8 ? mb->list_lw : mb->cols - kw - 8;
        if (lw < 1)
            lw = 1;
    }
    for (i = 0; m != LIST && i < n; i++) {
        const MenuItem *it = &menus[m].items[i];
        if (!it->label)
            continue;
        if (label_width(it->label) > lw)
            lw = label_width(it->label);
        if (label_width(it->keys) > kw)
            kw = label_width(it->keys);
    }
    *w = lw + kw + 8;
    *h = (n < mb->rows ? n : mb->rows) + 2;
    *x = m >= CONTEXT ? mb->x : title_x(m) - 1;
    *y = m >= CONTEXT ? mb->y : 1;
    /* a pull-down near the right edge of a narrow screen moves left */
    if (m < CONTEXT && *x + *w > mb->cols)
        *x = mb->cols - *w < 0 ? 0 : mb->cols - *w;
}

/* Cells for the label of list item it, in the list menu w cells wide; a
 * longer one is cut off, or moves while highlighted. */
static int label_room(const MenuBar *m, const ListItem *it, int w)
{
    return w - 8 - m->list_kw - indent(it);
}

static int room(const MenuBar *m, int i)
{
    int x, y, w, h;
    geometry(m, &x, &y, &w, &h);
    return label_room(m, &m->list[i], w);
}

/* The item of the open menu at a cell, or -1. */
static int item_at(const MenuBar *m, int cx, int cy)
{
    int x, y, w, h;
    if (m->open < 0)
        return -1;
    geometry(m, &x, &y, &w, &h);
    if (cx > x && cx < x + w - 1 && cy > y && cy < y + h - 1)
        return m->top + cy - y - 1;
    return -1;
}

/* Scrolls a long list menu by some rows. */
static void scroll(MenuBar *m, int by)
{
    m->top += by;
    if (m->top > count(m) - m->rows)
        m->top = count(m) - m->rows;
    if (m->top < 0)
        m->top = 0;
}

/* Scrolls the highlighted item into view. */
static void reveal(MenuBar *m)
{
    if (m->open < 0 || m->item < 0)
        return;
    if (m->item < m->top)
        m->top = m->item;
    if (m->item >= m->top + m->rows)
        m->top = m->item - m->rows + 1;
}

/* The scrollbar of a menu too long for the screen. */
static Scrollbar menu_scrollbar(const MenuBar *m)
{
    return scrollbar_make(count(m), m->top, m->rows);
}

/* Fits the open menu to the screen: scrolls it so the highlight shows, and
 * places a popup at the cell it was opened at, flipped left or up where it
 * would not fit. Runs again as the screen or a list's folders change. */
static void place(MenuBar *m)
{
    int x, y, w, h;
    if (m->open < 0)
        return;
    if (m->item >= count(m))
        m->item = count(m) - 1;
    scroll(m, 0);
    reveal(m);
    if (m->open < CONTEXT)
        return;
    m->x = m->ax;
    m->y = m->ay;
    geometry(m, &x, &y, &w, &h);
    if (m->ax + w > m->cols)
        m->x = m->ax + 1 - w;
    if (m->ay + h > m->lines)
        m->y = m->ay + 1 - h;
    /* still too big, or opened at a cell that is now off the screen */
    if (m->x + w > m->cols)
        m->x = m->cols - w;
    if (m->x < 0)
        m->x = 0;
    if (m->y + h > m->lines)
        m->y = m->lines - h;
    if (m->y < 1)
        m->y = 1;
}

void menu_layout(MenuBar *m, int cols, int rows)
{
    if (cols == m->cols && rows == m->lines)
        return;
    m->cols = cols;
    m->lines = rows;
    m->rows = rows - 3 < 1 ? 1 : rows - 3;     /* below the menu bar */
    place(m);
}

void menu_open(MenuBar *m, int i)
{
    m->open = (i + NMENUS) % NMENUS;
    m->item = 0;
    m->top = 0;
}

static void popup(MenuBar *m, int menu, int cx, int cy, int item)
{
    m->open = menu;
    m->item = item;
    m->top = 0;
    m->ax = cx;
    m->ay = cy;
    place(m);
}

void menu_popup(MenuBar *m, int cx, int cy)
{
    popup(m, CONTEXT, cx, cy, -1);
}

void menu_popup_list(MenuBar *m, int cx, int cy, int item)
{
    popup(m, LIST, cx, cy, item);
}

void menu_close(MenuBar *m)
{
    m->open = -1;
    m->tick_item = -1;
}

int menu_bar_width(void)
{
    return title_x(NMENUS);
}

int menu_with_hotkey(SDL_Keycode sym)
{
    int i;
    for (i = 0; i < NMENUS; i++)
        if (label_hotkey(menus[i].title) == (int)sym)
            return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* the list menu's items                                               */
/* ------------------------------------------------------------------ */

void menu_list_clear(MenuBar *m)
{
    int i;
    for (i = 0; i < m->nlist; i++)
        free(m->list[i].label);             /* keys share its allocation */
    free(m->list);
    m->list = NULL;
    m->nlist = m->list_lw = m->list_kw = 0;
}

static void measure(MenuBar *m, const ListItem *it)
{
    if (indent(it) + utf8_width(it->label) > m->list_lw)
        m->list_lw = indent(it) + utf8_width(it->label);
    if (utf8_width(it->keys) > m->list_kw)
        m->list_kw = utf8_width(it->keys);
}

void menu_list_insert(MenuBar *m, int i, const char *label, const char *keys,
                      int depth, int flags)
{
    int n = m->nlist;
    size_t ll = strlen(label) + 1, kl = strlen(keys) + 1;
    ListItem *it;
    m->list = (ListItem *)xgrow(m->list, n, sizeof *m->list);
    memmove(&m->list[i + 1], &m->list[i], (size_t)(n - i) * sizeof *m->list);
    it = &m->list[i];
    it->label = (char *)xmalloc(ll + kl);
    it->keys = it->label + ll;
    memcpy(it->label, label, ll);
    memcpy(it->keys, keys, kl);
    it->depth = depth;
    it->flags = flags;
    m->nlist++;
    measure(m, it);
    if (m->open == LIST) {
        if (m->item >= i)
            m->item++;
        place(m);
    }
}

void menu_list_collapse(MenuBar *m, int i)
{
    int j = i + 1, k, gone;
    while (j < m->nlist && m->list[j].depth > m->list[i].depth)
        j++;
    gone = j - i - 1;
    for (k = i + 1; k < j; k++)
        free(m->list[k].label);
    memmove(&m->list[i + 1], &m->list[j], (size_t)(m->nlist - j) * sizeof *m->list);
    m->nlist -= gone;
    m->list[i].flags &= ~LI_OPEN;
    if (m->item > i)
        m->item = m->item < j ? i : m->item - gone;
    m->list_lw = m->list_kw = 0;
    for (k = 0; k < m->nlist; k++)
        measure(m, &m->list[k]);
    if (m->open == LIST)
        place(m);
}

int menu_list_parent(const MenuBar *m, int i)
{
    int d = m->list[i].depth;
    while (--i >= 0)
        if (m->list[i].depth < d)
            return i;
    return -1;
}

void menu_tick(MenuBar *m, unsigned long now)
{
    int i = m->item;
    if (m->open != LIST || i < 0 || utf8_width(m->list[i].label) <= room(m, i)) {
        m->tick_item = -1;
        return;
    }
    if (m->tick_item != i) {
        m->tick_item = i;
        m->tick_pos = 0;
        m->tick_next = now + TICK_PAUSE;
    } else if (now >= m->tick_next) {
        m->tick_pos = (m->tick_pos + 1) % (utf8_width(m->list[i].label) + TICK_GAP);
        m->tick_next = now + (m->tick_pos ? TICK_MS : TICK_PAUSE);
    }
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */

/* Draws a label in w cells. One that is longer is cut off, or with pos > 0
 * runs through them like a news ticker: pos cells along, and coming around
 * again after a gap. */
static void draw_ticker(Screen *s, int x, int y, const char *label, int pos,
                        int w, int fg, int bg)
{
    size_t n = strlen(label), i = 0;
    int len = utf8_width(label), period = len + TICK_GAP, k, c;

    if (len <= w || pos == 0) {
        screen_puts(s, x, y, label, fg, bg, w);
        return;
    }
    for (k = 0; k < pos % period; k++)
        if (k < len)
            i = utf8_next(label, n, i);
    for (c = 0; c < w; c++) {
        unsigned long cp = ' ';
        if (k < len)
            i += utf8_decode(label + i, n - i, &cp);
        screen_put(s, x + c, y, cp, fg, bg);
        if (++k == period) {
            k = 0;
            i = 0;
        }
    }
}

/* Row i of the list menu, a w cells wide menu at column x. Folders show
 * their arrow and are colored like directories in the Open dialog. */
static void draw_list_item(const MenuBar *m, Screen *s, const Theme *t, int i,
                           int x, int row, int w)
{
    const ListItem *it = &m->list[i];
    int sel = i == m->item, bg = sel ? t->sel_bg : t->text_bg;
    int fg = sel ? t->sel_fg : it->flags & LI_FOLDER ? t->dir : t->text_fg;
    int lx = x + 2 + indent(it);

    screen_fill(s, x + 1, row, w - 2, 1, ' ', fg, bg);
    if (it->flags & LI_CHECKED)
        screen_put(s, x + 2, row, 0x2713, sel ? t->sel_hot : t->hot, bg);
    if (it->flags & LI_FOLDER)
        screen_put(s, lx - 2, row, it->flags & LI_OPEN ? 0x25BC : 0x25BA, fg, bg);
    draw_ticker(s, lx, row, it->label, m->tick_item == i ? m->tick_pos : 0,
                label_room(m, it, w), fg, bg);
    screen_puts(s, x + w - 2 - utf8_width(it->keys), row, it->keys, fg, bg, 20);
}

void menu_draw(const MenuBar *m, Screen *s, const Theme *t, CmdState state,
               void *ctx)
{
    int i, x, y, w, h, o = m->open;

    screen_fill(s, 0, 0, s->cols, 1, ' ', t->bar_fg, t->bar_bg);
    for (i = 0; i < NMENUS; i++) {
        int open = i == o, bg = open ? t->text_bg : t->bar_bg;
        x = title_x(i);
        screen_put(s, x - 1, 0, ' ', t->bar_fg, bg);
        x += screen_label(s, x, 0, menus[i].title, open ? t->text_fg : t->bar_fg,
                          open ? t->hot : t->bar_hot, bg);
        screen_put(s, x, 0, ' ', t->bar_fg, bg);
    }
    if (o < 0)
        return;

    geometry(m, &x, &y, &w, &h);
    screen_fill(s, x, y, w, h, ' ', t->text_fg, t->text_bg);
    screen_frame(s, x, y, w, h, 0, t->frame, t->text_bg);
    for (i = m->top; i < m->top + h - 2; i++) {
        const MenuItem *it;
        int row = y + 1 + i - m->top, sel = i == m->item, st, fg, bg;
        if (o == LIST) {
            draw_list_item(m, s, t, i, x, row, w);
            continue;
        }
        it = &menus[o].items[i];
        if (!it->label) {
            screen_fill(s, x + 1, row, w - 2, 1, 0x2500, t->frame, t->text_bg);
            screen_put(s, x, row, 0x251C, t->frame, t->text_bg);
            screen_put(s, x + w - 1, row, 0x2524, t->frame, t->text_bg);
            continue;
        }
        st = state(ctx, it->cmd);
        fg = !(st & CMD_ENABLED) ? t->disabled : sel ? t->sel_fg : t->text_fg;
        bg = sel ? t->sel_bg : t->text_bg;
        screen_fill(s, x + 1, row, w - 2, 1, ' ', fg, bg);
        if (st & CMD_CHECKED)
            screen_put(s, x + 2, row, 0x2713, sel ? t->sel_hot : t->hot, bg);
        screen_label(s, x + 4, row, it->label, fg,
                     !(st & CMD_ENABLED) ? t->disabled : sel ? t->sel_hot : t->hot, bg);
        screen_puts(s, x + w - 2 - label_width(it->keys), row, it->keys, fg, bg, 20);
    }

    /* a menu too long for the screen gets a scrollbar on its right border */
    if (count(m) > m->rows) {
        Scrollbar sb = menu_scrollbar(m);
        screen_scrollbar(s, x + w - 1, y + 1, &sb, t->frame, t->text_bg);
    }
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */

static void step(MenuBar *m, int d)
{
    int n = count(m), k;
    int i = m->item >= 0 ? m->item : d > 0 ? n - 1 : 0;   /* -1: none yet */
    for (k = 0; k < n; k++) {
        i = (i + d + n) % n;
        if (selectable(m, i))
            break;
    }
    m->item = i;
    reveal(m);
}

/* Highlights the next list item after the current one that starts with
 * letter c. */
static void seek(MenuBar *m, int c)
{
    int k;
    for (k = 1; k <= m->nlist; k++) {
        int i = (m->item + k) % m->nlist;
        if (ascii_lower((unsigned char)m->list[i].label[0]) == c) {
            m->item = i;
            reveal(m);
            return;
        }
    }
}

/* Moves the highlight of the list menu to item i, kept in the list. */
static void jump(MenuBar *m, int i)
{
    m->item = i < 0 ? 0 : i >= m->nlist ? m->nlist - 1 : i;
    reveal(m);
}

static int list_key(MenuBar *m, SDL_Keycode sym)
{
    const ListItem *it = m->item >= 0 ? &m->list[m->item] : NULL;
    switch (sym) {
    case SDLK_ESCAPE:
    case SDLK_F10:
        menu_close(m);
        break;
    case SDLK_UP:       step(m, -1); break;
    case SDLK_DOWN:     step(m, 1); break;
    case SDLK_PAGEUP:   jump(m, m->item - m->rows); break;
    case SDLK_PAGEDOWN: jump(m, m->item < 0 ? m->rows - 1 : m->item + m->rows); break;
    case SDLK_HOME:     jump(m, 0); break;
    case SDLK_END:      jump(m, m->nlist - 1); break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return it ? CMD_LIST + m->item : CMD_NONE;
    case SDLK_RIGHT:
        /* opens a folder, or steps into an open one */
        if (it && (it->flags & LI_FOLDER) && !(it->flags & LI_OPEN))
            return CMD_LIST + m->item;
        if (it && m->item + 1 < m->nlist && m->list[m->item + 1].depth > it->depth)
            jump(m, m->item + 1);
        break;
    case SDLK_LEFT:
        /* closes a folder, or steps out to the one around it */
        if (it && (it->flags & LI_OPEN))
            menu_list_collapse(m, m->item);
        else if (it && it->depth > 0)
            jump(m, menu_list_parent(m, m->item));
        break;
    default:
        if (sym > ' ' && sym < 127)
            seek(m, ascii_lower((int)sym));
    }
    return CMD_NONE;
}

int menu_key(MenuBar *m, SDL_Keycode sym)
{
    int i;
    if (m->open == LIST)
        return list_key(m, sym);
    switch (sym) {
    case SDLK_ESCAPE:
    case SDLK_F10:
        menu_close(m);
        return CMD_NONE;
    case SDLK_LEFT:
    case SDLK_RIGHT:
        if (m->open < NMENUS)
            menu_open(m, m->open + (sym == SDLK_LEFT ? -1 : 1));
        return CMD_NONE;
    case SDLK_UP:
        step(m, -1);
        return CMD_NONE;
    case SDLK_DOWN:
        step(m, 1);
        return CMD_NONE;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return m->item >= 0 ? cmd_at(m, m->item) : CMD_NONE;
    }
    for (i = 0; i < menus[m->open].n; i++) {
        const char *l = menus[m->open].items[i].label;
        if (l && label_hotkey(l) == (int)sym)
            return menus[m->open].items[i].cmd;
    }
    return CMD_NONE;
}

int menu_click(MenuBar *m, int cx, int cy)
{
    int item = item_at(m, cx, cy), i = cy == 0 ? title_at(cx) : -1;
    if (item >= 0) {
        if (m->open == LIST)
            m->item = item;
        return cmd_at(m, item);                     /* CMD_NONE on a separator */
    }
    if (m->open >= 0 && count(m) > m->rows) {
        int x, y, w, h;
        geometry(m, &x, &y, &w, &h);
        if (cx == x + w - 1 && cy > y && cy < y + h - 1) {
            Scrollbar sb = menu_scrollbar(m);
            scroll(m, scrollbar_step(&sb, cy - y - 1));
            return CMD_NONE;
        }
    }
    if (i >= 0 && i != m->open)
        menu_open(m, i);
    else
        menu_close(m);
    return CMD_NONE;
}

void menu_hover(MenuBar *m, int cx, int cy)
{
    int item = item_at(m, cx, cy), i = cy == 0 ? title_at(cx) : -1;
    if (m->open < 0)
        return;
    if (item >= 0 && selectable(m, item))
        m->item = item;
    else if (i >= 0 && i != m->open && m->open < NMENUS)
        menu_open(m, i);
}

void menu_wheel(MenuBar *m, int lines)
{
    if (m->open < 0 || count(m) <= m->rows)
        return;
    scroll(m, lines);
    /* keep the highlight on a row that is shown */
    if (m->item >= 0 && m->item < m->top)
        m->item = m->top;
    if (m->item >= m->top + m->rows)
        m->item = m->top + m->rows - 1;
}
