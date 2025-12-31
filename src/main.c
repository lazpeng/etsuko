#include <stdio.h>
#include <stdlib.h>

#include "etsuko.h"
#include "karaoke.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

typedef struct {
    Karaoke_t *karaoke;
    bool initialized;
} EntryPointArgs_t;

static void web_entrypoint(void *em_arg) {
    EntryPointArgs_t *args = em_arg;
    if ( !args->initialized ) {
        if ( args->karaoke == NULL ) {
            if ( global_init() != 0 ) {
                printf("Failed to initialize global");
                return;
            }
            args->karaoke = karaoke_init();
        }

        args->initialized = karaoke_load_loop(args->karaoke);
        if ( args->initialized )
            karaoke_setup(args->karaoke);
        return;
    }

    karaoke_loop(args->karaoke);
}
#endif

int main(void) {
#ifdef __EMSCRIPTEN__
    EntryPointArgs_t *args = calloc(1, sizeof(*args));
    if ( args == NULL ) {
        error_abort("Failed to allocate args for emscripten");
    }
    args->initialized = false;
    args->karaoke = NULL;

    emscripten_set_main_loop_arg(web_entrypoint, args, 0, 1);
    if ( args->karaoke != NULL ) {
        karaoke_finish(args->karaoke);
    }
    free(args);
#else
    if ( global_init() != 0 ) {
        printf("Failed to initialize global");
        return EXIT_FAILURE;
    }
    Karaoke_t *karaoke = karaoke_init();
    do {
    } while ( karaoke_load_loop(karaoke) == 0 );

    karaoke_setup(karaoke);
    do {
    } while ( karaoke_loop(karaoke) == 0 );

    karaoke_finish(karaoke);
#endif

    global_finish();
    return EXIT_SUCCESS;
}
