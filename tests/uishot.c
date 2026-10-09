/*
 * uishot.c - drives the real UI headlessly and saves screenshots, for
 * checking the look without a display.
 *   SDL_VIDEODRIVER=dummy uishot out-prefix file [script]
 * Script tokens: k:<keyname>[+shift|+ctrl|+alt] t:<text> c:<col>,<row> (click)
 *                d:<col>,<row> (double click) r:<col>,<row> (right click)
 *                w:<n> (wheel) s (screenshot)
 *                m:<col>,<row> (move the mouse there)
 *                g:<col>,<row>,<col>,<row> (drag from one cell to the other)
 *                p:<ms> (wait, running the app's timers)
 *                z:<w>x<h> (resize the window, in window units)
 * The script stops when the app exits.
 */
#define _XOPEN_SOURCE 700
#include "../src/ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static App app;
static int shots;
static const char *prefix;

/* Draws, after the searches and saves going on are over (a save's last
 * part runs on a thread), so that each step shows where they end. */
static void frame(void)
{
    while (app.job || app.save) {
        app_tick(&app);
        if (app.save_thread)
            SDL_Delay(1);
    }
    app_draw(&app);
}

static void shot(void)
{
    char name[512];
    SDL_Surface *s;
    int w, h;
    frame();
    SDL_GetRendererOutputSize(app.scr.ren, &w, &h);
    s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(app.scr.ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    sprintf(name, "%s%02d.bmp", prefix, shots++);
    SDL_SaveBMP(s, name);
    SDL_FreeSurface(s);
    printf("saved %s\n", name);
}

static void key(const char *spec)
{
    SDL_Event e;
    char name[64];
    const char *plus = strchr(spec, '+');
    Uint16 mod = 0;
    size_t n = plus ? (size_t)(plus - spec) : strlen(spec);
    memcpy(name, spec, n);
    name[n] = 0;
    if (plus && strstr(plus, "shift")) mod |= KMOD_LSHIFT;
    if (plus && strstr(plus, "ctrl")) mod |= KMOD_LCTRL;
    if (plus && strstr(plus, "alt")) mod |= KMOD_LALT;
    memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDL_GetKeyFromName(name);
    e.key.keysym.mod = mod;
    SDL_SetModState((SDL_Keymod)mod);
    app_event(&app, &e);
    e.type = SDL_KEYUP;
    app_event(&app, &e);
    SDL_SetModState(KMOD_NONE);
    frame();
}

/* The window coordinates of cell (cx, cy). */
static int win_x(int cx)
{
    return (cx * app.scr.cw * app.scr.scale + 2) * app.scr.win_w / app.scr.out_w;
}

static int win_y(int cy)
{
    return (cy * app.scr.ch * app.scr.scale + 2) * app.scr.win_h / app.scr.out_h;
}

static void drag(int x0, int y0, int x1, int y1)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEBUTTONDOWN;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = win_x(x0);
    e.button.y = win_y(y0);
    app_event(&app, &e);
    memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEMOTION;
    e.motion.x = win_x(x1);
    e.motion.y = win_y(y1);
    app_event(&app, &e);
    memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEBUTTONUP;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = win_x(x1);
    e.button.y = win_y(y1);
    app_event(&app, &e);
    frame();
}

static void click(int cx, int cy, int button, int times)
{
    SDL_Event e;
    int i;
    int px = win_x(cx);
    int py = win_y(cy);
    for (i = 0; i < times; i++) {
        memset(&e, 0, sizeof e);
        e.type = SDL_MOUSEBUTTONDOWN;
        e.button.button = (Uint8)button;
        e.button.x = px;
        e.button.y = py;
        app_event(&app, &e);
        e.type = SDL_MOUSEBUTTONUP;
        app_event(&app, &e);
    }
    frame();
}

int main(int argc, char **argv)
{
    int i;
    if (argc < 3)
        return 1;
    prefix = argv[1];
    if (!getenv("CEDIT_CONFIG"))
#ifdef _WIN32
        putenv("CEDIT_CONFIG=NUL");    /* "NAME=" removes NAME there */
#else
        putenv("CEDIT_CONFIG=");
#endif
    SDL_SetMainReady();
    SDL_Init(SDL_INIT_VIDEO);
    if (app_init(&app, 2, argv + 1) < 0) {
        fprintf(stderr, "init: %s\n", SDL_GetError());
        return 1;
    }
    while (buf_loading(app.win->ed.doc->buf))
        app_tick(&app);
    app.blink_on = 1;
    app.blink_next = 0xFFFFFFFFUL;
    /* no pointer until the script moves the mouse (m:) */
    screen_pointer(&app.scr, 0, 0, 0);
    for (i = 3; i < argc && app.running; i++) {
        const char *t = argv[i];
        if (t[0] == 'k' && t[1] == ':') key(t + 2);
        else if (t[0] == 't' && t[1] == ':') {
            SDL_Event e;
            memset(&e, 0, sizeof e);
            e.type = SDL_TEXTINPUT;
            strncpy(e.text.text, t + 2, sizeof e.text.text - 1);
            app_event(&app, &e);
            frame();
        } else if ((t[0] == 'c' || t[0] == 'd' || t[0] == 'r') && t[1] == ':') {
            int x, y;
            sscanf(t + 2, "%d,%d", &x, &y);
            click(x, y, t[0] == 'r' ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT, t[0] == 'd' ? 2 : 1);
        } else if (t[0] == 'g' && t[1] == ':') {
            int x0, y0, x1, y1;
            sscanf(t + 2, "%d,%d,%d,%d", &x0, &y0, &x1, &y1);
            drag(x0, y0, x1, y1);
        } else if (t[0] == 'm' && t[1] == ':') {
            SDL_Event e;
            int x, y;
            sscanf(t + 2, "%d,%d", &x, &y);
            memset(&e, 0, sizeof e);
            e.type = SDL_MOUSEMOTION;
            e.motion.x = (x * app.scr.cw * app.scr.scale + 3) * app.scr.win_w / app.scr.out_w;
            e.motion.y = (y * app.scr.ch * app.scr.scale + 3) * app.scr.win_h / app.scr.out_h;
            app_event(&app, &e);
            frame();
        } else if (t[0] == 'w' && t[1] == ':') {
            SDL_Event e;
            memset(&e, 0, sizeof e);
            e.type = SDL_MOUSEWHEEL;
            e.wheel.y = atoi(t + 2);
            app_event(&app, &e);
            frame();
        } else if (t[0] == 'p' && t[1] == ':') {
            Uint32 end = SDL_GetTicks() + (Uint32)atoi(t + 2);
            while (!SDL_TICKS_PASSED(SDL_GetTicks(), end)) {
                SDL_Delay(10);
                app_tick(&app);
            }
            frame();
        } else if (t[0] == 'z' && t[1] == ':') {
            SDL_Event e;
            int w, h;
            sscanf(t + 2, "%dx%d", &w, &h);
            SDL_SetWindowSize(app.scr.win, w, h);
            memset(&e, 0, sizeof e);
            e.type = SDL_WINDOWEVENT;
            e.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
            e.window.data1 = w;
            e.window.data2 = h;
            app_event(&app, &e);
            frame();
        } else if (t[0] == 's' && !t[1]) shot();
    }
    if (!app.running)
        printf("exited before token %d\n", i - 2);
    app_quit(&app);
    SDL_Quit();
    return 0;
}
