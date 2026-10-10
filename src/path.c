/*
 * path.c - path building and sorted directory listings.
 */
#define _XOPEN_SOURCE 700
#include "path.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#define getcwd(buf, size) _getcwd(buf, (int)(size))
#define IS_SEP(c) ((c) == '/' || (c) == '\\')
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#define IS_SEP(c) ((c) == '/')
#endif

/* The length of the root path starts with: "/", or on Windows also "C:/",
 * "C:" (C:'s working directory) or "//server/share/"; 0 if it is relative. */
static size_t root_len(const char *p)
{
#ifdef _WIN32
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':')
        return IS_SEP(p[2]) ? 3 : 2;
    if (IS_SEP(p[0]) && IS_SEP(p[1])) {
        size_t i, seps = 0;
        for (i = 2; p[i]; i++)
            if (IS_SEP(p[i]) && ++seps == 2)
                return i + 1;
        return i;
    }
#endif
    return IS_SEP(p[0]) ? 1 : 0;
}

void path_join(char *out, size_t size, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    str_copy(out, size, dir);
    if (!n || !IS_SEP(dir[n - 1]))
        str_cat(out, size, "/");
    str_cat(out, size, name);
}

void path_resolve(char *out, size_t size, const char *dir, const char *name)
{
    const char *home = getenv("HOME");
#ifdef _WIN32
    if (!home)
        home = getenv("USERPROFILE");
#endif
    if (root_len(name)) {
        str_copy(out, size, name);
    } else if (name[0] == '~' && (IS_SEP(name[1]) || !name[1]) && home) {
        str_copy(out, size, home);
        str_cat(out, size, name + 1);
    } else {
        path_join(out, size, dir, name);
    }
    fix_slashes(out);
}

void path_full(char *out, size_t size, const char *path)
{
#ifdef _WIN32
    char *rp = _fullpath(NULL, path, 0);
#else
    char *rp = realpath(path, NULL);
#endif
    str_copy(out, size, rp ? rp : path);
    free(rp);
    fix_slashes(out);
}

void path_dir(char *out, size_t size, const char *path)
{
    if (path) {
        char *slash;
        path_full(out, size, path);
        slash = strrchr(out, '/');
        if (slash && (size_t)(slash - out) >= root_len(out))
            *slash = 0;
        else if (slash)
            slash[1] = 0;
        else if (!getcwd(out, size))
            str_copy(out, size, ".");
    } else if (!getcwd(out, size)) {
        str_copy(out, size, ".");
    }
    fix_slashes(out);
}

int path_kind(const char *path)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES)
        return -1;
    return (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    return S_ISDIR(st.st_mode) != 0;
#endif
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

/* Whether dir_list lists entry name of dir: not "." nor a dotfile, but
 * ".." if parent is set and dir is not the root. */
static int listed(const char *dir, int parent, const char *name)
{
    return name[0] != '.' ||
           (parent && strcmp(name, "..") == 0 && root_len(dir) != strlen(dir));
}

static void add_entry(DirEntry **ents, int *n, const char *name, int folder,
                      int file, double size, int current)
{
    size_t len = strlen(name);
    DirEntry *de;

    /* the capacity is n rounded up to a power of two */
    if ((*n & (*n - 1)) == 0)
        *ents = (DirEntry *)xrealloc(*ents, (size_t)(*n ? 2 * *n : 1) * sizeof **ents);
    de = &(*ents)[(*n)++];
    de->name = (char *)xmalloc(len + 2);
    memcpy(de->name, name, len + 1);
    if (folder)
        strcat(de->name, "/");
    de->folder = folder;
    de->file = file;
    de->size = size;
    de->current = current;
}

#ifdef _WIN32
/* Whether a and b are the same path, ignoring case as Windows does. */
static int same_path(const char *a, const char *b)
{
    WCHAR wa[4096 + 256], wb[4096 + 256];
    return MultiByteToWideChar(CP_ACP, 0, a, -1, wa, (int)(sizeof wa / sizeof wa[0])) &&
           MultiByteToWideChar(CP_ACP, 0, b, -1, wb, (int)(sizeof wb / sizeof wb[0])) &&
           CompareStringOrdinal(wa, -1, wb, -1, TRUE) == CSTR_EQUAL;
}

int dir_list(const char *dir, int parent, const char *mark, DirEntry **out)
{
    DirEntry *ents = NULL;
    int n = 0;
    char pattern[4096 + 8], full[4096 + 256], cur[4096 + 256];
    WIN32_FIND_DATAA f;
    HANDLE h;

    *out = NULL;
    if (mark)
        path_full(cur, sizeof cur, mark);
    path_join(pattern, sizeof pattern, dir, "*");
    h = FindFirstFileA(pattern, &f);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    do {
        int folder = (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        int hidden = (f.dwFileAttributes &
                      (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
        if (!listed(dir, parent, f.cFileName) ||
            (hidden && strcmp(f.cFileName, "..") != 0))
            continue;
        path_join(full, sizeof full, dir, f.cFileName);
        add_entry(&ents, &n, f.cFileName, folder, !folder,
                  (double)f.nFileSizeHigh * 4294967296.0 + (double)f.nFileSizeLow,
                  mark && !folder && same_path(full, cur));
    } while (FindNextFileA(h, &f));
    FindClose(h);
    qsort(ents, (size_t)n, sizeof *ents, cmp_entries);
    *out = ents;
    return n;
}
#else
int dir_list(const char *dir, int parent, const char *mark, DirEntry **out)
{
    DirEntry *ents = NULL;
    int n = 0;
    DIR *d = opendir(dir);
    struct dirent *e;
    struct stat cur;
    int have_cur = mark && stat(mark, &cur) == 0;

    *out = NULL;
    if (!d)
        return -1;
    while ((e = readdir(d)) != NULL) {
        char full[4096 + 256];
        struct stat st;
        if (!listed(dir, parent, e->d_name))
            continue;
        path_join(full, sizeof full, dir, e->d_name);
        if (stat(full, &st) != 0)
            memset(&st, 0, sizeof st);
        add_entry(&ents, &n, e->d_name, S_ISDIR(st.st_mode), S_ISREG(st.st_mode),
                  (double)st.st_size,
                  have_cur && S_ISREG(st.st_mode) && st.st_dev == cur.st_dev &&
                      st.st_ino == cur.st_ino);
    }
    closedir(d);
    qsort(ents, (size_t)n, sizeof *ents, cmp_entries);
    *out = ents;
    return n;
}
#endif

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
