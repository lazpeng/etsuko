#include <cstdlib>
#include <iostream>

#include "karaoke.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static bool g_quit = false;
static bool g_initialized = false;

static void main_loop(void *param) {
    const auto karaoke = static_cast<etsuko::Karaoke *>(param);

    if ( !g_initialized ) {
        if ( karaoke->initialize() != 0 ) {
            std::puts("Failed to initialize karaoke");
            g_quit = true;
            return;
        }
        g_initialized = true;
    }
    try {
        g_quit = karaoke->loop();
    } catch ( std::exception &e ) {
        std::puts(e.what());
        std::puts("error ocurred");
        g_quit = true;
    }
}

int main() {
    etsuko::Karaoke karaoke;

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(main_loop, &karaoke, 0, true);
#else
    if ( karaoke.initialize() != 0 ) {
        std::puts("Failed to initialize karaoke");
        return EXIT_FAILURE;
    }
    while ( !karaoke.loop() ) {
        karaoke.wait_vsync();
    }
#endif

    return EXIT_SUCCESS;
}
