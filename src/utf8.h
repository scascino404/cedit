/*
 * utf8.h - small UTF-8 helpers. Invalid bytes decode as single bytes with
 * codepoint 0xFFFD, so every byte sequence can be displayed and edited.
 */
#ifndef CEDIT_UTF8_H
#define CEDIT_UTF8_H

#include <stddef.h>

/* Decodes one character at s; returns the number of bytes used (>= 1). */
size_t utf8_decode(const char *s, size_t n, unsigned long *cp);
/* Number of characters (screen cells) in a NUL-terminated string. */
int utf8_width(const char *s);
/* Offset of the character after/before position i in s[0..n). */
size_t utf8_next(const char *s, size_t n, size_t i);
size_t utf8_prev(const char *s, size_t i);

#endif
