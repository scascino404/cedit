/*
 * path.h - file paths and directory listings, for the Open and Save As
 * dialogs and the file menu. Knows nothing about SDL.
 */
#ifndef CEDIT_PATH_H
#define CEDIT_PATH_H

#include <stddef.h>
#include <sys/stat.h>

/* dir, a '/' (unless dir is the root) and name, into out[size]. */
void path_join(char *out, size_t size, const char *dir, const char *name);
/* What the user typed as a file name in directory dir: an absolute path,
 * one under the home directory ("~/..."), or else one relative to dir. */
void path_resolve(char *out, size_t size, const char *dir, const char *name);
/* The directory of file path, or the working directory for NULL. */
void path_dir(char *out, size_t size, const char *path);

typedef struct DirEntry {
    char *name;             /* folders end in '/' */
    struct stat st;         /* all 0 when stat failed */
} DirEntry;

/* The entries of directory dir, without "." and dotfiles, but with ".."
 * if parent is set and dir is not the root. Sorted: "../" first, then the
 * folders, then the files, by name ignoring case. Returns how many, or -1
 * if dir can't be read. */
int dir_list(const char *dir, int parent, DirEntry **out);
void dir_free(DirEntry *ents, int n);

/* A file size as ls -h shows it, into out[16]: "980", "4.2K", "17M". */
void format_size(char *out, double n);

#endif
