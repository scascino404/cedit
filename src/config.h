/*
 * config.h - user settings, kept in $XDG_CONFIG_HOME/cedit/cedit.conf
 * (or ~/.config/cedit/cedit.conf) as "key = value" lines. The CEDIT_CONFIG
 * environment variable overrides the path; set it empty to disable.
 */
#ifndef CEDIT_CONFIG_H
#define CEDIT_CONFIG_H

typedef struct Config {
    int size;           /* SIZE_SMALL, SIZE_NORMAL, SIZE_LARGE */
    int dark;
    int autoindent;
    int line_numbers;
    int tab_width;
    int highlight;      /* syntax highlighting */
} Config;

/* Fills c with the defaults, then whatever the config file overrides. */
void config_load(Config *c);
void config_save(const Config *c);

#endif
