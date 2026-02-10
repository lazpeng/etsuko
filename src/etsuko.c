#include "etsuko.h"

#include <stdio.h>
#include <stdlib.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "error.h"
#include "karaoke.h"
#include "main_menu.h"
#include "renderer.h"
#include "ui.h"

struct AppState {
    OWNING void *state;
    Config_OpMode_t current_mode, next_mode;
    // Whether the current mode finished loading
    bool finished_loading;
};

static struct AppState *g_state = NULL;

static void error_callback(const int error, const char *description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

void global_init(void) {
    if ( g_state != NULL )
        error_abort("global_init called more than once");
    g_state = calloc(1, sizeof(*g_state));
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

    g_state->next_mode = mode;
    if ( g_state->current_mode == APP_MODE_NONE )
        global_update();
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
        MainMenu_t *menu = g_state->state;
        AppStatus_t status = menu_load_loop(menu);
        if ( status == APP_STATUS_OK ) {
            menu_setup(menu);
            g_state->finished_loading = true;
        }
        return status;
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
        MainMenu_t *menu = g_state->state;
        status = menu_loop(menu);
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

void global_update() {
    if ( g_state == NULL )
        error_abort("global_update: state is null. Call global_init first");

    if ( g_state->next_mode != APP_MODE_NONE ) {
        if ( g_state->current_mode == APP_MODE_KARAOKE ) {
            karaoke_finish(g_state->state);
        } else if ( g_state->current_mode == APP_MODE_MENU ) {
            menu_finish(g_state->state);
        }

        if ( g_state->state != NULL ) {
            free(g_state->state);
            g_state->state = NULL;
        }
        const Config_OpMode_t mode = g_state->next_mode;

        g_state->current_mode = mode;
        g_state->finished_loading = false;

        if ( mode == APP_MODE_KARAOKE ) {
            g_state->state = karaoke_init();
        } else if ( mode == APP_MODE_MENU ) {
            g_state->state = menu_init();
        }

        g_state->next_mode = APP_MODE_NONE;
    }
}

void etsuko_setup_version(Ui_t *ui) {
    Drawable_t *version_text =
        ui_make_text(ui,
                     &(Drawable_TextData_t){
                         .text = "etsuko v" VERSION,
                         .font_type = FONT_UI,
                         .em = 0.8,
                         .color = {255, 255, 255, 255},
                     },
                     ui_root_container(ui), &(Layout_t){.offset_x = -1, .flags = LAYOUT_ANCHOR_RIGHT_X | LAYOUT_WRAP_AROUND_X});
    ui_drawable_set_alpha_immediate(version_text, 128);
}
