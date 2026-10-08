/*
 * theme.h - the color themes: each UI element mapped to one of the 16
 * palette colors.
 */
#ifndef CEDIT_THEME_H
#define CEDIT_THEME_H

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

extern const Theme theme_light, theme_dark;

#endif
