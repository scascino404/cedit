/*
 * path.c - path building and sorted directory listings.
 */
#define _XOPEN_SOURCE 700
#include "path.h"
#include "util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void path_join(char *out, size_t size, const char *dir, const char *name)
{
    str_copy(out, size, dir);
    if (strcmp(dir, "/") != 0)
        str_cat(out, size, "/");
    str_cat(out, size, name);
}

void path_resolve(char *out, size_t size, const char *dir, const char *name)
{
    const char *home = getenv("HOME");
    if (name[0] == '/') {
        str_copy(out, size, name);
    } else if (name[0] == '~' && (name[1] == '/' || !name[1]) && home) {
        str_copy(out, size, home);
        str_cat(out, size, name + 1);
    } else {
        path_join(out, size, dir, name);
    }
}

void path_dir(char *out, size_t size, const char *path)
{
    if (path) {
        char *rp = realpath(path, NULL), *slash;
        str_copy(out, size, rp ? rp : path);
        free(rp);
        slash = strrchr(out, '/');
        if (slash && slash != out)
            *slash = 0;
        else if (slash)
            slash[1] = 0;
        else if (!getcwd(out, size))
            str_copy(out, size, ".");
    } else if (!getcwd(out, size)) {
        str_copy(out, size, ".");
    }
}

static int cmp_entries(const void *pa, const void *pb)
{
    const char *x = ((const DirEntry *)pa)->name, *y = ((const DirEntry *)pb)->name;
    int dx = x[strlen(x) - 1] == '/', dy = y[strlen(y) - 1] == '/';
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

int dir_list(const char *dir, int parent, DirEntry **out)
{
    DirEntry *ents = NULL;
    int n = 0;
    DIR *d = opendir(dir);
    struct dirent *e;

    *out = NULL;
    if (!d)
        return -1;
    while ((e = readdir(d)) != NULL) {
        char full[4096 + 256];
        size_t len = strlen(e->d_name);
        DirEntry *de;
        if (e->d_name[0] == '.' &&
            !(parent && strcmp(e->d_name, "..") == 0 && strcmp(dir, "/") != 0))
            continue;
        /* the capacity is n rounded up to a power of two */
        if ((n & (n - 1)) == 0)
            ents = (DirEntry *)xrealloc(ents, (size_t)(n ? 2 * n : 1) * sizeof *ents);
        de = &ents[n++];
        path_join(full, sizeof full, dir, e->d_name);
        if (stat(full, &de->st) != 0)
            memset(&de->st, 0, sizeof de->st);
        de->name = (char *)xmalloc(len + 2);
        memcpy(de->name, e->d_name, len + 1);
        if (S_ISDIR(de->st.st_mode))
            strcat(de->name, "/");
    }
    closedir(d);
    qsort(ents, (size_t)n, sizeof *ents, cmp_entries);
    *out = ents;
    return n;
}

void dir_free(DirEntry *ents, int n)
{
    int i;
    for (i = 0; i < n; i++)
        free(ents[i].name);
    free(ents);
}

void format_size(char *out, double n)
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
