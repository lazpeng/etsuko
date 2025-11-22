#include "events.h"

#include <stdio.h>
#include <string.h>

static bool g_quit = false;
static bool g_window_resized = false;
static double g_mouse_scroll = 0;
static int32_t g_mouse_x, g_mouse_y;
static bool g_mouse_clicked = false;
static bool g_key_presses[KEY_INVALID] = {0};
static double g_window_pixel_scale = 1.0;
static double g_prev_ticks = 0;
static double g_delta_time = 0;

static void clear_key_presses(void) { memset(g_key_presses, 0, sizeof(g_key_presses)); }

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

static void key_callback(GLFWwindow *, const int key, int, const int action, int) {
    if ( action == GLFW_PRESS ) {
        etsuko_Key_t k;
        switch ( key ) {
        case GLFW_KEY_SPACE:
            k = KEY_SPACE;
            break;
        case GLFW_KEY_LEFT:
            k = KEY_ARROW_LEFT;
            break;
        case GLFW_KEY_RIGHT:
            k = KEY_ARROW_RIGHT;
            break;
        default:
            return;
        }

        g_key_presses[k] = true;
    }
}

static void mouse_button_callback(GLFWwindow *window, const int button, const int action, int) {
    if ( button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS ) {
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        g_mouse_x = (int32_t)(x * g_window_pixel_scale);
        g_mouse_y = (int32_t)(y * g_window_pixel_scale);
        g_mouse_clicked = true;
    }
}

static void cursor_position_callback(GLFWwindow *, const double x_pos, const double y_pos) {
    g_mouse_x = (int32_t)(x_pos * g_window_pixel_scale);
    g_mouse_y = (int32_t)(y_pos * g_window_pixel_scale);
}

static void scroll_callback(GLFWwindow *, double, const double y_offset) { g_mouse_scroll += y_offset; }

static void window_size_callback(GLFWwindow *, int, int) {
    g_window_resized = true;
}

void events_setup_callbacks(void *window) {
    GLFWwindow *w = window;
    glfwSetKeyCallback(w, key_callback);
    glfwSetMouseButtonCallback(w, mouse_button_callback);
    glfwSetCursorPosCallback(w, cursor_position_callback);
    glfwSetScrollCallback(w, scroll_callback);
    glfwSetWindowSizeCallback(w, window_size_callback);
}

void events_init(void) {}

void events_finish(void) {}

void events_loop(void) {
    const double ticks = glfwGetTime();
    if ( g_prev_ticks != 0 ) {
        g_delta_time = ticks - g_prev_ticks;
    }
    g_prev_ticks = ticks;

    glfwPollEvents();

    if ( glfwWindowShouldClose(glfwGetCurrentContext()) ) {
        g_quit = true;
    }
}

void events_frame_end(void) {
    // Reset
    g_window_resized = false;
    g_mouse_scroll = 0;
    g_mouse_clicked = false;
    // Don't clear mouse_x and mouse_y
    clear_key_presses();
}

double events_get_delta_time(void) { return g_delta_time; }
double events_get_elapsed_time(void) { return glfwGetTime(); }

void events_get_mouse_position(int32_t *x, int32_t *y) {
    if ( x != NULL )
        *x = g_mouse_x;
    if ( y != NULL )
        *y = g_mouse_y;
}

bool events_get_mouse_click(int32_t *x, int32_t *y) {
    if ( g_mouse_clicked ) {
        events_get_mouse_position(x, y);
    }

    return g_mouse_clicked;
}

double events_get_mouse_scrolled(void) { return g_mouse_scroll; }

bool events_key_was_pressed(const etsuko_Key_t key) { return g_key_presses[key]; }

bool events_has_quit(void) { return g_quit; }

bool events_window_changed(void) { return g_window_resized; }

void events_set_window_pixel_scale(const double scale) { g_window_pixel_scale = scale; }
