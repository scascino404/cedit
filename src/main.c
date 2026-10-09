/*
 * main.c - cedit, a classic text editor.
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

/* Keys that move the cursor or a menu selection a step at a time. */
static int step_key(SDL_Keycode sym)
{
    return sym == SDLK_UP || sym == SDLK_DOWN || sym == SDLK_LEFT ||
           sym == SDLK_RIGHT || sym == SDLK_PAGEUP || sym == SDLK_PAGEDOWN;
}

/* While a window edge is dragged, macOS and Windows hold the main loop
 * until the mouse is released and stretch the last frame to the window,
 * distorting the text. An event watch still sees each resize: draw there. */
static int SDLCALL resize_watch(void *data, SDL_Event *e)
{
    App *app = (App *)data;
    if (e->type == SDL_WINDOWEVENT &&
        e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        app_event(app, e);
        app_draw(app);
    }
    return 0;
}

int main(int argc, char **argv)
{
    static App app;
    SDL_Event e;
    SDL_Keysym last;

    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        puts("usage: cedit [file]");
        return 0;
    }
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "cedit: %s\n", SDL_GetError());
        return 1;
    }
    if (app_init(&app, argc, argv) < 0) {
        fprintf(stderr, "cedit: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_AddEventWatch(resize_watch, &app);
    while (app.running) {
        app_draw(&app);
        if (SDL_WaitEventTimeout(&e, app_timeout(&app))) {
            /* A held key's repeats pile up while the loop is late, then
             * come all at once; the cursor takes one step per draw, not
             * a jump. Only the first press of a step key counts. */
            last.sym = SDLK_UNKNOWN;
            last.mod = KMOD_NONE;
            do {
                if (e.type == SDL_KEYDOWN && step_key(e.key.keysym.sym)) {
                    if (e.key.keysym.sym == last.sym && e.key.keysym.mod == last.mod)
                        continue;
                    last = e.key.keysym;
                }
                app_event(&app, &e);
            } while (app.running && SDL_PollEvent(&e));
        }
        app_tick(&app);
    }
    app_quit(&app);
    SDL_Quit();
    return 0;
}
