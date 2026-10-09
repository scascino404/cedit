/*
 * test_ui.c - the work that goes on over several frames, driven through the
 * real UI headlessly: searching, Replace All, saving, and moves that wait
 * for a big file to load; and what the user can do meanwhile.
 *
 * The file is big enough (128 MB) that none of it fits in one frame.
 */
#define _XOPEN_SOURCE 700
#include "../src/ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <dirent.h>
#endif

#include "testutil.h"

#define LINES 4000000L      /* of 32 bytes: 128 MB */

static App app;
static int failures;
static char path[512];

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); \
    failures++; } } while (0)

static Editor *ed(void)
{
    return &app.win->ed;
}

static void frame(void)
{
    app_tick(&app);
    app_draw(&app);
}

/* Runs the main loop until the job and the save going on are over;
 * returns the number of frames. */
static int drain(void)
{
    int n = 0;
    while (app.job || app.save) {
        if (app.save_thread && !SDL_AtomicGet(&app.save_ended)) {
            SDL_Delay(1);
            continue;
        }
        frame();
        n++;
    }
    return n;
}

static void key(SDL_Keycode sym, Uint16 mod)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = sym;
    e.key.keysym.mod = mod;
    SDL_SetModState((SDL_Keymod)mod);
    app_event(&app, &e);
    e.type = SDL_KEYUP;
    app_event(&app, &e);
    SDL_SetModState(KMOD_NONE);
}

static void text(const char *s)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_TEXTINPUT;
    strncpy(e.text.text, s, sizeof e.text.text - 1);
    app_event(&app, &e);
}

/* Line ln of the file as written. */
static void line_text(char *out, long ln)
{
    sprintf(out, "line %07ld of the test file %s\n", ln, ln == LINES - 1 ? "NEEDLE" : "......");
}

static void make_file(void)
{
    int fd = tmp_file(path, sizeof path, "cedit-ui");
    FILE *f = fdopen(fd, "wb");
    char l[64];
    long i;
    for (i = 0; i < LINES; i++) {
        line_text(l, i);
        fputs(l, f);
    }
    fclose(f);
}

/* Opens the file, as dropping it on the window does. */
static void open_file(void)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_DROPFILE;
    e.drop.file = SDL_strdup(path);
    app_event(&app, &e);
    frame();
}

static const char *line(long ln, size_t *len)
{
    return ed_line(ed(), ln, len);
}

static int starts(long ln, const char *s)
{
    size_t len;
    const char *l = line(ln, &len);
    return len >= strlen(s) && memcmp(l, s, strlen(s)) == 0;
}

/* Moves that wait for the file to load. */
static void test_moves(void)
{
    char num[32];
    long ln = 3000000;

    open_file();
    CHECK(buf_loading(ed()->doc->buf));
    key(SDLK_END, KMOD_LCTRL);
    CHECK(app.job == J_DOCEND);
    drain();
    CHECK(!buf_loading(ed()->doc->buf));
    CHECK(ed()->cy == LINES && ed()->cx == 0);

    /* moving the cursor meanwhile stops it */
    open_file();
    key(SDLK_END, KMOD_LCTRL);
    key(SDLK_DOWN, 0);
    frame();
    CHECK(app.job == J_NONE && ed()->cy == 1);

    /* Go to Line */
    open_file();
    key(SDLK_g, KMOD_LCTRL);
    sprintf(num, "%ld", ln + 1);
    key(SDLK_BACKSPACE, 0);
    text(num);
    key(SDLK_RETURN, 0);
    CHECK(app.job == J_GOTO || ed()->cy == ln);   /* unless it was quick */
    drain();
    CHECK(ed()->cy == ln && ed()->cx == 0);
    CHECK(starts(ln, "line 3000000 "));
}

#ifdef __APPLE__
/* Text that comes with mod held. */
static void text_with(const char *s, SDL_Keymod mod)
{
    SDL_SetModState(mod);
    text(s);
    SDL_SetModState(KMOD_NONE);
}

/* The number of checked checkboxes in the dialog. */
static int checked(void)
{
    int i, n = 0;
    for (i = 0; i < app.dlg.n; i++)
        n += app.dlg.wd[i].kind == W_CHECK && app.dlg.wd[i].checked;
    return n;
}

/* On macOS, Cmd works as Ctrl, and both Option keys type characters. */
static void test_mac(void)
{
    const Widget *f;
    int n;

    open_file();
    key(SDLK_END, KMOD_LGUI);
    drain();
    CHECK(ed()->cy == LINES && ed()->cx == 0);
    key(SDLK_a, KMOD_RGUI);
    CHECK(ed()->sel);
    /* text that comes with Cmd held is a command, not text */
    text_with("q", KMOD_LGUI);
    CHECK(!ed_modified(ed()));

    /* Option+F types, rather than opening the File menu */
    key(SDLK_HOME, KMOD_LGUI);
    CHECK(!ed()->sel && ed()->cy == 0);
    key(SDLK_f, KMOD_LALT);
    CHECK(app.menu.open < 0);
    text_with("\xc6\x92", KMOD_LALT);      /* f with hook */
    CHECK(starts(0, "\xc6\x92line 0000000"));

    /* and Option+C in the Find field types, rather than toggling Match case */
    key(SDLK_f, KMOD_LGUI);
    CHECK(app.dlg.kind != DLG_NONE);
    f = &app.dlg.wd[app.dlg.focus];
    CHECK(f->kind == W_FIELD);
    n = checked();
    key(SDLK_c, KMOD_LALT);
    text_with("\xc3\xa7", KMOD_LALT);      /* c with cedilla */
    CHECK(checked() == n);
    CHECK(strstr(f->text, "\xc3\xa7") != NULL);
    key(SDLK_ESCAPE, 0);
    CHECK(app.dlg.kind == DLG_NONE);
    key(SDLK_z, KMOD_LGUI);
    CHECK(!ed_modified(ed()));
}
#endif

static void test_find(void)
{
    size_t len, before;

    open_file();
    strcpy(app.opt.find, "needle");
    app.opt.icase = 1;
    key(SDLK_F3, 0);
    frame();
    CHECK(app.job == J_FIND);
    drain();
    CHECK(ed()->sel && ed()->cy == LINES - 1 && ed()->ay == LINES - 1);
    line(LINES - 1, &len);
    CHECK(ed()->ax == len - 6 && ed()->cx == len);
    CHECK(app.msg_until == 0 || strstr(app.msg, "Not found") == NULL);

    /* backward from the top wraps to the end */
    key(SDLK_HOME, KMOD_LCTRL);
    key(SDLK_F3, KMOD_LSHIFT);
    drain();
    CHECK(ed()->sel && ed()->cy == LINES - 1);
    CHECK(strcmp(app.msg, "Search wrapped to the end") == 0);

    /* not found, after searching all of it */
    key(SDLK_HOME, KMOD_LCTRL);
    strcpy(app.opt.find, "no such text");
    key(SDLK_F3, 0);
    frame();
    CHECK(app.job == J_FIND);
    /* the same search again lets it go on */
    before = app.search.done;
    key(SDLK_F3, 0);
    CHECK(app.job == J_FIND && app.search.done >= before && before > 0);
    drain();
    CHECK(strcmp(app.msg, "Not found: no such text") == 0);
    CHECK(ed()->cy == 0 && !ed()->sel);

    /* Esc stops it */
    key(SDLK_F3, 0);
    frame();
    CHECK(app.job == J_FIND);
    key(SDLK_ESCAPE, 0);
    CHECK(app.job == J_NONE && strcmp(app.msg, "Search stopped") == 0);

    /* so does typing, which goes in */
    key(SDLK_F3, 0);
    text("x");
    frame();
    CHECK(app.job == J_NONE && starts(0, "xline 0000000"));
    key(SDLK_z, KMOD_LCTRL);
    CHECK(!ed_modified(ed()));
}

/* Presses the dialog button with hotkey sym: Alt+letter, or on macOS,
 * where Option types characters, the plain letter once Tab has moved the
 * focus out of the text fields. */
static void press(SDL_Keycode sym)
{
#ifdef __APPLE__
    while (app.dlg.wd[app.dlg.focus].kind == W_FIELD)
        key(SDLK_TAB, 0);
    key(sym, 0);
#else
    key(sym, KMOD_LALT);
#endif
}

static void test_replace_all(void)
{
    /* "line 00" starts the first 100000 lines */
    strcpy(app.opt.find, "line 00");
    strcpy(app.opt.repl, "LINE 00");
    app.opt.icase = 0;
    key(SDLK_HOME, KMOD_LCTRL);
    key(SDLK_h, KMOD_LCTRL);
    press(SDLK_a);
    CHECK(app.job == J_REPLACE);
    frame();
    /* Esc stops it, keeping what it replaced as one undo step */
    key(SDLK_ESCAPE, 0);
    CHECK(app.job == J_NONE && app.repl.count > 0 && app.repl.count < 100000);
    CHECK(strstr(app.msg, "Replace All stopped after") != NULL);
    CHECK(starts(0, "LINE 0000000"));
    key(SDLK_ESCAPE, 0);                    /* closes the dialog */
    CHECK(app.dlg.kind == DLG_NONE);
    key(SDLK_z, KMOD_LCTRL);
    CHECK(starts(0, "line 0000000") && !ed_modified(ed()));

    /* the document can't change meanwhile (the dialog closed as its
     * Close button does) */
    key(SDLK_h, KMOD_LCTRL);
    press(SDLK_a);
    dlg_close(&app.dlg);
    text("Y");
    CHECK(strcmp(app.msg, "Replace All is running (Esc stops it)") == 0);
    drain();
    CHECK(app.repl.count == 100000);
    CHECK(strcmp(app.msg, "Replaced 100000 occurrences") == 0);
    CHECK(starts(0, "LINE 0000000") && starts(99999, "LINE 0099999") &&
          starts(100000, "line 0100000"));
    key(SDLK_z, KMOD_LCTRL);
    CHECK(starts(0, "line 0000000") && starts(99999, "line 0099999"));
    CHECK(!ed_modified(ed()));
}

/* The saved file is the text with "X" in front. */
static void check_saved(void)
{
    FILE *f = fopen(path, "rb");
    char l[64], want[64];
    long i = 0;
    int ok = f != NULL;
    while (ok && fgets(l, sizeof l, f)) {
        line_text(want, i);
        ok = strcmp(i ? l : l + 1, want) == 0 && (i || l[0] == 'X');
        i++;
    }
    CHECK(ok && i == LINES);
    if (f)
        fclose(f);
}

/* Whether a save left a temporary file next to the file. */
static int temp_left(void)
{
    const char *base = strrchr(path, '/') + 1;
    int dlen = (int)(base - 1 - path), found = 0;
#ifdef _WIN32
    char pattern[512];
    struct _finddata_t f;
    intptr_t h;
    sprintf(pattern, "%.*s/.%s.cedit-*", dlen, path, base);
    h = _findfirst(pattern, &f);
    found = h != -1;
    if (found)
        _findclose(h);
#else
    char dir[512], name[64];
    DIR *d;
    struct dirent *e;
    sprintf(dir, "%.*s", dlen, path);
    sprintf(name, ".%s.cedit-", base);
    d = opendir(dir);
    while (d && (e = readdir(d)) != NULL)
        found |= strncmp(e->d_name, name, strlen(name)) == 0;
    if (d)
        closedir(d);
#endif
    return found;
}

static void test_save(void)
{
    unsigned long changes;

    key(SDLK_HOME, KMOD_LCTRL);
    text("X");
    key(SDLK_s, KMOD_LCTRL);
    CHECK(app.save != NULL);
    /* the document can't change meanwhile, nor be closed */
    changes = ed()->doc->buf->changes;
    text("Y");
    key(SDLK_RETURN, 0);
    key(SDLK_z, KMOD_LCTRL);
    CHECK(ed()->doc->buf->changes == changes);
    CHECK(strstr(app.msg, "no changes until the file is saved") != NULL);
    key(SDLK_n, KMOD_LCTRL);
    CHECK(strcmp(app.msg, "Wait until the file is saved") == 0);
    CHECK(ed()->doc->path && strcmp(ed()->doc->path, path) == 0);
    /* but the cursor moves, and searches work */
    key(SDLK_DOWN, 0);
    CHECK(ed()->cy == 1);
    strcpy(app.opt.find, "line 0000005");
    key(SDLK_F3, 0);
    CHECK(ed()->cy == 5 && ed()->sel);
    drain();
    CHECK(app.save == NULL && !ed_modified(ed()));
    CHECK(strncmp(app.msg, "Saved ", 6) == 0);
    check_saved();
    CHECK(!temp_left());

    /* exiting waits for the save, then exits */
    key(SDLK_HOME, KMOD_LCTRL);
    key(SDLK_DELETE, 0);
    text("X");
    key(SDLK_s, KMOD_LCTRL);
    key(SDLK_q, KMOD_LCTRL);
    CHECK(app.running && app.save_quit);
    drain();
    CHECK(!app.running && !ed_modified(ed()));
    check_saved();
    CHECK(!temp_left());
}

int main(void)
{
    char *argv[2];

    putenv(NO_CONFIG);
    /* not offscreen: on macOS SDL gives its windows OpenGL there, which
     * it then can't load (no EGL), and dummy draws the same */
    if (!getenv("SDL_VIDEODRIVER"))
        putenv("SDL_VIDEODRIVER=dummy");
    make_file();
    argv[0] = "test_ui";
    argv[1] = NULL;
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) < 0 || app_init(&app, 1, argv) < 0) {
        printf("test_ui: %s\n", SDL_GetError());
        unlink(path);
        return 1;
    }
    app.blink_next = (unsigned long)-1;

    test_moves();
#ifdef __APPLE__
    test_mac();
#endif
    test_find();
    test_replace_all();
    test_save();

    app_quit(&app);
    SDL_Quit();
    unlink(path);
    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("all ui tests passed\n");
    return 0;
}
