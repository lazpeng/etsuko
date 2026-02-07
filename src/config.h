/**
 * config.h - Global settings for the application to know which stuff to load and use
 */

#ifndef ETSUKO_CONFIG_H
#define ETSUKO_CONFIG_H

#include <stdbool.h>

#include "etsuko.h"
#include "constants.h"

/**
 * The config should be a static and permanent way to tell the application how to behave and what to do
 * This should not change during runtime, so for example if reading hints are disabled, they should not be generated
 * because there's no way the user could turn them back on.
 * This is different from settings or preferences that the user can hide/show certain elements at runtime, but that
 * does not control whether those features are actually available at all or not
 */
typedef struct {
    // Font files to be used
    OWNING char *ui_font, *lyrics_font;
    // Main song file name. Only applicable in karaoke mode
    OWNING char *song_file;
    // Operating mode
    Config_OpMode_t op_mode;
    // Makes past lyrics fade away in karaoke mode
    bool hide_past_lyrics;
    // Enables shadow on the album art
    bool draw_album_art_shadow;
    // Enables shadow on the text for each lyric line
    bool draw_lyric_shadow;
    // Shows a loading screen while loading assets
    bool show_loading_screen;
    // Make the active line bigger than the inactive ones
    bool enlarge_active_line;
    // Enable the effect of filling characters when the song timings have support for it
    bool enable_dynamic_fill;
    // Enable the display of reading hints below lyric segments when the song has support for it
    bool enable_reading_hints;
    // Enable a "jumping" or pulsing effect that plays together with the dynamic fill effect. Requires dynamic fill to work
    bool enable_pulse_effect;
} Config_t;

// Returns the current configuration for the application
Config_t *config_get(void);

#endif // ETSUKO_CONFIG_H
