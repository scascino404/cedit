/*
 * util.h - small helpers shared by all modules: allocation that never
 * returns NULL, bounded string copies and ASCII case-insensitive matching.
 */
#ifndef CEDIT_UTIL_H
#define CEDIT_UTIL_H

#include <stddef.h>

/* Abort with a message when out of memory. */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

/* Copy / append src into dst[size], truncating and always terminating. */
void str_copy(char *dst, size_t size, const char *src);
void str_cat(char *dst, size_t size, const char *src);

int ascii_lower(int c);
/* Do a[0..n) and b[0..n) match, ignoring ASCII case if icase is set? */
int mem_match(const char *a, const char *b, size_t n, int icase);

#endif
