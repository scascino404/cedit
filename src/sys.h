/*
 * sys.h - what cedit needs from the operating system, besides SDL: files,
 * paths and folders. sys_posix.c is it on Linux and macOS, sys_win32.c on
 * Windows; the rest of cedit is the same on all of them.
 *
 * Paths use '/', which Windows takes too. There, the backslashes in paths
 * from the system and from the user are turned into slashes, and roots are
 * "C:/" and "//server/share/".
 */
#ifndef CEDIT_SYS_H
#define CEDIT_SYS_H

#include <stddef.h>

/* Loads the file at path: maps it, or reads it into a block from malloc.
 * *data is NULL for an empty file, and *mapped tells whether sys_unmap or
 * free releases it. Returns 0, 1 if there is no file at path, or -1 with
 * the error in err[errlen]. */
int sys_load(const char *path, char **data, size_t *len, int *mapped,
             char *err, size_t errlen);
void sys_unmap(char *data, size_t len);

/* Writing files, for saving. These return -1 with errno set when they
 * fail, but sys_replace, whose error goes in err[errlen]. */
typedef struct SysChunk {
    const char *p;
    size_t n;
} SysChunk;
#define SYS_CHUNKS 256      /* the most sys_write takes at once */

/* Creates and opens the temporary file tmpl, whose name ends in XXXXXX,
 * with the permissions of the file at like, or those of a new file when
 * like is NULL or not there. Returns its descriptor. */
int sys_open_temp(char *tmpl, const char *like);
/* Opens path to write it from the start, creating it if need be. */
int sys_open_write(const char *path);
/* 0 if path can be written, or is not there. */
int sys_writable(const char *path);
/* Writes chunks c[0..n) to fd, all of them. */
int sys_write(int fd, const SysChunk *c, int n);
/* Puts what was written to fd on the disk. */
int sys_sync(int fd);
int sys_close(int fd);
/* Puts file tmp in place of target, which keeps its permissions. */
int sys_replace(const char *tmp, const char *target, char *err, size_t errlen);

/* On Windows, turns the backslashes in path into slashes. */
void sys_fix_slashes(char *path);
/* The length of the root path starts with: "/", or on Windows also "C:/",
 * "C:" (C:'s working directory) or "//server/share/"; 0 if it is relative. */
size_t sys_root_len(const char *path);
/* The absolute path of path, with symbolic links resolved where the system
 * does that, from malloc; NULL if it can't be had. */
char *sys_full_path(const char *path);
/* The working directory into out[size]; -1 if it can't be had. */
int sys_cwd(char *out, size_t size);
/* The user's home directory, or NULL. */
const char *sys_home(void);
/* The folder of the user's settings folders into out[size]: on Windows
 * %APPDATA%, elsewhere $XDG_CONFIG_HOME or ~/.config. 0 if there is none. */
int sys_config_dir(char *out, size_t size);
/* Creates folder path, if it isn't there. */
void sys_mkdir(const char *path);
/* 1 if path is a folder, 0 if it is something else, -1 if there's nothing
 * there. */
int sys_kind(const char *path);
/* Whether paths a and b lead to the same file. */
int sys_same_file(const char *a, const char *b);
/* The folder for temporary files, and the file that is always empty and
 * takes any writes, for the tests. */
const char *sys_temp_dir(void);
extern const char sys_null_file[];

/* Reading folders: the entries but what the system hides (on Windows,
 * hidden and system files), with "." and "..". */
typedef struct SysDir SysDir;
typedef struct SysEntry {
    const char *name;       /* until the next sys_dir_next */
    int folder, file;       /* a folder, a regular file (else neither) */
    double size;            /* a file's size in bytes */
} SysEntry;

/* NULL if folder dir can't be read. */
SysDir *sys_dir_open(const char *dir);
/* The next entry into e; 0 at the end. */
int sys_dir_next(SysDir *d, SysEntry *e);
void sys_dir_close(SysDir *d);

/* Tells the user msg as cedit starts, an error or not: on the terminal,
 * or on Windows, where a program with a window has none, in a message box. */
void sys_report(const char *msg, int error);

#endif
