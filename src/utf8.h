/*
 * utf8.h - small UTF-8 helpers. Invalid bytes decode as single bytes with
 * codepoint 0xFFFD, so every byte sequence can be displayed and edited.
 */
#ifndef CEDIT_UTF8_H
#define CEDIT_UTF8_H

#include <stddef.h>

/* Decodes one character at s; returns the number of bytes used (>= 1). */
size_t utf8_decode(const char *s, size_t n, unsigned long *cp);
/* Encodes cp into out (4 bytes max); returns the length. */
size_t utf8_encode(unsigned long cp, char *out);
/* Offset of the character after/before position i in s[0..n). */
size_t utf8_next(const char *s, size_t n, size_t i);
size_t utf8_prev(const char *s, size_t i);

#endif
