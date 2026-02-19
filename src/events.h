/**
 * events.h - Handles system events and user input
 */

#ifndef ETSUKO_EVENTS_H
#define ETSUKO_EVENTS_H

#include <stdbool.h>
#include <stdint.h>

// Keyboard keys
typedef enum Key_t { KEY_SPACE = 0, KEY_ARROW_LEFT, KEY_ARROW_RIGHT, KEY_R, KEY_L, KEY_INVALID } Key_t;

// Does absolutely nothing
void events_init(void);
// Does absolutely nothing 2
void events_finish(void);
// Checks a few stuff like mouse clicks and pointer position for the other event calls
void events_loop(void);
/**
 * This only exists due to a weird aspect of emscripten (or browsers), since events happen asynchronously and due to the way
 * glfw works, all the callbacks are called mostly during the interval between frames, so if we clear the events in the beginning
 * of the frame, nothing is ever registered (unless an event happens in the <1ms duration of a frame)
 * So we clear them at the end to consume all the events generated between when the last frame ended and this one started
 */
void events_frame_end(void);
// glfw specific stuff that registers callbacks to when events happen
void events_setup_callbacks(void *window);
// Returns whether the window was closed, basically
bool events_has_quit(void);
// Returns whether ui should be rearranged/recomputed because the screen's dimensions changed
bool events_window_changed(void);
// Returns the elapsed time since the previous frame and the current one
double events_get_delta_time(void);
// Returns the total elapsed time since the application started
double events_get_elapsed_time(void);
// Returns the mouse pointer position in screen coordinates relative to the pixel scale (in practice only applicable to macOS)
void events_get_mouse_position(int32_t *x, int32_t *y);
// Returns whether there was a mouse click, along with the mouse cursor position relative to the pixel scale (in practice only applicable to macOS)
bool events_get_mouse_click(int32_t *x, int32_t *y);
// Returns the amount of mouse scroll performed by the user in the last frame.
[[deprecated("Use events instead")]]
double events_get_mouse_scrolled(void);
// Whether any key was pressed in the last frame
bool events_any_key_was_pressed(void);
// Returns whether the given key was pressed in the last frame
bool events_key_was_pressed(Key_t key);
// Returns a pixel scale from virtual pixel coordinates to real screen coordinates (in practice is 1.0 in all platforms other than macOS)
void events_set_window_pixel_scale(double scale);

bool events_mouse_moved(void);
// Time since either the mouse stopped moving
double events_time_since_mouse_stopped(void);

#endif // ETSUKO_EVENTS_H
