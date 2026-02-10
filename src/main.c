#include <stdio.h>
#include <stdlib.h>

#include "etsuko.h"
#include "karaoke.h"
#include "config.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static bool main_loop() {
    static bool global_initialized = false;
    if ( !global_initialized ) {
        global_init();
        global_initialized = true;
    }

    if ( global_active_mode() == APP_MODE_NONE ) {
        // No mode initialized, so set the default one set in the config
        global_mode_switch(config_get()->op_mode);
    }

    if ( !global_mode_finished_loading() ) {
        AppStatus_t status = global_load();
        switch ( status ) {
        case APP_STATUS_OK:
            break;
        case APP_STATUS_LOADING:
            return true;
        case APP_STATUS_FAILURE:
            return false;
        }
    }

    return global_loop() == APP_STATUS_OK;
}

#ifdef __EMSCRIPTEN__
static void main_loop_void() {
    main_loop();
}
#endif

int main(void) {
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(main_loop_void, 0, 1);
#else
    while ( main_loop() ) {}
#endif

    global_mode_switch(APP_MODE_NONE);
    global_finish();
    return EXIT_SUCCESS;
}
