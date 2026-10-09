/*
 * path.h - file paths and directory listings, for the Open and Save As
 * dialogs and the file menu. Knows nothing about SDL.
 */
#ifndef CEDIT_PATH_H
#define CEDIT_PATH_H

#include <stddef.h>

/*
 * Paths use '/'. On Windows, paths from the system and from the user have
 * their backslashes turned into slashes, and roots are "C:/" and
 * "//server/share/".
 */

/* dir, a '/' (unless dir ends in one, as the root does) and name, into
 * out[size]. */
void path_join(char *out, size_t size, const char *dir, const char *name);
/* What the user typed as a file name in directory dir: an absolute path,
 * one under the home directory ("~/..."), or else one relative to dir. */
void path_resolve(char *out, size_t size, const char *dir, const char *name);
/* The directory of file path, or the working directory for NULL. */
void path_dir(char *out, size_t size, const char *path);
/* The absolute path of path, with symbolic links resolved where the system
 * does that, or path itself if it can't be had. */
void path_full(char *out, size_t size, const char *path);
/* 1 if path is a folder, 0 if it is something else, -1 if there's nothing
 * there. */
int path_kind(const char *path);

typedef struct DirEntry {
    char *name;             /* folders end in '/' */
    int folder, file;       /* a folder, a regular file (else neither) */
    double size;            /* a file's size in bytes */
    int current;            /* it is the file dir_list was asked to mark */
} DirEntry;

/* The entries of directory dir, without "." and dotfiles (and on Windows,
 * hidden files), but with ".." if parent is set and dir is not the root.
 * The entry that is the file at path mark, if not NULL, is marked current.
 * Sorted: "../" first, then the folders, then the files, by name ignoring
 * case. Returns how many, or -1 if dir can't be read. */
int dir_list(const char *dir, int parent, const char *mark, DirEntry **out);
void dir_free(DirEntry *ents, int n);

/* A file size as ls -h shows it, into out[16]: "980", "4.2K", "17M". */
void format_size(char *out, double n);

#endif
