/**
 * config.h - Global settings for the application to know which stuff to load and use
 */

#ifndef ETSUKO_CONFIG_H
#define ETSUKO_CONFIG_H

#include <stdbool.h>

#include "etsuko.h"
#include "constants.h"

typedef struct KaraokeOpts_t {
    // Main song file name
    OWNING char *song_file;
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
    // Time in seconds to hide optional ui elements after elapsed
    double hide_ui_elements_delay_sec;
} KaraokeOpts_t;

typedef struct SettingsOpts_t {
    bool show_settings;
} SettingsOpts_t;

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
    // Operating mode
    Config_OpMode_t op_mode;
    // Global time scale, generally to debug animations (it applies to audio, and it's weird. use only for debugging)
    double time_scale;
    KaraokeOpts_t karaoke;
    SettingsOpts_t settings;
} Config_t;

// Returns the current configuration for the application
Config_t *config_get(void);

// Sets the song file name for the karaoke portion based on an id
void config_set_karaoke_song_file(Config_t *config, const char *id);

#endif // ETSUKO_CONFIG_H
