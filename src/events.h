/**
 * events.h - Handles system events and user input
 */

#ifndef ETSUKO_EVENTS_H
#define ETSUKO_EVENTS_H

#include <stdint.h>
#include <stdbool.h>

typedef enum etsuko_Key_t {
    KEY_SPACE,
    KEY_ARROW_LEFT,
    KEY_ARROW_RIGHT,
} etsuko_Key_t;

// Init, finish and loop
void events_init(void);
void events_finish(void);
void events_loop(void);
// Lifetime events
bool events_has_quit(void);
bool events_window_changed(void);
// Queries for state
double events_get_delta_time(void);
double events_get_elapsed_time(void);
void events_get_mouse_position(int32_t *x, int32_t *y);
bool events_get_mouse_click(int32_t *x, int32_t *y);
bool events_mouse_was_clicked_inside_area(int32_t x, int32_t y, int32_t w, int32_t h);
double events_get_mouse_scrolled(void);
bool events_key_was_pressed(etsuko_Key_t key);
// Config
void events_set_window_pixel_scale(double scale);

#endif // ETSUKO_EVENTS_H
