/*
 * main.c - cedit, a classic text editor.
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    static App app;
    SDL_Event e;

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
    while (app.running) {
        app_draw(&app);
        if (SDL_WaitEventTimeout(&e, app_timeout(&app))) {
            do
                app_event(&app, &e);
            while (app.running && SDL_PollEvent(&e));
        }
        app_tick(&app);
    }
    app_quit(&app);
    SDL_Quit();
    return 0;
}
