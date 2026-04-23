#include "fps_counter.h"

#include "config.h"
#include "ui.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Drawable_t *g_drawable = NULL;
static double g_window_start = 0.0;
static int g_frames_in_window = 0;

void fps_counter_setup(Ui_t *ui) {
    if ( !config_get()->show_fps )
        return;

    g_drawable = ui_make_text(ui,
                              &(Drawable_TextData_t){
                                  .text = "-- fps",
                                  .font_type = FONT_UI,
                                  .em = 0.8,
                                  .color = {255, 255, 255, 255},
                                  .draw_shadow = true,
                              },
                              ui_root_container(ui), &(Layout_t){.z_index = 1000});
    g_window_start = glfwGetTime();
    g_frames_in_window = 0;
}

void fps_counter_update(void) {
    if ( !config_get()->show_fps || g_drawable == NULL )
        return;

    g_frames_in_window++;
    const double now = glfwGetTime();
    const double elapsed = now - g_window_start;
    if ( elapsed < 0.5 )
        return;

    const int fps = (int)(g_frames_in_window / elapsed + 0.5);
    g_window_start = now;
    g_frames_in_window = 0;

    char buf[32];
    snprintf(buf, sizeof(buf), "%d fps", fps);

    Drawable_TextData_t *data = g_drawable->custom_data;
    if ( strcmp(buf, data->text) != 0 ) {
        free(data->text);
        data->text = strdup(buf);
        ui_recompute_drawable(g_drawable);
    }
}
