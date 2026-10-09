/*
 * sys_win32.c - sys.h on Windows. The manifest (cedit.manifest) gives the
 * programs the UTF-8 code page, so the C library and the "A" calls take
 * file names in UTF-8, as cedit and SDL use them.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "sys.h"
#include "util.h"

#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Files up to this size are read rather than mapped. No program can
 * truncate a mapped file, and most save by truncating: they couldn't save
 * a file while cedit has it open. */
#define READ_MAX    ((size_t)16 * 1024 * 1024)
#define WRITE_MAX   (1u << 30)  /* the most a single read or write takes */

#define IS_SEP(c) ((c) == '/' || (c) == '\\')

/* The message for Windows error code e into err, without the final period,
 * as strerror gives it. */
static void win_error(char *err, size_t errlen, DWORD e)
{
    size_t n;
    char num[32];

    if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                        NULL, e, 0, err, (DWORD)errlen, NULL)) {
        sprintf(num, "Error %lu", (unsigned long)e);
        str_copy(err, errlen, num);
        return;
    }
    n = strlen(err);
    while (n && (err[n - 1] == '\n' || err[n - 1] == '\r' || err[n - 1] == ' ' ||
                 err[n - 1] == '.'))
        err[--n] = 0;
}

int sys_load(const char *path, char **data, size_t *len, int *mapped,
             char *err, size_t errlen)
{
    DWORD attr = GetFileAttributesA(path), e;
    HANDLE h;
    LARGE_INTEGER size;
    int disk;

    *data = NULL;
    *len = 0;
    *mapped = 0;
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        str_copy(err, errlen, "Is a directory");
        return -1;
    }
    /* others may still change, rename or delete it */
    h = CreateFileA(path, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            return 1;
        win_error(err, errlen, e);
        return -1;
    }
    size.QuadPart = 0;
    disk = GetFileType(h) == FILE_TYPE_DISK && GetFileSizeEx(h, &size);
    if (disk && (size_t)size.QuadPart > READ_MAX) {
        /* the mapping lasts after both handles are closed */
        HANDLE m = CreateFileMappingA(h, NULL, PAGE_READONLY, 0, 0, NULL);
        void *v = m ? MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0) : NULL;
        if (m)
            CloseHandle(m);
        if (v) {
            *data = (char *)v;
            *len = (size_t)size.QuadPart;
            *mapped = 1;
        }
    }
    if (!*data && !(disk && size.QuadPart == 0)) {
        /* small, or not mappable (a pipe, ...): read it into one heap block */
        size_t cap = disk ? (size_t)size.QuadPart + 1 : 65536, n = 0;
        char *d = (char *)xmalloc(cap);
        for (;;) {
            DWORD r, want;
            if (n == cap)
                d = (char *)xrealloc(d, cap *= 2);
            want = cap - n < WRITE_MAX ? (DWORD)(cap - n) : WRITE_MAX;
            if (!ReadFile(h, d + n, want, &r, NULL)) {
                e = GetLastError();
                if (e == ERROR_BROKEN_PIPE)     /* the end of a pipe */
                    break;
                win_error(err, errlen, e);
                free(d);
                CloseHandle(h);
                return -1;
            }
            if (r == 0)
                break;
            n += r;
        }
        if (n) {
            *data = d;
            *len = n;
        } else {
            free(d);
        }
    }
    CloseHandle(h);
    return 0;
}

void sys_unmap(char *data, size_t len)
{
    (void)len;
    UnmapViewOfFile(data);
}

int sys_open_temp(char *tmpl, const char *like)
{
    (void)like;     /* sys_replace gives the target's permissions back */
    if (_mktemp_s(tmpl, strlen(tmpl) + 1) != 0)
        return -1;
    return _open(tmpl, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                 _S_IREAD | _S_IWRITE);
}

int sys_open_write(const char *path)
{
    return _open(path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY,
                 _S_IREAD | _S_IWRITE);
}

int sys_writable(const char *path)
{
    return _access(path, 2) < 0 && errno != ENOENT ? -1 : 0;
}

static int write_out(int fd, const char *p, size_t n)
{
    while (n) {
        int w = _write(fd, p, n < WRITE_MAX ? (unsigned)n : WRITE_MAX);
        if (w < 0)
            return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

/* Windows has no writev for files: the chunks are gathered a megabyte to
 * a write, but for the big ones. */
int sys_write(int fd, const SysChunk *c, int n)
{
    size_t cap = (size_t)1 << 20, len = 0;
    char *buf = (char *)xmalloc(cap);
    int i, r = 0, e;

    for (i = 0; i < n && r == 0; i++) {
        if (len + c[i].n > cap) {
            r = write_out(fd, buf, len);
            len = 0;
        }
        if (r == 0 && c[i].n > cap) {
            r = write_out(fd, c[i].p, c[i].n);
        } else if (r == 0) {
            memcpy(buf + len, c[i].p, c[i].n);
            len += c[i].n;
        }
    }
    if (r == 0)
        r = write_out(fd, buf, len);
    e = errno;
    free(buf);
    errno = e;
    return r;
}

int sys_sync(int fd)
{
    return _commit(fd);
}

int sys_close(int fd)
{
    return _close(fd);
}

/* ReplaceFile keeps the target's attributes, permissions and creation
 * time. */
int sys_replace(const char *tmp, const char *target, char *err, size_t errlen)
{
    DWORD e;

    if (ReplaceFileA(target, tmp, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL))
        return 0;
    e = GetLastError();
    if (e == ERROR_FILE_NOT_FOUND) {    /* a new file */
        if (MoveFileExA(tmp, target, MOVEFILE_WRITE_THROUGH))
            return 0;
        e = GetLastError();
    }
    win_error(err, errlen, e);
    return -1;
}

void sys_fix_slashes(char *path)
{
    for (; *path; path++)
        if (*path == '\\')
            *path = '/';
}

size_t sys_root_len(const char *p)
{
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':')
        return IS_SEP(p[2]) ? 3 : 2;
    if (IS_SEP(p[0]) && IS_SEP(p[1])) {
        size_t i, seps = 0;
        for (i = 2; p[i]; i++)
            if (IS_SEP(p[i]) && ++seps == 2)
                return i + 1;
        return i;
    }
    return IS_SEP(p[0]) ? 1 : 0;
}

char *sys_full_path(const char *path)
{
    char *p = _fullpath(NULL, path, 0);
    if (p)
        sys_fix_slashes(p);
    return p;
}

int sys_cwd(char *out, size_t size)
{
    if (!_getcwd(out, (int)size))
        return -1;
    sys_fix_slashes(out);
    return 0;
}

const char *sys_home(void)
{
    const char *home = getenv("HOME");
    return home ? home : getenv("USERPROFILE");
}

int sys_config_dir(char *out, size_t size)
{
    const char *appdata = getenv("APPDATA");

    if (!appdata || !*appdata || strlen(appdata) >= size)
        return 0;
    strcpy(out, appdata);
    sys_fix_slashes(out);
    return 1;
}

void sys_mkdir(const char *path)
{
    _mkdir(path);
}

int sys_kind(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES)
        return -1;
    return (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

/* Windows has no inode numbers: a and b are the same file when their full
 * paths are the same, but for case, as Windows takes them. */
int sys_same_file(const char *a, const char *b)
{
    char *fa = _fullpath(NULL, a, 0), *fb = _fullpath(NULL, b, 0);
    WCHAR wa[4096 + 256], wb[4096 + 256];
    int same = fa && fb &&
               MultiByteToWideChar(CP_ACP, 0, fa, -1, wa, (int)(sizeof wa / sizeof wa[0])) &&
               MultiByteToWideChar(CP_ACP, 0, fb, -1, wb, (int)(sizeof wb / sizeof wb[0])) &&
               CompareStringOrdinal(wa, -1, wb, -1, TRUE) == CSTR_EQUAL;
    free(fa);
    free(fb);
    return same;
}

const char *sys_temp_dir(void)
{
    static char dir[MAX_PATH + 1];
    const char *t = getenv("TEMP");
    str_copy(dir, sizeof dir, t ? t : ".");
    sys_fix_slashes(dir);
    return dir;
}

const char sys_null_file[] = "NUL";

struct SysDir {
    HANDLE h;               /* INVALID_HANDLE_VALUE: the folder is empty */
    WIN32_FIND_DATAA f;
    int have;               /* f is an entry sys_dir_next has yet to give */
};

SysDir *sys_dir_open(const char *dir)
{
    SysDir *d = (SysDir *)xmalloc(sizeof *d);
    char pattern[4096 + 8];
    size_t n;

    str_copy(pattern, sizeof pattern - 2, dir);
    n = strlen(pattern);
    if (!n || !IS_SEP(pattern[n - 1]))
        strcat(pattern, "/");
    strcat(pattern, "*");
    d->h = FindFirstFileA(pattern, &d->f);
    d->have = d->h != INVALID_HANDLE_VALUE;
    /* (an empty drive has not even "." and "..") */
    if (!d->have && GetLastError() != ERROR_FILE_NOT_FOUND) {
        free(d);
        return NULL;
    }
    return d;
}

int sys_dir_next(SysDir *d, SysEntry *e)
{
    DWORD a;

    for (;;) {
        if (!d->have &&
            (d->h == INVALID_HANDLE_VALUE || !FindNextFileA(d->h, &d->f)))
            return 0;
        d->have = 0;
        a = d->f.dwFileAttributes;
        if (!(a & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) ||
            strcmp(d->f.cFileName, "..") == 0)
            break;
    }
    e->name = d->f.cFileName;
    e->folder = (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
    e->file = !e->folder;
    e->size = (double)d->f.nFileSizeHigh * 4294967296.0 + (double)d->f.nFileSizeLow;
    return 1;
}

void sys_dir_close(SysDir *d)
{
    if (d->h != INVALID_HANDLE_VALUE)
        FindClose(d->h);
    free(d);
}

void sys_report(const char *msg, int error)
{
    MessageBoxA(NULL, msg, "cedit", error ? MB_ICONERROR : MB_ICONINFORMATION);
}
