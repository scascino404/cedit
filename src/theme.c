/*
 * theme.c - the light (TempleOS) and dark color themes.
 */
#include "theme.h"
#include "screen.h"

/* TempleOS: blue ink on white paper */
const Theme theme_light = {
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
const Theme theme_dark = {
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
