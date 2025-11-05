#include <stdio.h>
#include <stdlib.h>

#include "etsuko.h"
#include "karaoke.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

static int g_initialized = 0;

static void web_entrypoint() {
    if ( !g_initialized ) {
        if ( global_init() != 0 ) {
            printf("Failed to initialize global");
            return;
        }
        g_initialized = karaoke_load_async();
        if ( g_initialized )
            karaoke_init();
        return;
    }

    karaoke_loop();
}
#endif

int main() {
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(web_entrypoint, 0, 1);
#else
    if ( global_init() != 0 ) {
        printf("Failed to initialize global");
        return EXIT_FAILURE;
    }
    do {
    } while ( karaoke_load_async() == 0 );

    karaoke_init();
    do {
    } while ( karaoke_loop() == 0 );
#endif

    karaoke_finish();
    global_finish();
    return EXIT_SUCCESS;
}
