/*
 * path.c - path building and sorted directory listings.
 */
#include "path.h"
#include "sys.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void path_join(char *out, size_t size, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    str_copy(out, size, dir);
    if (!n || dir[n - 1] != '/')
        str_cat(out, size, "/");
    str_cat(out, size, name);
}

void path_resolve(char *out, size_t size, const char *dir, const char *name)
{
    const char *home = sys_home();
    char *nm = xstrdup(name);

    sys_fix_slashes(nm);
    if (sys_root_len(nm)) {
        str_copy(out, size, nm);
    } else if (nm[0] == '~' && (nm[1] == '/' || !nm[1]) && home) {
        str_copy(out, size, home);
        str_cat(out, size, nm + 1);
    } else {
        path_join(out, size, dir, nm);
    }
    sys_fix_slashes(out);
    free(nm);
}

void path_full(char *out, size_t size, const char *path)
{
    char *rp = sys_full_path(path);
    str_copy(out, size, rp ? rp : path);
    free(rp);
    sys_fix_slashes(out);
}

void path_dir(char *out, size_t size, const char *path)
{
    if (path) {
        char *slash;
        path_full(out, size, path);
        slash = strrchr(out, '/');
        if (slash && (size_t)(slash - out) >= sys_root_len(out))
            *slash = 0;
        else if (slash)
            slash[1] = 0;
        else if (sys_cwd(out, size) < 0)
            str_copy(out, size, ".");
    } else if (sys_cwd(out, size) < 0) {
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

int dir_list(const char *dir, int parent, const char *mark, DirEntry **out)
{
    DirEntry *ents = NULL;
    int n = 0;
    SysDir *d = sys_dir_open(dir);
    SysEntry e;

    *out = NULL;
    if (!d)
        return -1;
    while (sys_dir_next(d, &e)) {
        char full[PATH_LEN + 256];
        size_t len = strlen(e.name);
        DirEntry *de;
        if (e.name[0] == '.' &&
            !(parent && strcmp(e.name, "..") == 0 && sys_root_len(dir) != strlen(dir)))
            continue;
        ents = (DirEntry *)xgrow(ents, n, sizeof *ents);
        de = &ents[n++];
        de->name = (char *)xmalloc(len + 2);
        memcpy(de->name, e.name, len + 1);
        if (e.folder)
            strcat(de->name, "/");
        de->folder = e.folder;
        de->file = e.file;
        de->size = e.size;
        de->current = 0;
        if (mark && e.file) {
            path_join(full, sizeof full, dir, e.name);
            de->current = sys_same_file(full, mark);
        }
    }
    sys_dir_close(d);
    if (n)
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
