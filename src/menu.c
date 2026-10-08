/*
 * menu.c - menu tables, drawing and input.
 */
#include "menu.h"

#include <stddef.h>

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

static void geometry(int m, int *x, int *y, int *w, int *h)
{
    int i, lw = 0, kw = 0;
    for (i = 0; i < menus[m].n; i++) {
        const MenuItem *it = &menus[m].items[i];
        if (!it->label)
            continue;
        if (label_width(it->label) > lw)
            lw = label_width(it->label);
        if (label_width(it->keys) > kw)
            kw = label_width(it->keys);
    }
    *x = title_x(m) - 1;
    *y = 1;
    *w = lw + kw + 8;
    *h = menus[m].n + 2;
}

/* The item of the open menu at a cell, or -1. */
static int item_at(const MenuBar *m, int cx, int cy)
{
    int x, y, w, h;
    if (m->open < 0)
        return -1;
    geometry(m->open, &x, &y, &w, &h);
    if (cx > x && cx < x + w - 1 && cy > y && cy < y + h - 1)
        return cy - y - 1;
    return -1;
}

void menu_open(MenuBar *m, int i)
{
    m->open = (i + NMENUS) % NMENUS;
    m->item = 0;
}

void menu_close(MenuBar *m)
{
    m->open = -1;
}

int menu_with_hotkey(SDL_Keycode sym)
{
    int i;
    for (i = 0; i < NMENUS; i++)
        if (label_hotkey(menus[i].title) == (int)sym)
            return i;
    return -1;
}

static void step(MenuBar *m, int d)
{
    int n = menus[m->open].n, i = m->item, k;
    for (k = 0; k < n; k++) {
        i = (i + d + n) % n;
        if (menus[m->open].items[i].label)
            break;
    }
    m->item = i;
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

    geometry(o, &x, &y, &w, &h);
    screen_fill(s, x, y, w, h, ' ', t->text_fg, t->text_bg);
    screen_frame(s, x, y, w, h, 0, t->frame, t->text_bg);
    for (i = 0; i < menus[o].n; i++) {
        const MenuItem *it = &menus[o].items[i];
        int row = y + 1 + i, sel = i == m->item, st, fg, bg;
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
    screen_shadow(s, x + w, y + 1, 2, h);
    screen_shadow(s, x + 2, y + h, w - 2, 1);
}

int menu_key(MenuBar *m, SDL_Keycode sym)
{
    int i;
    switch (sym) {
    case SDLK_ESCAPE:
    case SDLK_F10:
        menu_close(m);
        return CMD_NONE;
    case SDLK_LEFT:
        menu_open(m, m->open - 1);
        return CMD_NONE;
    case SDLK_RIGHT:
        menu_open(m, m->open + 1);
        return CMD_NONE;
    case SDLK_UP:
        step(m, -1);
        return CMD_NONE;
    case SDLK_DOWN:
        step(m, 1);
        return CMD_NONE;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return menus[m->open].items[m->item].cmd;
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
    if (item >= 0)
        return menus[m->open].items[item].cmd;    /* CMD_NONE on a separator */
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
    if (item >= 0 && menus[m->open].items[item].label)
        m->item = item;
    else if (i >= 0 && i != m->open)
        menu_open(m, i);
}
