#include "etsuko.h"

#include <stdio.h>
#include <stdlib.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "error.h"
#include "renderer.h"
#include "karaoke.h"

struct AppState {
    OWNING void *state;
    Config_OpMode_t current_mode;
    // Whether the current mode finished loading
    bool finished_loading;
};

static struct AppState *g_state = NULL;

static void error_callback(const int error, const char *description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

void global_init(void) {
    if (g_state != NULL )
        error_abort("global_init called more than once");
    g_state = calloc(1, sizeof (*g_state));
    if ( g_state == NULL )
        error_abort("Failed to allocate the global sate\n");
    g_state->finished_loading = true; // Default mode is NONE

    glfwSetErrorCallback(error_callback);

    if ( !glfwInit() ) {
        error_abort("glfwInit failed");
    }

    render_init();
}

void global_finish(void) {
    render_finish();
    glfwTerminate();
}

void global_mode_switch(Config_OpMode_t mode) {
    if ( g_state == NULL )
        error_abort("global_mode_switch: state is null. Call global_init first");
    if ( !g_state->finished_loading )
        printf("global_mode_switch: The current mode hasn't finished loading yet, weird shit could happen\n");

    if ( g_state->current_mode == APP_MODE_KARAOKE ) {
        karaoke_finish((Karaoke_t *)g_state->state);
    } else if ( g_state->current_mode == APP_MODE_MENU ) {
        // TODO
    }

    if ( g_state->state != NULL ) {
        free(g_state->state);
        g_state->state = NULL;
    }

    g_state->current_mode = mode;
    g_state->finished_loading = false;

    if ( mode == APP_MODE_KARAOKE ) {
        g_state->state = karaoke_init();
    } else if ( mode == APP_MODE_MENU ) {
        // TODO
    }
}

AppStatus_t global_load() {
    if ( g_state == NULL )
        error_abort("global_load: state is null. Call global_init first");
    if ( g_state->finished_loading ) {
        printf("global_load: Current loaded mode has already finished loading before this call, fix your shit up\n");
        return APP_STATUS_OK;
    }

    if ( g_state->current_mode == APP_MODE_KARAOKE ) {
        Karaoke_t *karaoke = g_state->state;
        AppStatus_t initialized = karaoke_load_loop(karaoke);
        if ( initialized == APP_STATUS_OK ) {
            karaoke_setup(karaoke);
            g_state->finished_loading = true;
        }
        return initialized;
    } else if ( g_state->current_mode == APP_MODE_MENU ) {
        // TODO
    }

    return APP_STATUS_OK;
}

AppStatus_t global_loop() {
    if ( g_state == NULL )
        error_abort("global_loop: state is null. Call global_init first");
    if ( !g_state->finished_loading ) {
        error_abort("global_loop: The current mode hasn't finished loading yet. Something went very wrong in the main loop\n");
    }
    AppStatus_t status = APP_STATUS_OK;
    if ( g_state->current_mode == APP_MODE_KARAOKE ) {
        Karaoke_t *karaoke = g_state->state;
        status = karaoke_loop(karaoke);
    } else if ( g_state->current_mode == APP_MODE_MENU ) {
        // TODO
    }

    return status;
}

Config_OpMode_t global_active_mode() {
    if ( g_state == NULL )
        error_abort("global_active_mode: state is null. Call global_init first");
    return g_state->current_mode;
}

bool global_mode_finished_loading() {
    if ( g_state == NULL )
        error_abort("global_mode_finished_loading: state is null. Call global_init first");
    return g_state->finished_loading;
}
