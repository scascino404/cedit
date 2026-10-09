/*
 * sys_posix.c - sys.h on Linux, macOS and the other POSIX systems.
 */
#define _XOPEN_SOURCE 700
#include "sys.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

int sys_load(const char *path, char **data, size_t *len, int *mapped,
             char *err, size_t errlen)
{
    int fd;
    struct stat st;

    *data = NULL;
    *len = 0;
    *mapped = 0;
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT)
            return 1;
        str_copy(err, errlen, strerror(errno));
        return -1;
    }
    if (fstat(fd, &st) < 0) {
        str_copy(err, errlen, strerror(errno));
        close(fd);
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        str_copy(err, errlen, "Is a directory");
        close(fd);
        return -1;
    }
    if (S_ISREG(st.st_mode) && st.st_size > 0) {
        void *m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (m != MAP_FAILED) {
#ifdef POSIX_MADV_SEQUENTIAL
            posix_madvise(m, (size_t)st.st_size, POSIX_MADV_SEQUENTIAL);
#endif
            *data = (char *)m;
            *len = (size_t)st.st_size;
            *mapped = 1;
        }
    }
    if (!*data && !(S_ISREG(st.st_mode) && st.st_size == 0)) {
        /* Not mappable (pipe, /proc, ...): read it into one heap block. */
        size_t cap = 65536, n = 0;
        char *d = (char *)xmalloc(cap);
        for (;;) {
            ssize_t r;
            if (n == cap)
                d = (char *)xrealloc(d, cap *= 2);
            r = read(fd, d + n, cap - n);
            if (r < 0 && errno == EINTR)
                continue;
            if (r < 0) {
                str_copy(err, errlen, strerror(errno));
                free(d);
                close(fd);
                return -1;
            }
            if (r == 0)
                break;
            n += (size_t)r;
        }
        if (n) {
            *data = d;
            *len = n;
        } else {
            free(d);
        }
    }
    close(fd);
    return 0;
}

void sys_unmap(char *data, size_t len)
{
    munmap(data, len);
}

int sys_open_temp(char *tmpl, const char *like)
{
    struct stat st;
    mode_t mode;
    int fd = mkstemp(tmpl);

    if (fd < 0)
        return -1;
    if (like && stat(like, &st) == 0) {
        mode = st.st_mode & 07777;
    } else {
        mode_t um = umask(0);
        umask(um);
        mode = 0666 & ~um;
    }
    fchmod(fd, mode);
    return fd;
}

int sys_open_write(const char *path)
{
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
}

int sys_writable(const char *path)
{
    return access(path, W_OK) < 0 && errno != ENOENT ? -1 : 0;
}

int sys_write(int fd, const SysChunk *c, int n)
{
    struct iovec iov[SYS_CHUNKS];
    int k;

    for (k = 0; k < n; k++) {
        iov[k].iov_base = (void *)c[k].p;
        iov[k].iov_len = c[k].n;
    }
    for (k = 0; k < n;) {
        ssize_t w = writev(fd, iov + k, n - k);
        if (w < 0 && errno == EINTR)
            continue;
        if (w < 0)
            return -1;
        for (; k < n && (size_t)w >= iov[k].iov_len; k++)
            w -= (ssize_t)iov[k].iov_len;
        if (k < n) {
            iov[k].iov_base = (char *)iov[k].iov_base + w;
            iov[k].iov_len -= (size_t)w;
        }
    }
    return 0;
}

int sys_sync(int fd)
{
    return fsync(fd);
}

int sys_close(int fd)
{
    return close(fd);
}

int sys_replace(const char *tmp, const char *target, char *err, size_t errlen)
{
    if (rename(tmp, target) == 0)
        return 0;
    str_copy(err, errlen, strerror(errno));
    return -1;
}

void sys_fix_slashes(char *path)
{
    (void)path;
}

size_t sys_root_len(const char *path)
{
    return path[0] == '/';
}

char *sys_full_path(const char *path)
{
    return realpath(path, NULL);
}

int sys_cwd(char *out, size_t size)
{
    return getcwd(out, size) ? 0 : -1;
}

const char *sys_home(void)
{
    return getenv("HOME");
}

int sys_config_dir(char *out, size_t size)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");

    if (xdg && *xdg && strlen(xdg) < size)
        strcpy(out, xdg);
    else if (home && *home && strlen(home) + 8 < size)
        sprintf(out, "%s/.config", home);
    else
        return 0;
    return 1;
}

void sys_mkdir(const char *path)
{
    mkdir(path, 0755);
}

int sys_kind(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    return S_ISDIR(st.st_mode) != 0;
}

int sys_same_file(const char *a, const char *b)
{
    struct stat sa, sb;
    return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_dev == sb.st_dev &&
           sa.st_ino == sb.st_ino;
}

const char *sys_temp_dir(void)
{
    return "/tmp";
}

const char sys_null_file[] = "/dev/null";

struct SysDir {
    DIR *d;
    char path[4096 + 256];  /* the folder, a '/', then the entry's name */
    size_t dlen;            /* up to the entry's name */
};

SysDir *sys_dir_open(const char *dir)
{
    DIR *d = opendir(dir);
    SysDir *sd;

    if (!d)
        return NULL;
    sd = (SysDir *)xmalloc(sizeof *sd);
    sd->d = d;
    str_copy(sd->path, sizeof sd->path - 1, dir);
    sd->dlen = strlen(sd->path);
    if (!sd->dlen || sd->path[sd->dlen - 1] != '/')
        sd->path[sd->dlen++] = '/';
    return sd;
}

int sys_dir_next(SysDir *d, SysEntry *e)
{
    struct dirent *de = readdir(d->d);
    struct stat st;

    if (!de)
        return 0;
    str_copy(d->path + d->dlen, sizeof d->path - d->dlen, de->d_name);
    if (stat(d->path, &st) != 0)
        memset(&st, 0, sizeof st);
    e->name = de->d_name;
    e->folder = S_ISDIR(st.st_mode) != 0;
    e->file = S_ISREG(st.st_mode) != 0;
    e->size = (double)st.st_size;
    return 1;
}

void sys_dir_close(SysDir *d)
{
    closedir(d->d);
    free(d);
}

void sys_report(const char *msg, int error)
{
    if (error)
        fprintf(stderr, "cedit: %s\n", msg);
    else
        puts(msg);
}
