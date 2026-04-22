/**
 * fps_counter.h - Optional debug FPS counter drawn in the top-left corner.
 * Enabled via Config_t::show_fps. Each mode owns its own Ui_t, so setup must
 * be called once per mode after ui_init, and update must be called once per
 * frame while that mode's ui is active.
 */

#ifndef ETSUKO_FPS_COUNTER_H
#define ETSUKO_FPS_COUNTER_H

struct Ui_t;

// Attach the FPS counter drawable to the given ui. Doesn't do anything if show_fps is false.
void fps_counter_setup(struct Ui_t *ui);
// Tally a frame and refresh the displayed value periodically. No-op if show_fps is false.
void fps_counter_update(void);

#endif // ETSUKO_FPS_COUNTER_H
