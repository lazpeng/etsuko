#include "events.h"

#include <SDL2/SDL.h>

#include "container_utils.h"

static bool g_quit = false;
static bool g_window_resized = false;
static double g_mouse_scroll = 0;
static int32_t g_mouse_x, g_mouse_y;
static bool g_mouse_clicked = false;
static int32_t g_mouse_click_x, g_mouse_click_y;
static Vector_t *g_key_presses;
static double g_window_pixel_scale = 1.0;
static uint64_t g_prev_ticks = 0;
static uint64_t g_start_ticks = 0;
static double g_delta_time = 0;

static void clear_key_presses(void) {
    // Remove one by one from right to left
    while ( g_key_presses->size > 0 ) {
        const size_t idx = g_key_presses->size - 1;
        free(g_key_presses->data[idx]);
        vec_remove(g_key_presses, idx);
    }
}

static etsuko_Key_t *key_from_event(const SDL_Event *e) {
    etsuko_Key_t key;
    switch ( e->key.keysym.sym ) {
    case SDLK_SPACE:
        key = KEY_SPACE;
        break;
    case SDLK_LEFT:
        key = KEY_ARROW_LEFT;
        break;
    case SDLK_RIGHT:
        key = KEY_ARROW_RIGHT;
        break;
    default:
        return NULL;
    }

    etsuko_Key_t *result = calloc(1, sizeof(etsuko_Key_t));
    *result = key;
    return result;
}

void events_init(void) { g_key_presses = vec_init(); g_start_ticks = SDL_GetTicks64(); }

void events_finish(void) { vec_destroy(g_key_presses); }

void events_loop(void) {
    // Reset
    g_window_resized = false;
    g_mouse_scroll = 0;
    g_mouse_clicked = false;
    g_mouse_click_x = g_mouse_click_y = 0;
    // Don't clear mouse_x and mouse_y
    clear_key_presses();

    const uint64_t ticks = SDL_GetTicks64();
    if ( g_prev_ticks != 0 ) {
        g_delta_time = (double)(ticks - g_prev_ticks) / 1000.0;
    }
    g_prev_ticks = ticks;

    etsuko_Key_t *key;

    SDL_Event event;
    while ( SDL_PollEvent(&event) ) {
        switch ( event.type ) {
        case SDL_QUIT:
            g_quit = true;
            break;
        case SDL_WINDOWEVENT:
            if ( event.window.event == SDL_WINDOWEVENT_RESIZED ) {
                g_window_resized = true;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            g_mouse_click_x = (int32_t)(event.button.x * g_window_pixel_scale);
            g_mouse_click_y = (int32_t)(event.button.y * g_window_pixel_scale);
            g_mouse_clicked = true;
            break;
        case SDL_MOUSEMOTION:
            g_mouse_x = (int32_t)(event.motion.x * g_window_pixel_scale);
            g_mouse_y = (int32_t)(event.motion.y * g_window_pixel_scale);
            break;
        case SDL_MOUSEWHEEL:
            g_mouse_scroll += event.wheel.y;
            break;
        case SDL_KEYDOWN:
            if ( (key = key_from_event(&event)) != NULL ) {
                vec_add(g_key_presses, key);
            }
            break;
        default:
            break;
        }
    }
}

double events_get_delta_time(void) { return g_delta_time; }
double events_get_elapsed_time(void) { return (double)(SDL_GetTicks64() - g_start_ticks) / 1000.0; }

void events_get_mouse_position(int32_t *x, int32_t *y) {
    if ( x != NULL )
        *x = g_mouse_x;
    if ( y != NULL )
        *y = g_mouse_y;
}

bool events_mouse_was_clicked_inside_area(const int32_t x, const int32_t y, const int32_t w, const int32_t h) {
    if ( g_mouse_clicked ) {
        return g_mouse_click_x >= x && g_mouse_click_x <= x + w && g_mouse_click_y >= y && g_mouse_click_y <= y + h;
    }

    return false;
}

bool events_get_mouse_click(int32_t *x, int32_t *y) {
    if ( g_mouse_clicked ) {
        events_get_mouse_position(x, y);
    }

    return g_mouse_clicked;
}

double events_get_mouse_scrolled(void) { return g_mouse_scroll; }

bool events_key_was_pressed(const etsuko_Key_t key) {
    for ( size_t i = 0; i < g_key_presses->size; i++ ) {
        if ( *(etsuko_Key_t *)g_key_presses->data[i] == key ) {
            return true;
        }
    }
    return false;
}

bool events_has_quit(void) { return g_quit; }

bool events_window_changed(void) { return g_window_resized; }

void events_set_window_pixel_scale(const double scale) { g_window_pixel_scale = scale; }
