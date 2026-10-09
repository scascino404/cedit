/*
 * util.c - allocation and string helpers.
 */
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) {
        fputs("cedit: out of memory\n", stderr);
        abort();
    }
    return p;
}

void *xmalloc(size_t n)
{
    return xrealloc(NULL, n);
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    return (char *)memcpy(xmalloc(n), s, n);
}

void str_copy(char *dst, size_t size, const char *src)
{
    size_t n = strlen(src);
    if (!size)
        return;
    if (n >= size)
        n = size - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

void str_cat(char *dst, size_t size, const char *src)
{
    size_t l = strlen(dst);
    if (l < size)
        str_copy(dst + l, size - l, src);
}

int ascii_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

int mem_match(const char *a, const char *b, size_t n, int icase)
{
    size_t i;
    if (!icase)
        return memcmp(a, b, n) == 0;
    for (i = 0; i < n; i++)
        if (ascii_lower((unsigned char)a[i]) != ascii_lower((unsigned char)b[i]))
            return 0;
    return 1;
}

void fix_slashes(char *path)
{
#ifdef _WIN32
    for (; *path; path++)
        if (*path == '\\')
            *path = '/';
#else
    (void)path;
#endif
}
