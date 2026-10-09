/*
 * bench.c - performance benchmarks: drives the real UI headlessly through
 * user-like sessions on large generated files, and reports times and memory.
 *
 *   bench [-d dir] [-r runs] [-o out.tsv] [-b base.tsv] [scenario...]
 *
 * Each scenario runs in its own process (so memory is measured per
 * scenario), runs times, and reports the median of each metric. -o saves
 * the results, -b compares with saved ones. A scenario argument runs only
 * the scenarios whose name starts with it ("log" runs log.open, log.find
 * and log.edit).
 *
 * The files are generated into dir (build/bench-data) on first use: C and
 * Markdown from the sources in dir/seed (the Makefile extracts them from a
 * pinned commit, so they don't change as the code does), and synthetic logs
 * and JSON. They take about 1.3 GB.
 *
 * Frames are timed from the input event to the end of drawing, as the main
 * loop does them (event, app_tick, app_draw). The SDL calls that scale and
 * show the frame are replaced by no-ops (the Makefile links with --wrap):
 * on a real display the GPU does that work, and the offscreen software
 * renderer would only add noise. Rasterizing the changed cells and the
 * texture upload are kept; "raster" metrics time screen_present alone.
 *
 * Memory: anon is the process's anonymous resident memory (heap) minus what
 * it was right after startup with an empty document; peak is the peak
 * resident size, including the mapped file's pages.
 */
#define _XOPEN_SOURCE 700
#include "../src/ui.h"
#include "../src/syntax.h"
#include "../src/util.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MB (1024.0 * 1024.0)

static const char *data = "build/bench-data";
static App app;
static double base_anon;

/* ------------------------------------------------------------------ */
/* measuring                                                           */
/* ------------------------------------------------------------------ */

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

/* A field of /proc/self/status, in MB (0 where there is none). */
static double status_mb(const char *key)
{
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    size_t n = strlen(key);
    double kb = 0;
    if (!f)
        return 0;
    while (fgets(line, sizeof line, f))
        if (strncmp(line, key, n) == 0 && line[n] == ':') {
            kb = atof(line + n + 1);
            break;
        }
    fclose(f);
    return kb / 1024;
}

static void report(const char *metric, double v, const char *unit)
{
    printf("%s\t%.3f\t%s\n", metric, v, unit);
}

static void report_anon(const char *metric)
{
    report(metric, status_mb("RssAnon") - base_anon, "MB");
}

/* Samples, for latency percentiles. */
typedef struct Samples {
    double *v;
    int n, cap;
} Samples;

static void add(Samples *s, double v)
{
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 1024;
        s->v = (double *)realloc(s->v, (size_t)s->cap * sizeof *s->v);
    }
    s->v[s->n++] = v;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static double pct(Samples *s, double p)
{
    int i;
    if (!s->n)
        return 0;
    qsort(s->v, (size_t)s->n, sizeof *s->v, cmp_double);
    i = (int)(p / 100 * (s->n - 1) + 0.5);
    return s->v[i];
}

/* Reports p50, p99 and max of s as metric_p50 ..., and empties it. */
static void report_lat(const char *metric, Samples *s)
{
    char name[128];
    sprintf(name, "%s_p50", metric);
    report(name, pct(s, 50), "ms");
    sprintf(name, "%s_p99", metric);
    report(name, pct(s, 99), "ms");
    sprintf(name, "%s_max", metric);
    report(name, pct(s, 100), "ms");
    s->n = 0;
}

/* ------------------------------------------------------------------ */
/* the frame, without the GPU's part (see --wrap in the Makefile)      */
/* ------------------------------------------------------------------ */

static double raster_ms;        /* screen_present time of the last frame */

void __real_screen_present(Screen *s);
void __wrap_screen_present(Screen *s)
{
    double t = now();
    __real_screen_present(s);
    raster_ms = now() - t;
}

int __wrap_SDL_RenderClear(SDL_Renderer *r)
{
    (void)r;
    return 0;
}

int __wrap_SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src,
                          const SDL_Rect *dst)
{
    (void)r;
    (void)t;
    (void)src;
    (void)dst;
    return 0;
}

int __wrap_SDL_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect)
{
    (void)r;
    (void)rect;
    return 0;
}

void __wrap_SDL_RenderPresent(SDL_Renderer *r)
{
    (void)r;
}

/* ------------------------------------------------------------------ */
/* driving the app                                                     */
/* ------------------------------------------------------------------ */

/* What the main loop does after handling input. */
static void frame(void)
{
    app_tick(&app);
    app_draw(&app);
}

/* Runs the main loop while it has background work (loading, lexing).
 * The cursor blink is stopped so that it doesn't count as work. */
static void settle(void)
{
    app.blink_next = (unsigned long)-1;
    while (app_timeout(&app) == 0)
        frame();
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

/* Types one character as the keyboard sends it. */
static void type_char(char c)
{
    char s[2];
    if (c == '\n') {
        key(SDLK_RETURN, 0);
    } else if (c == '\t') {
        key(SDLK_TAB, 0);
    } else {
        s[0] = c;
        s[1] = 0;
        text(s);
    }
}

static Editor *ed(void)
{
    return &app.win->ed;
}

static void path_of(char *out, const char *name)
{
    sprintf(out, "%s/%s", data, name);
}

/* Opens a file of the data directory as dropping it on the window does,
 * and draws the first frame; returns the time that took. */
static double open_file(const char *name)
{
    SDL_Event e;
    char path[4096];
    double t;
    path_of(path, name);
    memset(&e, 0, sizeof e);
    e.type = SDL_DROPFILE;
    e.drop.file = SDL_strdup(path);
    t = now();
    app_event(&app, &e);
    frame();
    t = now() - t;
    if (!ed()->doc->path || strcmp(ed()->doc->path, path) != 0) {
        fprintf(stderr, "bench: %s did not open\n", path);
        exit(1);
    }
    return t;
}

/* Closes the document (File > New, not saving), and returns the time. */
static double close_file(void)
{
    double t = now();
    key(SDLK_n, KMOD_LCTRL);
    if (app.dlg.kind != DLG_NONE)   /* "Save changes?": No */
        key(SDLK_n, 0);
    frame();
    return now() - t;
}

/* Starts the app in a 1920x1080 window, Medium text, word wrap and
 * highlighting on, and notes the memory it uses with no file. */
static double start(void)
{
    double t = now();
    SDL_Event e;
    char *argv[2];

    argv[0] = "bench";
    argv[1] = NULL;
    if (SDL_Init(SDL_INIT_VIDEO) < 0 || app_init(&app, 1, argv) < 0) {
        fprintf(stderr, "bench: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_SetWindowSize(app.scr.win, 1920, 1080);
    memset(&e, 0, sizeof e);
    e.type = SDL_WINDOWEVENT;
    e.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
    e.window.data1 = 1920;
    e.window.data2 = 1080;
    app_event(&app, &e);
    app.opt.wrap = 1;
    app.highlight = 1;
    screen_pointer(&app.scr, 0, 0, 0);
    frame();
    t = now() - t;
    if (getenv("BENCH_DEBUG"))
        fprintf(stderr, "grid %dx%d, text area %dx%d\n", app.scr.cols, app.scr.rows,
                app.win->ed.view_w, app.win->ed.view_h);
    base_anon = status_mb("RssAnon");
    return t;
}

static void stop(void)
{
    struct rusage ru;
    app_quit(&app);
    SDL_Quit();
    getrusage(RUSAGE_SELF, &ru);
    report("peak_rss", ru.ru_maxrss / 1024.0, "MB");
}

/* A random number generator that is the same everywhere (xorshift32).
 * Each user seeds it, so that what it makes doesn't depend on what ran
 * before. */
static unsigned long rng;

static unsigned long rnd(unsigned long n)
{
    rng ^= (rng << 13) & 0xFFFFFFFFUL;
    rng ^= rng >> 17;
    rng ^= (rng << 5) & 0xFFFFFFFFUL;
    return rng % n;
}

/* Code typed in the typing scenarios, as written by hand. */
static const char typed_code[] =
    "static int count_words(const char *s, size_t len)\n"
    "{\n"
    "    size_t i;\n"
    "    int n = 0, in_word = 0;\n"
    "\n"
    "    /* a word is a run of letters, digits or '_' */\n"
    "    for (i = 0; i < len; i++) {\n"
    "        int w = isalnum((unsigned char)s[i]) || s[i] == '_';\n"
    "        if (w && !in_word)\n"
    "            n++;\n"
    "        in_word = w;\n"
    "    }\n"
    "    printf(\"%d words\\n\", n);\n"
    "    return n;\n"
    "}\n\n";

/* ------------------------------------------------------------------ */
/* scenarios                                                           */
/* ------------------------------------------------------------------ */

/* Starting the app with no file. */
static void sc_startup(void)
{
    report("startup", start(), "ms");
    report("base_anon", base_anon, "MB");
    stop();
}

/* Opening and closing an everyday source file (editor.c, 30 KB). */
static void sc_small(void)
{
    Samples o = {0}, c = {0};
    int i;
    start();
    for (i = 0; i < 100; i++) {
        add(&o, open_file("small.c"));
        add(&c, close_file());
    }
    report("open", pct(&o, 50), "ms");
    report("close", pct(&c, 50), "ms");
    report_anon("anon_after");
    stop();
}

/* A 1 GB log: the first screen, indexing in the background, jumping to
 * the end, closing. */
static void sc_log_open(void)
{
    double t;
    start();
    report("first_frame", open_file("huge.log"), "ms");
    t = now();
    settle();
    report("index", now() - t, "ms");
    t = now();
    key(SDLK_END, KMOD_LCTRL);
    frame();
    report("ctrl_end", now() - t, "ms");
    report_anon("anon");
    report("file_rss", status_mb("RssFile"), "MB");
    report("close", close_file(), "ms");
    report_anon("anon_after_close");
    stop();
}

/* Searching the 1 GB log for text it doesn't contain (both passes of a
 * wrapped search), matching case and not. */
static void sc_log_find(void)
{
    double t;
    start();
    open_file("huge.log");
    settle();
    strcpy(app.opt.find, "Timeout: upstream");
    app.opt.icase = 0;
    t = now();
    key(SDLK_F3, 0);
    frame();
    report("find_miss", now() - t, "ms");
    app.opt.icase = 1;
    t = now();
    key(SDLK_F3, 0);
    frame();
    report("find_miss_icase", now() - t, "ms");
    close_file();
    stop();
}

/* Edits scattered over the 1 GB log (each a jump and a typed character),
 * then saving it and closing it. */
static void sc_log_edit(void)
{
    Samples s = {0};
    char out[4096], err[256];
    double t;
    long n;
    int i;

    start();
    open_file("huge.log");
    settle();
    n = ed_lines(ed());
    rng = 3;
    for (i = 0; i < 5000; i++) {
        t = now();
        ed_set_cursor(ed(), (long)rnd((unsigned long)n), 0, 0);
        text("#");
        frame();
        add(&s, now() - t);
    }
    report_lat("edit", &s);
    report_anon("anon");
    path_of(out, "out.tmp");
    t = now();
    if (ed_save(ed(), out, err, sizeof err) < 0) {
        fprintf(stderr, "bench: save: %s\n", err);
        exit(1);
    }
    frame();
    report("save", now() - t, "ms");
    report("close", close_file(), "ms");
    report_anon("anon_after_close");
    unlink(out);
    stop();
}

/* A 256 MB C file: the first screen, jumping to the end before the
 * highlighter got there, and the time until it has. */
static void sc_bigc_open(void)
{
    double t;
    start();
    report("first_frame", open_file("big.c"), "ms");
    t = now();
    key(SDLK_END, KMOD_LCTRL);
    frame();
    report("ctrl_end", now() - t, "ms");
    t = now();
    settle();
    report("hl_catchup", now() - t, "ms");
    report_anon("anon");
    report("close", close_file(), "ms");
    stop();
}

/* Typing at the top of the 256 MB C file while a second window shows its
 * end: every keystroke invalidates the highlighting the other one needs. */
static void sc_bigc_split(void)
{
    Samples s = {0};
    double t;
    int i;

    start();
    open_file("big.c");
    key(SDLK_BACKSLASH, KMOD_LCTRL);
    key(SDLK_END, KMOD_LCTRL);
    frame();
    settle();
    key(SDLK_F6, 0);
    frame();
    for (i = 0; i < 200; i++) {
        t = now();
        type_char(typed_code[i % (sizeof typed_code - 1)]);
        frame();
        add(&s, now() - t);
    }
    report_lat("key", &s);
    t = now();
    settle();
    report("hl_catchup", now() - t, "ms");
    stop();
}

/* Paging through an 8 MB C file, with and without word wrap, and moving
 * down a line at a time (every row scrolls). */
static void sc_code_scroll(void)
{
    Samples s = {0}, r = {0};
    double t;
    int i, wrap;

    start();
    open_file("code.c");
    settle();
    for (wrap = 1; wrap >= 0; wrap--) {
        app.opt.wrap = wrap;
        key(SDLK_HOME, KMOD_LCTRL);
        frame();
        for (i = 0; i < 2000 && ed()->cy < ed_lines(ed()) - 1; i++) {
            t = now();
            key(SDLK_PAGEDOWN, 0);
            frame();
            add(&s, now() - t);
            add(&r, raster_ms);
        }
        report_lat(wrap ? "pgdn_wrap" : "pgdn_nowrap", &s);
    }
    report("raster_p50", pct(&r, 50), "ms");
    app.opt.wrap = 1;
    key(SDLK_HOME, KMOD_LCTRL);
    frame();
    for (i = 0; i < 3000; i++) {
        key(SDLK_DOWN, 0);
        if (i < 100)
            frame();        /* the cursor reaches the bottom first */
        else {
            t = now();
            frame();
            add(&s, now() - t);
        }
    }
    report_lat("down", &s);
    stop();
}

/* Typing code into the middle of the 8 MB C file, then undoing and
 * redoing all of it. */
static void sc_code_type(void)
{
    Samples s = {0};
    double t;
    int i, n = 0;

    start();
    open_file("code.c");
    settle();
    ed_goto(ed(), ed_lines(ed()) / 2);
    frame();
    for (i = 0; i < 3000; i++) {
        t = now();
        type_char(typed_code[i % (sizeof typed_code - 1)]);
        frame();
        add(&s, now() - t);
    }
    report_lat("key", &s);
    report_anon("anon");
    t = now();
    while (ed_modified(ed())) {
        key(SDLK_z, KMOD_LCTRL);
        n++;
    }
    frame();
    report("undo_all", now() - t, "ms");
    report("undo_steps", n, "steps");
    t = now();
    while (n--)
        key(SDLK_y, KMOD_LCTRL);
    frame();
    report("redo_all", now() - t, "ms");
    stop();
}

/* Replace All of a common identifier in the 8 MB C file, undoing it, and
 * closing the file with the history. */
static void sc_code_replace(void)
{
    double t;
    long n;

    start();
    open_file("code.c");
    settle();
    strcpy(app.opt.find, "len");
    strcpy(app.opt.repl, "length");
    app.opt.icase = 0;
    t = now();
    n = ed_replace_all(ed());
    frame();
    report("replace_all", now() - t, "ms");
    report("matches", (double)n, "count");
    report_anon("anon");
    t = now();
    key(SDLK_z, KMOD_LCTRL);
    frame();
    report("undo", now() - t, "ms");
    report("close", close_file(), "ms");
    report_anon("anon_after_close");
    stop();
}

/* Copying all of the 8 MB C file and pasting it at the end. */
static void sc_code_paste(void)
{
    double t;
    char *clip;
    size_t n;

    start();
    open_file("code.c");
    settle();
    t = now();
    key(SDLK_a, KMOD_LCTRL);
    clip = ed_copy(ed(), &n);   /* the system clipboard, as cedit uses it */
    key(SDLK_END, KMOD_LCTRL);
    frame();
    report("copy", now() - t, "ms");
    t = now();
    ed_paste(ed(), clip, n);
    frame();
    report("paste", now() - t, "ms");
    free(clip);
    report_anon("anon");
    t = now();
    key(SDLK_z, KMOD_LCTRL);
    frame();
    report("undo", now() - t, "ms");
    stop();
}

/* A 4 MB minified JSON file, all on one line (a minified script or
 * bundle is like it): paging through it and typing at its end, with word
 * wrap and without. */
static void sc_json_long(void)
{
    Samples s = {0};
    double t;
    int i, wrap;

    start();
    report("first_frame", open_file("mini.json"), "ms");
    settle();
    for (i = 0; i < 5; i++) {
        t = now();
        key(SDLK_PAGEDOWN, 0);
        frame();
        add(&s, now() - t);
    }
    report_lat("pgdn_wrap", &s);
    for (wrap = 1; wrap >= 0; wrap--) {
        app.opt.wrap = wrap;
        key(SDLK_HOME, KMOD_LCTRL);
        frame();
        t = now();
        key(SDLK_END, KMOD_LCTRL);
        frame();
        report(wrap ? "ctrl_end_wrap" : "ctrl_end_nowrap", now() - t, "ms");
        for (i = 0; i < 10; i++) {
            t = now();
            type_char('x');
            frame();
            add(&s, now() - t);
        }
        report_lat(wrap ? "key_at_end_wrap" : "key_at_end_nowrap", &s);
    }
    stop();
}

/* Lexer throughput on whole files: the background pass (states only) and
 * the screen pass (a class for every byte). */
static void lex_file(const char *name, const char *label)
{
    char path[4096], err[256], metric[64];
    Buffer *b = buf_new();
    Highlight h;
    int is_new;
    long ln, n;
    size_t len;
    double t, mb;

    path_of(path, name);
    if (buf_open(b, path, &is_new, err, sizeof err) < 0) {
        fprintf(stderr, "bench: %s: %s\n", path, err);
        exit(1);
    }
    buf_load_all(b);
    mb = buf_size(b) / MB;
    hl_init(&h);
    hl_set(&h, b, syn_detect(name, buf_line(b, 0, &len), len));
    t = now();
    while (hl_behind(&h, buf_lines(b) - 1))
        hl_fill(&h, buf_lines(b) - 1, (size_t)1 << 30);
    /* hl_fill leaves the last few MB to hl_line */
    n = buf_lines(b);
    hl_line(&h, n - 1);
    sprintf(metric, "%s_states", label);
    report(metric, mb / ((now() - t) / 1e3), "MB/s");
    t = now();
    for (ln = 0; ln < n; ln++)
        hl_line(&h, ln);
    sprintf(metric, "%s_classes", label);
    report(metric, mb / ((now() - t) / 1e3), "MB/s");
    hl_free(&h);
    buf_free(b);
}

static void sc_lex(void)
{
    lex_file("code.c", "c");
    lex_file("docs.md", "markdown");
    lex_file("pretty.json", "json");
}

typedef struct Scenario {
    const char *name;
    void (*run)(void);
} Scenario;

static const Scenario scenarios[] = {
    {"startup", sc_startup},
    {"small", sc_small},
    {"log.open", sc_log_open},
    {"log.find", sc_log_find},
    {"log.edit", sc_log_edit},
    {"bigc.open", sc_bigc_open},
    {"bigc.split", sc_bigc_split},
    {"code.scroll", sc_code_scroll},
    {"code.type", sc_code_type},
    {"code.replace", sc_code_replace},
    {"code.paste", sc_code_paste},
    {"json.long", sc_json_long},
    {"lex", sc_lex},
};
#define NSCEN ((int)(sizeof scenarios / sizeof scenarios[0]))

/* ------------------------------------------------------------------ */
/* the files                                                           */
/* ------------------------------------------------------------------ */

static FILE *create(const char *name)
{
    char path[4096];
    FILE *f;
    path_of(path, name);
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "bench: %s: %s\n", path, strerror(errno));
        exit(1);
    }
    return f;
}

static int exists(const char *name)
{
    char path[4096];
    struct stat st;
    path_of(path, name);
    return stat(path, &st) == 0;
}

static char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    char *d;
    long len;
    if (!f) {
        fprintf(stderr, "bench: %s: %s (run make bench)\n", path, strerror(errno));
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = (char *)malloc((size_t)len + 1);
    *n = fread(d, 1, (size_t)len, f);
    fclose(f);
    return d;
}

/* Writes text repeated to about size bytes, in whole copies. */
static void repeat(const char *name, const char *text, size_t n, size_t size)
{
    FILE *f = create(name);
    size_t done;
    for (done = 0; done < size; done += n)
        fwrite(text, 1, n, f);
    fclose(f);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The seed's C sources (not the font tables, which are mostly ASCII art in
 * strings), in name order, concatenated. */
static char *seed_code(size_t *n)
{
    char dir[4096], path[4096], *names[64], *all = NULL;
    DIR *d;
    struct dirent *e;
    int k = 0, i;

    sprintf(dir, "%s/seed/src", data);
    d = opendir(dir);
    if (!d) {
        fprintf(stderr, "bench: %s: %s (run make bench)\n", dir, strerror(errno));
        exit(1);
    }
    while ((e = readdir(d)) && k < 64) {
        size_t l = strlen(e->d_name);
        if (l > 2 && strcmp(e->d_name + l - 2, ".c") == 0 &&
            strncmp(e->d_name, "font", 4) != 0)
            names[k++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)k, sizeof *names, cmp_str);
    *n = 0;
    for (i = 0; i < k; i++) {
        size_t m;
        char *t;
        sprintf(path, "%s/%s", dir, names[i]);
        t = slurp(path, &m);
        all = (char *)realloc(all, *n + m);
        memcpy(all + *n, t, m);
        *n += m;
        free(t);
        free(names[i]);
    }
    return all;
}

static void gen_log(void)
{
    static const char *const level[] = {"INFO ", "INFO ", "INFO ", "DEBUG", "WARN ", "ERROR"};
    static const char *const svc[] = {"api", "worker", "auth", "billing", "search"};
    static const char *const path[] = {"items", "users", "orders", "sessions", "reports"};
    static const char *const meth[] = {"GET", "GET", "POST", "PUT", "DELETE"};
    FILE *f = create("huge.log");
    size_t done = 0;
    unsigned long sec = 0;
    char line[512];

    rng = 2463534242UL;
    while (done < (size_t)1 << 30) {
        int n;
        sec += rnd(3);
        n = sprintf(line,
                    "2026-10-%02lu %02lu:%02lu:%02lu.%03lu %s [%s-%lu] %s /api/v%lu/%s/%lu "
                    "status=%lu dur=%lu.%lums req=%08lx%08lx\n",
                    1 + sec / 86400 % 28, sec / 3600 % 24, sec / 60 % 60, sec % 60,
                    rnd(1000), level[rnd(6)], svc[rnd(5)], rnd(16), meth[rnd(5)],
                    1 + rnd(2), path[rnd(5)], rnd(100000),
                    rnd(10) ? 200 : 400 + rnd(104), rnd(500), rnd(10),
                    rnd(0xFFFFFFFUL), rnd(0xFFFFFFFUL));
        fwrite(line, 1, (size_t)n, f);
        done += (size_t)n;
    }
    fclose(f);
}

/* JSON records, all on one line, or pretty-printed with two-space
 * indents. */
static void gen_json(const char *name, size_t size, int pretty, unsigned long seed)
{
    static const char *const words[] = {"alpha", "bravo", "charlie", "delta",
                                        "echo", "foxtrot", "golf", "hotel"};
    FILE *f = create(name);
    const char *nl = pretty ? "\n" : "", *i1 = pretty ? "  " : "",
               *i2 = pretty ? "    " : "", *i3 = pretty ? "      " : "",
               *sp = pretty ? " " : "";
    size_t done = 0;
    unsigned long id = 0;

    rng = seed;
    fputc('[', f);
    while (done < size) {
        long start = ftell(f);
        fprintf(f, "%s%s{%s", id ? "," : "", nl, nl);
        fprintf(f, "%s%s\"id\":%s%lu,%s", i1, i2, sp, id, nl);
        fprintf(f, "%s%s\"name\":%s\"%s %s \\\"%lu\\\"\",%s", i1, i2, sp,
                words[rnd(8)], words[rnd(8)], id, nl);
        fprintf(f, "%s%s\"price\":%s%lu.%02lu,%s", i1, i2, sp, rnd(1000), rnd(100), nl);
        fprintf(f, "%s%s\"active\":%s%s,%s", i1, i2, sp, rnd(2) ? "true" : "false", nl);
        fprintf(f, "%s%s\"tags\":%s[\"%s\",%s\"%s\"],%s", i1, i2, sp, words[rnd(8)], sp,
                words[rnd(8)], nl);
        fprintf(f, "%s%s\"owner\":%s{%s%s%s\"login\":%s\"user%lu\",%s%s%s\"score\":%s%lu%s%s%s},%s",
                i1, i2, sp, nl, i1, i3, sp, rnd(5000), nl, i1, i3, sp, rnd(100), nl,
                i1, i2, nl);
        fprintf(f, "%s%s\"note\":%s\"Caf\xc3\xa9 order, ships in %lu days\\n\"%s",
                i1, i2, sp, 1 + rnd(9), nl);
        fprintf(f, "%s}", i1);
        done += (size_t)(ftell(f) - start);
        id++;
    }
    fprintf(f, "%s]%s", nl, nl);
    fclose(f);
}

static void gen(void)
{
    char path[4096];
    size_t n;
    char *t;

    if (!exists("code.c") || !exists("big.c") || !exists("small.c")) {
        printf("generating code.c, big.c, small.c\n");
        fflush(stdout);
        t = seed_code(&n);
        repeat("code.c", t, n, (size_t)8 << 20);
        repeat("big.c", t, n, (size_t)256 << 20);
        free(t);
        sprintf(path, "%s/seed/src/editor.c", data);
        t = slurp(path, &n);
        repeat("small.c", t, n, 1);
        free(t);
    }
    if (!exists("docs.md")) {
        size_t m;
        char *u;
        printf("generating docs.md\n");
        fflush(stdout);
        sprintf(path, "%s/seed/README.md", data);
        t = slurp(path, &n);
        sprintf(path, "%s/seed/limitations.md", data);
        u = slurp(path, &m);
        t = (char *)realloc(t, n + m);
        memcpy(t + n, u, m);
        repeat("docs.md", t, n + m, (size_t)8 << 20);
        free(t);
        free(u);
    }
    if (!exists("huge.log")) {
        printf("generating huge.log\n");
        fflush(stdout);
        gen_log();
    }
    if (!exists("mini.json") || !exists("pretty.json")) {
        printf("generating mini.json, pretty.json\n");
        fflush(stdout);
        gen_json("mini.json", (size_t)4 << 20, 0, 1);
        gen_json("pretty.json", (size_t)8 << 20, 1, 2);
    }
}

/* ------------------------------------------------------------------ */
/* running, collecting and comparing                                   */
/* ------------------------------------------------------------------ */

typedef struct Metric {
    char name[96];          /* scenario.metric */
    char unit[16];
    double v[16];
    int n;
    double base;
    int has_base;
} Metric;

static Metric metrics[512];
static int nmetrics;

static Metric *find_metric(const char *name, const char *unit)
{
    int i;
    for (i = 0; i < nmetrics; i++)
        if (strcmp(metrics[i].name, name) == 0)
            return &metrics[i];
    if (nmetrics == (int)(sizeof metrics / sizeof metrics[0])) {
        fprintf(stderr, "bench: too many metrics\n");
        exit(1);
    }
    memset(&metrics[nmetrics], 0, sizeof metrics[0]);
    str_copy(metrics[nmetrics].name, sizeof metrics[0].name, name);
    str_copy(metrics[nmetrics].unit, sizeof metrics[0].unit, unit);
    return &metrics[nmetrics++];
}

static double median(Metric *m)
{
    Samples s;
    s.v = m->v;
    s.n = s.cap = m->n;
    return pct(&s, 50);
}

/* Runs a scenario in a child process and collects what it reports. */
static int run(const Scenario *sc)
{
    int fd[2], status;
    pid_t pid;
    FILE *f;
    char line[256];

    fflush(stdout);
    if (pipe(fd) < 0)
        return -1;
    pid = fork();
    if (pid == 0) {
        close(fd[0]);
        dup2(fd[1], 1);
        close(fd[1]);
        sc->run();
        fflush(stdout);
        _exit(0);
    }
    close(fd[1]);
    f = fdopen(fd[0], "r");
    while (fgets(line, sizeof line, f)) {
        char name[64], unit[16], full[96];
        double v;
        Metric *m;
        if (sscanf(line, "%63s %lf %15s", name, &v, unit) != 3)
            continue;
        sprintf(full, "%s.%s", sc->name, name);
        m = find_metric(full, unit);
        if (m->n < 16)
            m->v[m->n++] = v;
    }
    fclose(f);
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "bench: %s failed\n", sc->name);
        return -1;
    }
    return 0;
}

static void load_base(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256], name[96], unit[16];
    double v;
    if (!f) {
        fprintf(stderr, "bench: %s: %s\n", path, strerror(errno));
        exit(1);
    }
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "%95s %lf %15s", name, &v, unit) == 3) {
            Metric *m = find_metric(name, unit);
            m->base = v;
            m->has_base = 1;
        }
    fclose(f);
}

static void print_results(FILE *out)
{
    int i;
    for (i = 0; i < nmetrics; i++) {
        Metric *m = &metrics[i];
        double v = median(m);
        if (!m->n)
            continue;
        if (out) {
            fprintf(out, "%s\t%.3f\t%s\n", m->name, v, m->unit);
            continue;
        }
        printf("  %-36s %11.2f %-5s", m->name, v, m->unit);
        if (m->has_base) {
            /* Throughput is better higher, everything else lower. Changes
             * under 10%, or under 0.1 ms, are within the noise. */
            int higher = strcmp(m->unit, "MB/s") == 0;
            double d = m->base ? (v - m->base) / m->base * 100 : 0;
            int sig = (d > 10 || d < -10) &&
                      (strcmp(m->unit, "ms") != 0 || v - m->base > 0.1 ||
                       v - m->base < -0.1);
            printf(" %11.2f  %+6.1f%%%s", m->base, d,
                   !sig ? "" : (d > 0) == higher ? "  better" : "  worse");
        }
        printf("\n");
    }
}

static void usage(void)
{
    int i;
    fprintf(stderr, "usage: bench [-d dir] [-r runs] [-o out.tsv] [-b base.tsv] "
                    "[scenario...]\nscenarios:");
    for (i = 0; i < NSCEN; i++)
        fprintf(stderr, " %s", scenarios[i].name);
    fprintf(stderr, "\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *out = NULL, *base = NULL;
    int runs = 3, i, k, r, ok = 1, nsel = 0;
    char **sel;

    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        if (i + 1 >= argc)
            usage();
        if (strcmp(argv[i], "-d") == 0)
            data = argv[++i];
        else if (strcmp(argv[i], "-r") == 0)
            runs = atoi(argv[++i]);
        else if (strcmp(argv[i], "-o") == 0)
            out = argv[++i];
        else if (strcmp(argv[i], "-b") == 0)
            base = argv[++i];
        else
            usage();
    }
    if (runs < 1 || runs > 16)
        usage();
    sel = argv + i;
    nsel = argc - i;
    putenv("CEDIT_CONFIG=");
    if (!getenv("SDL_VIDEODRIVER"))
        putenv("SDL_VIDEODRIVER=offscreen");
    gen();
    if (base)
        load_base(base);

    for (k = 0; k < NSCEN; k++) {
        const Scenario *sc = &scenarios[k];
        int want = nsel == 0;
        for (i = 0; i < nsel; i++)
            if (strncmp(sc->name, sel[i], strlen(sel[i])) == 0)
                want = 1;
        if (!want)
            continue;
        printf("%-14s", sc->name);
        for (r = 0; r < runs; r++) {
            printf(" .");
            if (run(sc) < 0)
                ok = 0;
        }
        printf("\n");
    }
    printf("\n  %-36s %11s %-5s%s\n", "metric", "median", "",
           base ? "        base  change" : "");
    print_results(NULL);
    if (out) {
        FILE *f = fopen(out, "w");
        if (!f) {
            fprintf(stderr, "bench: %s: %s\n", out, strerror(errno));
            return 1;
        }
        print_results(f);
        fclose(f);
    }
    return ok ? 0 : 1;
}
