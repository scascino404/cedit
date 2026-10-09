/*
 * testutil.h - what the tests need from the system, on POSIX and on
 * Windows: temporary files, and running without the settings file.
 */
#ifndef CEDIT_TESTUTIL_H
#define CEDIT_TESTUTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#define strncasecmp _strnicmp
#define write(fd, buf, n) _write(fd, buf, (unsigned)(n))
/* putenv("NAME=") removes NAME on Windows; NUL is the null device */
#define NO_CONFIG "CEDIT_CONFIG=NUL"
#else
#include <strings.h>
#include <unistd.h>
#define NO_CONFIG "CEDIT_CONFIG="
#endif

/* Creates a new temporary file, named prefix and six more characters, and
 * opens it to read and write, without line end conversion. Its path, with
 * '/' separators, goes into path[size]. Returns the descriptor or -1. */
static int tmp_file(char *path, size_t size, const char *prefix)
{
#ifdef _WIN32
    const char *dir = getenv("TEMP");
    char *p;
    if (!dir)
        dir = ".";
    if (strlen(dir) + strlen(prefix) + 9 > size)
        return -1;
    sprintf(path, "%s/%s-XXXXXX", dir, prefix);
    for (p = path; *p; p++)
        if (*p == '\\')
            *p = '/';
    if (_mktemp_s(path, strlen(path) + 1) != 0)
        return -1;
    return _open(path, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    if (strlen(prefix) + 13 > size)
        return -1;
    sprintf(path, "/tmp/%s-XXXXXX", prefix);
    return mkstemp(path);
#endif
}

#endif
