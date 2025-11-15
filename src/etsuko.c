#include "etsuko.h"

#include <stdio.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
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

    // Init image
    const int image_formats = IMG_INIT_PNG | IMG_INIT_JPG;
    if ( IMG_Init(image_formats) != image_formats ) {
        puts(IMG_GetError());
        error_abort("IMG_Init failed");
    }

    render_init();

    // Init audio
    const int flags = MIX_INIT_MP3;
    if ( Mix_Init(flags) == 0 ) {
        puts(Mix_GetError());
        error_abort("Mix_Init failed");
    }

    if ( Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 2048) != 0 ) {
        puts(Mix_GetError());
        error_abort("OpenAudio failed");
    }

    return 0;
}

void global_finish(void) {
    Mix_Quit();
    IMG_Quit();
    SDL_Quit();
    render_finish();
}
