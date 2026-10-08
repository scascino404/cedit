/*
 * utf8.c - UTF-8 decoding, cursor stepping and width.
 */
#include "utf8.h"

#include <string.h>

size_t utf8_decode(const char *str, size_t n, unsigned long *cp)
{
    const unsigned char *s = (const unsigned char *)str;
    unsigned long c;
    size_t len, i;

    if (!n) {
        *cp = 0;
        return 0;
    }
    if (s[0] < 0x80) {
        *cp = s[0];
        return 1;
    }
    if (s[0] >= 0xC2 && s[0] <= 0xDF) {
        len = 2;
        c = s[0] & 0x1F;
    } else if (s[0] >= 0xE0 && s[0] <= 0xEF) {
        len = 3;
        c = s[0] & 0x0F;
    } else if (s[0] >= 0xF0 && s[0] <= 0xF4) {
        len = 4;
        c = s[0] & 0x07;
    } else {
        *cp = 0xFFFD;
        return 1;
    }
    if (len > n) {
        *cp = 0xFFFD;
        return 1;
    }
    for (i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *cp = 0xFFFD;
            return 1;
        }
        c = (c << 6) | (s[i] & 0x3F);
    }
    /* reject overlong forms, surrogates and values past U+10FFFF */
    if ((len == 3 && c < 0x800) || (len == 4 && (c < 0x10000 || c > 0x10FFFF)) ||
        (c >= 0xD800 && c <= 0xDFFF)) {
        *cp = 0xFFFD;
        return 1;
    }
    *cp = c;
    return len;
}

int utf8_width(const char *s)
{
    size_t n = strlen(s), i = 0;
    int w = 0;
    while (i < n) {
        i = utf8_next(s, n, i);
        w++;
    }
    return w;
}

size_t utf8_next(const char *s, size_t n, size_t i)
{
    unsigned long cp;
    if (i >= n)
        return n;
    return i + utf8_decode(s + i, n - i, &cp);
}

size_t utf8_prev(const char *s, size_t i)
{
    size_t j, k;
    unsigned long cp;

    if (i == 0)
        return 0;
    /* step back over up to 3 continuation bytes, then check that the
     * sequence really decodes to exactly this span */
    for (k = 1; k <= 4 && k <= i; k++) {
        j = i - k;
        if (((unsigned char)s[j] & 0xC0) != 0x80) {
            if (utf8_decode(s + j, k, &cp) == k)
                return j;
            break;
        }
    }
    return i - 1;
}
