/*
 * test_win32.c - saving and opening files where Windows differs: a file
 * another program holds open, one reached through a symbolic link, a
 * hidden one saved in place, and one whose path is too long for the "A"
 * calls. Built on Windows only.
 */
#include "../src/buffer.h"
#include "../src/sys.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>

#include "testutil.h"

static int failures;

#define CHECK(c, what) do { if (!(c)) { printf("FAIL line %d: %s: %s\n", __LINE__, \
    #c, what); failures++; } } while (0)

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

/* Through a symbolic link: the target is saved, the link stays. */
static void test_link(void)
{
    char path[512], target[512], link[512], err[256] = "";
    FILE *f = tmp_file(path, sizeof path, "cedit-target");
    DWORD a;
    int i, r;

    fclose(f);
    sprintf(link, "%s.link", path);
    strcpy(target, path);           /* a link's target takes backslashes */
    for (i = 0; target[i]; i++)
        if (target[i] == '/')
            target[i] = '\\';
    if (!CreateSymbolicLinkA(link, target, SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        printf("(no symbolic link test: this account can't make links)\n");
        remove(path);
        return;
    }
    r = save_text(link, "saved\n", err, sizeof err);
    a = GetFileAttributesA(link);
    DeleteFileA(link);
    CHECK(r == 0, err);
    CHECK(a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT),
          "the link was replaced by a file");
    CHECK(holds(path, "saved\n"), "the link's target");
    remove(path);
}

/* A hidden file saved in place, in a folder where no file can be added
 * (so no temporary file either), though the file can be written. */
static void test_hidden_in_place(void)
{
    char dir[512], path[512], err[256] = "";
    PSECURITY_DESCRIPTOR sd;
    FILE *f;
    DWORD a;
    int r, ok;

    sprintf(dir, "%s/cedit-noadd", sys_temp_dir());
    sprintf(path, "%s/hidden.txt", dir);
    CreateDirectoryA(dir, NULL);
    f = fopen(path, "wb");
    CHECK(f != NULL, "can't make the file");
    if (!f)
        return;
    fclose(f);
    SetFileAttributesA(path, FILE_ATTRIBUTE_HIDDEN);
    /* everyone: denied adding files to the folder, allowed all else */
    r = ConvertStringSecurityDescriptorToSecurityDescriptorA(
            "D:P(D;;0x2;;;WD)(A;OICI;FA;;;WD)", SDDL_REVISION_1, &sd, NULL) &&
        SetFileSecurityA(dir, DACL_SECURITY_INFORMATION, sd);
    if (r)
        LocalFree(sd);
    CHECK(r, "can't deny adding files");
    r = save_text(path, "saved\n", err, sizeof err);
    a = GetFileAttributesA(path);
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    ok = holds(path, "saved\n");
    DeleteFileA(path);
    RemoveDirectoryA(dir);
    CHECK(r == 0, err);
    CHECK(ok, "the hidden file");
    CHECK(a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_HIDDEN),
          "the file is no longer hidden");
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

int main(void)
{
    test_held_open();
    test_link();
    test_hidden_in_place();
    test_long_path();
    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("all Windows tests passed\n");
    return 0;
}
