/*
 * uishot.c - drives the real UI headlessly and saves screenshots, for
 * checking the look without a display.
 *   SDL_VIDEODRIVER=offscreen uishot out-prefix file [script]
 * Script tokens: k:<keyname>[+shift|+ctrl|+alt] t:<text> c:<col>,<row> (click)
 *                d:<col>,<row> (double click) w:<n> (wheel) s (screenshot)
 *                m:<col>,<row> (move the mouse there)
 */
#define _XOPEN_SOURCE 700
#include "../src/ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static App app;
static int shots;
static const char *prefix;

static void frame(void)
{
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

static void click(int cx, int cy, int times)
{
    SDL_Event e;
    int i;
    int px = (cx * app.scr.cw * app.scr.scale + 2) * app.scr.win_w / app.scr.out_w;
    int py = (cy * app.scr.ch * app.scr.scale + 2) * app.scr.win_h / app.scr.out_h;
    for (i = 0; i < times; i++) {
        memset(&e, 0, sizeof e);
        e.type = SDL_MOUSEBUTTONDOWN;
        e.button.button = SDL_BUTTON_LEFT;
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
        putenv("CEDIT_CONFIG=");
    SDL_Init(SDL_INIT_VIDEO);
    if (app_init(&app, 2, argv + 1) < 0) {
        fprintf(stderr, "init: %s\n", SDL_GetError());
        return 1;
    }
    while (buf_loading(app.ed.buf))
        app_tick(&app);
    app.blink_on = 1;
    app.blink_next = 0xFFFFFFFFUL;
    for (i = 3; i < argc; i++) {
        const char *t = argv[i];
        if (t[0] == 'k' && t[1] == ':') key(t + 2);
        else if (t[0] == 't' && t[1] == ':') {
            SDL_Event e;
            memset(&e, 0, sizeof e);
            e.type = SDL_TEXTINPUT;
            strncpy(e.text.text, t + 2, sizeof e.text.text - 1);
            app_event(&app, &e);
            frame();
        } else if ((t[0] == 'c' || t[0] == 'd') && t[1] == ':') {
            int x, y;
            sscanf(t + 2, "%d,%d", &x, &y);
            click(x, y, t[0] == 'd' ? 2 : 1);
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
        } else if (t[0] == 's' && !t[1]) shot();
    }
    app_quit(&app);
    SDL_Quit();
    return 0;
}
