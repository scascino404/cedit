/*
 * testutil.h - temporary files for the tests, on every system.
 */
#ifndef CEDIT_TESTUTIL_H
#define CEDIT_TESTUTIL_H

#include "../src/sys.h"

#include <stdio.h>
#include <string.h>

/* Creates a new temporary file, named prefix and six more characters, and
 * opens it to write, without line end conversion. Its path goes into
 * path[size]. NULL if it can't be made. */
static FILE *tmp_file(char *path, size_t size, const char *prefix)
{
    const char *dir = sys_temp_dir();
    int fd;

    if (strlen(dir) + strlen(prefix) + 9 > size)
        return NULL;
    sprintf(path, "%s/%s-XXXXXX", dir, prefix);
    fd = sys_open_temp(path, NULL);
    if (fd < 0 || sys_close(fd) < 0)
        return NULL;
    return fopen(path, "wb");
}

#endif
