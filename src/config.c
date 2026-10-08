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

enum { BOOL, INT, SIZE };

/* Adding a setting: a field in Config, a default and a row here. */
static const Config defaults = {SIZE_LARGE, 0, 0, 0, 4};

static const struct {
    const char *key;
    size_t off;
    int type;
} keys[] = {
    {"size", offsetof(Config, size), SIZE},
    {"dark", offsetof(Config, dark), BOOL},
    {"autoindent", offsetof(Config, autoindent), BOOL},
    {"line_numbers", offsetof(Config, line_numbers), BOOL},
    {"tab_width", offsetof(Config, tab_width), INT}
};
#define NKEYS (sizeof keys / sizeof keys[0])

static const char *const size_names[SIZE_COUNT] = {"small", "normal", "large"};

static int *field(Config *c, size_t i)
{
    return (int *)((char *)c + keys[i].off);
}

static int parse_value(int type, const char *val)
{
    int k;
    if (type == BOOL && (strcmp(val, "true") == 0 || strcmp(val, "on") == 0))
        return 1;
    if (type == SIZE)
        for (k = 0; k < SIZE_COUNT; k++)
            if (strcmp(val, size_names[k]) == 0)
                return k;
    return atoi(val);
}

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

    *c = defaults;
    if (!config_path(path, sizeof path, 0) || !(f = fopen(path, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        char key[64], val[64];
        size_t i;
        if (sscanf(line, " %63[a-z_] = %63s", key, val) != 2)
            continue;
        for (i = 0; i < NKEYS; i++)
            if (strcmp(key, keys[i].key) == 0)
                *field(c, i) = parse_value(keys[i].type, val);
    }
    fclose(f);
    if (c->size < 0 || c->size >= SIZE_COUNT)
        c->size = defaults.size;
    if (c->tab_width < 1 || c->tab_width > 16)
        c->tab_width = defaults.tab_width;
}

void config_save(const Config *c)
{
    char path[4200];
    FILE *f;
    size_t i;

    if (!config_path(path, sizeof path, 1) || !(f = fopen(path, "w")))
        return;
    fprintf(f, "# cedit settings\n");
    for (i = 0; i < NKEYS; i++) {
        int v = *(const int *)((const char *)c + keys[i].off);
        fprintf(f, "%s = ", keys[i].key);
        if (keys[i].type == BOOL)
            fprintf(f, "%s\n", v ? "true" : "false");
        else if (keys[i].type == SIZE)
            fprintf(f, "%s\n", size_names[v]);
        else
            fprintf(f, "%d\n", v);
    }
    fclose(f);
}
