/*
 * test_save.c - saving and opening files where the systems go their own
 * ways: a file's attributes kept, a file reached through a symbolic link,
 * and one saved in place in a folder where no file can be added; on
 * Windows also a file another program holds open, and one whose path is
 * too long for the "A" calls.
 */
#ifndef _WIN32
#define _XOPEN_SOURCE 700
#endif
#include "../src/buffer.h"
#include "../src/sys.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <string.h>

#include "testutil.h"

static int failures;

#define CHECK(c, what) do { if (!(c)) { printf("FAIL line %d: %s: %s\n", __LINE__, \
    #c, what); failures++; } } while (0)

/* What each system does its own way: */
#ifdef _WIN32

/* Gives the file an attribute a save must keep: hidden. */
static void set_attributes(const char *path)
{
    SetFileAttributesA(path, FILE_ATTRIBUTE_HIDDEN);
}

static int has_attributes(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_HIDDEN);
}

/* Makes link a symbolic link to target. 0 if done. */
static int make_link(const char *link, const char *target)
{
    char t[512];
    int i;

    strcpy(t, target);              /* a link's target takes backslashes */
    for (i = 0; t[i]; i++)
        if (t[i] == '/')
            t[i] = '\\';
    return CreateSymbolicLinkA(link, t, SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)
        ? 0 : -1;
}

static int is_link(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}

/* Denies everyone adding files to the folder, and allows all else. 0 if
 * done. */
static int deny_adding(const char *dir)
{
    PSECURITY_DESCRIPTOR sd;
    int r;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
            "D:P(D;;0x2;;;WD)(A;OICI;FA;;;WD)", SDDL_REVISION_1, &sd, NULL))
        return -1;
    r = SetFileSecurityA(dir, DACL_SECURITY_INFORMATION, sd) ? 0 : -1;
    LocalFree(sd);
    return r;
}

/* Removes the file at path, and then the folder dir it is in. */
static void remove_dir(const char *dir, const char *path)
{
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    DeleteFileA(path);
    RemoveDirectoryA(dir);
}

#else

/* Gives the file permissions a save must keep: rw-r-----. */
static void set_attributes(const char *path)
{
    chmod(path, 0640);
}

static int has_attributes(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & 07777) == 0640;
}

/* Makes link a symbolic link to target. 0 if done. */
static int make_link(const char *link, const char *target)
{
    return symlink(target, link);
}

static int is_link(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
}

/* Denies adding files to the folder, and allows all else. 0 if done. */
static int deny_adding(const char *dir)
{
    return chmod(dir, 0555);
}

/* Removes the file at path, and then the folder dir it is in. */
static void remove_dir(const char *dir, const char *path)
{
    chmod(dir, 0755);
    remove(path);
    rmdir(dir);
}

#endif

/* Saves text, as one insertion, to path. */
static int save_text(const char *path, const char *text, char *err, size_t errlen)
{
    Buffer *b = buf_new();
    int r;
    buf_insert(b, 0, text, strlen(text));
    r = buf_save(b, path, err, errlen);
    buf_free(b);
    return r;
}

/* Whether the file at path holds text. */
static int holds(const char *path, const char *text)
{
    char got[64];
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(got, 1, sizeof got - 1, f) : 0;
    if (f)
        fclose(f);
    got[n] = 0;
    return f && strcmp(got, text) == 0;
}

/* Whether a file can be added to the folder dir. */
static int can_add(const char *dir)
{
    char probe[512];
    FILE *f;

    sprintf(probe, "%s/probe", dir);
    f = fopen(probe, "wb");
    if (f) {
        fclose(f);
        remove(probe);
    }
    return f != NULL;
}

/* The file that replaces another keeps its attributes. */
static void test_replaced(void)
{
    char path[512], err[256] = "";
    FILE *f = tmp_file(path, sizeof path, "cedit-attrs");
    int r, kept;

    fclose(f);
    set_attributes(path);
    r = save_text(path, "saved\n", err, sizeof err);
    kept = has_attributes(path);
    remove(path);
    CHECK(r == 0, err);
    CHECK(kept, "the file lost its attributes");
}

/* Through a symbolic link: the target is saved, the link stays. */
static void test_link(void)
{
    char path[512], link[512], err[256] = "";
    FILE *f = tmp_file(path, sizeof path, "cedit-target");
    int r, still;

    fclose(f);
    sprintf(link, "%s.link", path);
    if (make_link(link, path) < 0) {
        printf("(no symbolic link test: this account can't make links)\n");
        remove(path);
        return;
    }
    r = save_text(link, "saved\n", err, sizeof err);
    still = is_link(link);
    remove(link);
    CHECK(r == 0, err);
    CHECK(still, "the link was replaced by a file");
    CHECK(holds(path, "saved\n"), "the link's target");
    remove(path);
}

/* Saved in place, in a folder where no file can be added (so no temporary
 * file either), though the file can be written: it keeps its attributes. */
static void test_in_place(void)
{
    char dir[512], path[512], err[256] = "";
    FILE *f;
    int r, ok, kept;

    sprintf(dir, "%s/cedit-noadd", sys_temp_dir());
    sprintf(path, "%s/file.txt", dir);
    remove_dir(dir, path);          /* left by a run that broke off */
    sys_mkdir(dir);
    f = fopen(path, "wb");
    CHECK(f != NULL, "can't make the file");
    if (!f)
        return;
    fclose(f);
    set_attributes(path);
    if (deny_adding(dir) < 0) {
        CHECK(0, "can't deny adding files");
        remove_dir(dir, path);
        return;
    }
    if (can_add(dir)) {             /* as root, say */
        printf("(no in-place test: this account adds files anyway)\n");
        remove_dir(dir, path);
        return;
    }
    r = save_text(path, "saved\n", err, sizeof err);
    kept = has_attributes(path);
    ok = holds(path, "saved\n");
    remove_dir(dir, path);
    CHECK(r == 0, err);
    CHECK(ok, "the file saved in place");
    CHECK(kept, "the file lost its attributes");
}

#ifdef _WIN32

/* Held open by a program that lets others read and write it, but not
 * replace it, as Python's open does. */
static void test_held_open(void)
{
    char path[512], err[256] = "";
    FILE *f = tmp_file(path, sizeof path, "cedit-held");
    HANDLE h;
    int r;

    fclose(f);
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, 0, NULL);
    r = save_text(path, "saved\n", err, sizeof err);
    CloseHandle(h);
    CHECK(r == 0, err);
    CHECK(holds(path, "saved\n"), "the file held open");
    remove(path);
}

/* A file at a path of over MAX_PATH characters is never taken for a new
 * one: it opens where Windows allows long paths, or else fails. */
static void test_long_path(void)
{
    char dir[2048], err[256] = "";
    WCHAR w[2048];
    HANDLE h;
    Buffer *b;
    int i, r, is_new = -1;

    sprintf(dir, "\\\\?\\%s/cedit-long", sys_temp_dir());
    for (i = 4; dir[i]; i++)        /* "\\?\" paths take only backslashes */
        if (dir[i] == '/')
            dir[i] = '\\';
    for (i = 0; i < 8; i++) {
        if (i)
            strcat(dir, "\\a-folder-name-long-enough-to-pass-max-path");
        MultiByteToWideChar(CP_ACP, 0, dir, -1, w, 2048);
        CreateDirectoryW(w, NULL);
    }
    strcat(dir, "\\file.txt");
    MultiByteToWideChar(CP_ACP, 0, dir, -1, w, 2048);
    h = CreateFileW(w, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CHECK(h != INVALID_HANDLE_VALUE, "can't make the file");
    CloseHandle(h);
    b = buf_new();
    r = buf_open(b, dir + 4, &is_new, err, sizeof err);
    buf_free(b);
    DeleteFileW(w);
    for (i = 0; i < 8; i++) {
        *wcsrchr(w, '\\') = 0;
        RemoveDirectoryW(w);
    }
    CHECK(r < 0 || !is_new, "an existing file opened as a new one");
}

#endif

int main(void)
{
    test_replaced();
    test_link();
    test_in_place();
#ifdef _WIN32
    test_held_open();
    test_long_path();
#endif
    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("all save tests passed\n");
    return 0;
}
