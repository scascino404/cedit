/*
 * theme.c - the light (TempleOS) and dark color themes.
 */
#include "theme.h"
#include "screen.h"

/* TempleOS: black ink on white paper, with blue frames and bars */
const Theme theme_light = {
    BLACK, WHITE,                   /* text */
    WHITE, BLUE,                    /* selection */
    BLUE,                           /* frame */
    WHITE, BLUE,                    /* title */
    WHITE, BLUE, YELLOW,            /* bars */
    RED, YELLOW,                    /* hotkeys */
    LIGHTGRAY, DARKGRAY, BLUE,      /* disabled, line numbers */
    WHITE, RED, MAGENTA,            /* control characters */
    RED,                            /* bad */
    BLACK, LIGHTCYAN,               /* field */
    BLACK, YELLOW,                  /* focus */
    RED,                            /* directories */
    YELLOW, BLACK, RED,             /* cursor */
    /* normal, keyword, type, comment, string, number, preprocessor */
    {BLACK, BLUE, BLUE, GREEN, BROWN, BLACK, BLUE}
};

/* Dark mode: white on black, same blue bars and yellow block cursor */
const Theme theme_dark = {
    WHITE, BLACK,                   /* text */
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
    YELLOW, BLACK, LIGHTRED,        /* cursor */
    {WHITE, LIGHTBLUE, LIGHTBLUE, LIGHTGREEN, YELLOW, WHITE, LIGHTBLUE}
};
