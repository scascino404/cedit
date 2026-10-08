/*
 * config.c - load and save the settings file.
 */
#define _XOPEN_SOURCE 700
#include "config.h"
#include "screen.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const struct {
    const char *key;
    size_t off;
} keys[] = {
    {"size", offsetof(Config, size)},
    {"dark", offsetof(Config, dark)},
    {"autoindent", offsetof(Config, autoindent)},
    {"line_numbers", offsetof(Config, line_numbers)},
    {"tab_width", offsetof(Config, tab_width)}
};
#define NKEYS (sizeof keys / sizeof keys[0])

static const char *const size_names[SIZE_COUNT] = {"small", "normal", "large"};

/* Writes the config file path into buf; returns 0 if there is none. With
 * mkdirs set, creates the directories leading to it. */
static int config_path(char *buf, size_t n, int mkdirs)
{
    const char *env = getenv("CEDIT_CONFIG");
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char dir[4096];

    if (env) {
        if (!*env || strlen(env) >= n)
            return 0;
        strcpy(buf, env);
        return 1;
    }
    if (xdg && *xdg && strlen(xdg) < sizeof dir - 16)
        sprintf(dir, "%s", xdg);
    else if (home && *home && strlen(home) < sizeof dir - 16)
        sprintf(dir, "%s/.config", home);
    else
        return 0;
    if (mkdirs)
        mkdir(dir, 0755);
    strcat(dir, "/cedit");
    if (mkdirs)
        mkdir(dir, 0755);
    if (strlen(dir) + 16 > n)
        return 0;
    sprintf(buf, "%s/cedit.conf", dir);
    return 1;
}

void config_load(Config *c)
{
    char path[4200], line[256];
    FILE *f;

    c->size = SIZE_LARGE;
    c->dark = 0;
    c->autoindent = 0;
    c->line_numbers = 0;
    c->tab_width = 4;
    if (!config_path(path, sizeof path, 0) || !(f = fopen(path, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        char key[64], val[64];
        size_t i;
        if (sscanf(line, " %63[a-z_] = %63s", key, val) != 2)
            continue;
        for (i = 0; i < NKEYS; i++) {
            int *field = (int *)((char *)c + keys[i].off), v, k;
            if (strcmp(key, keys[i].key) != 0)
                continue;
            v = atoi(val);
            if (strcmp(val, "true") == 0 || strcmp(val, "on") == 0)
                v = 1;
            for (k = 0; k < SIZE_COUNT; k++)
                if (strcmp(val, size_names[k]) == 0)
                    v = k;
            *field = v;
        }
    }
    fclose(f);
    if (c->size < 0 || c->size >= SIZE_COUNT)
        c->size = SIZE_LARGE;
    if (c->tab_width < 1 || c->tab_width > 16)
        c->tab_width = 4;
}

void config_save(const Config *c)
{
    char path[4200];
    FILE *f;

    if (!config_path(path, sizeof path, 1) || !(f = fopen(path, "w")))
        return;
    fprintf(f, "# cedit settings\n");
    fprintf(f, "size = %s\n", size_names[c->size]);
    fprintf(f, "dark = %s\n", c->dark ? "true" : "false");
    fprintf(f, "autoindent = %s\n", c->autoindent ? "true" : "false");
    fprintf(f, "line_numbers = %s\n", c->line_numbers ? "true" : "false");
    fprintf(f, "tab_width = %d\n", c->tab_width);
    fclose(f);
}
