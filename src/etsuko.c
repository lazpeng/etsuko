#include "etsuko.h"

#include <stdio.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "error.h"
#include "renderer.h"

int global_init(void) {
    // Init sdl
    if ( SDL_Init(SDL_INIT_VIDEO) != 0 ) {
        puts(SDL_GetError());
        error_abort("SDL_Init failed");
    }

    // Init ttf
    if ( TTF_Init() != 0 ) {
        puts(TTF_GetError());
        error_abort("TTF_Init failed");
    }

    render_init();

    return 0;
}

void global_finish(void) {
    SDL_Quit();
    render_finish();
}
