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
/* Array p of n elements of size bytes, with room for one more: its
 * capacity is n rounded up to a power of two. */
void *xgrow(void *p, int n, size_t size);

/* Copy / append src into dst[size], truncating and always terminating. */
void str_copy(char *dst, size_t size, const char *src);
void str_cat(char *dst, size_t size, const char *src);

/* The number of elements of array a. */
#define NELEM(a) ((int)(sizeof (a) / sizeof (a)[0]))

int ascii_lower(int c);
/* Do a[0..n) and b[0..n) match, ignoring ASCII case if icase is set? */
int mem_match(const char *a, const char *b, size_t n, int icase);

#endif
