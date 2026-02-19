#include "events.h"

#include "config.h"

#include <stdio.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#define SCROLL_MODIFIER (50)
#else
#define SCROLL_MODIFIER (10)
#endif

struct Events_t {
    struct {
        int32_t x, y;
    } prev_mouse;
    struct {
        int32_t x, y;
        double elapsed_since_move;
        bool clicked;
        double scrolled;
    } mouse;
    bool quit;
    struct {
        bool resized;
        double pixel_scale;
    } window;
    struct {
        double prev_ticks, delta;
    } time;
    struct {
        bool any_pressed;
        bool key_presses[KEY_INVALID];
    } keyboard;
} g_events;

static void clear_key_presses(void) { memset(g_events.keyboard.key_presses, 0, sizeof(g_events.keyboard.key_presses)); }

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

static void key_callback(GLFWwindow *, const int key, int, const int action, int) {
    if ( action == GLFW_PRESS ) {
        Key_t k;
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
        case GLFW_KEY_R:
            k = KEY_R;
            break;
        case GLFW_KEY_L:
            k = KEY_L;
            break;
        default:
            return;
        }

        g_events.keyboard.any_pressed = true;
        g_events.keyboard.key_presses[k] = true;
    }
}

static void mouse_button_callback(GLFWwindow *, const int button, const int action, int) {
    if ( button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS ) {
        g_events.mouse.clicked = true;
    }
}

static void cursor_position_callback(GLFWwindow *window, const double x_pos, const double y_pos) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    if ( x_pos < 0.0 || x_pos > width || y_pos < 0.0 || y_pos > height )
        return;
    g_events.mouse.x = (int32_t)(x_pos * g_events.window.pixel_scale);
    g_events.mouse.y = (int32_t)(y_pos * g_events.window.pixel_scale);
}

static void scroll_callback(GLFWwindow *, double, const double y_offset) { g_events.mouse.scrolled = y_offset; }

static void window_size_callback(GLFWwindow *, int, int) {
    g_events.window.resized = true;
}

void events_setup_callbacks(void *window) {
    GLFWwindow *w = window;
    glfwSetKeyCallback(w, key_callback);
    glfwSetMouseButtonCallback(w, mouse_button_callback);
    glfwSetCursorPosCallback(w, cursor_position_callback);
    glfwSetScrollCallback(w, scroll_callback);
    glfwSetWindowSizeCallback(w, window_size_callback);
}

void events_init(void) {
    g_events = (struct Events_t){0};
}

void events_finish(void) {}

void events_loop(void) {
    const double ticks = glfwGetTime();
    if ( g_events.time.prev_ticks != 0 ) {
        g_events.time.delta  = ticks - g_events.time.prev_ticks;
    }
    g_events.time.prev_ticks = ticks;

    glfwPollEvents();

    if ( glfwWindowShouldClose(glfwGetCurrentContext()) ) {
        g_events.quit = true;
    }

    if ( events_mouse_moved() ) {
        g_events.mouse.elapsed_since_move = 0.0;
    } else {
        g_events.mouse.elapsed_since_move += g_events.time.delta ;
    }
}

void events_frame_end(void) {
    // Reset
    g_events.window.resized = false;
    g_events.mouse.scrolled = 0;
    g_events.mouse.clicked = false;
    g_events.keyboard.any_pressed = false;
    clear_key_presses();
    // Update prev
    g_events.prev_mouse.x = g_events.mouse.x;
    g_events.prev_mouse.y = g_events.mouse.y;
}

double events_get_delta_time(void) { return g_events.time.delta * config_get()->time_scale; }
double events_get_elapsed_time(void) { return glfwGetTime(); }

void events_get_mouse_position(int32_t *x, int32_t *y) {
    if ( x != NULL )
        *x = g_events.mouse.x;
    if ( y != NULL )
        *y = g_events.mouse.y;
}

bool events_get_mouse_click(int32_t *x, int32_t *y) {
    if ( g_events.mouse.clicked ) {
        events_get_mouse_position(x, y);
    }

    return g_events.mouse.clicked;
}

double events_get_mouse_scrolled(void) { return g_events.mouse.scrolled * SCROLL_MODIFIER; }

bool events_any_key_was_pressed(void) {
    return g_events.keyboard.any_pressed;
}

bool events_key_was_pressed(const Key_t key) { return g_events.keyboard.key_presses[key]; }

bool events_has_quit(void) { return g_events.quit; }

bool events_window_changed(void) { return g_events.window.resized; }

void events_set_window_pixel_scale(const double scale) { g_events.window.pixel_scale = scale; }

bool events_mouse_moved(void) {
    return g_events.mouse.x != g_events.prev_mouse.x || g_events.mouse.y != g_events.prev_mouse.y;
}

double events_time_since_mouse_stopped(void) {
    return g_events.mouse.elapsed_since_move;
}
