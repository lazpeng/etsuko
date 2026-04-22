#include "config.h"

#include "str_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Config_t *g_config = NULL;

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

#include "constants.h"
#include "str_utils.h"

EM_JS(const char *, get_song_param, (void), {
    const params = new URLSearchParams(window.location.search);
    const song = params.get("song") || "";
    return stringToNewUTF8(song);
})

static void try_load_config_web(Config_t *config) {
    const char *song = get_song_param();
    if ( strlen(song) > 0 ) {
        config_set_karaoke_song_file(config, song);
        config->op_mode = APP_MODE_KARAOKE;
    }
}

#endif

static Config_t *get_default_config(void) {
    Config_t *config = malloc(sizeof(*config));
    if ( config == NULL ) {
        return NULL;
    }
    config->lyrics_font = strdup("NotoSans_ExtraCondensed-Bold.ttf");
    config->ui_font = strdup("NotoSansJP-Regular.ttf");
    config->op_mode = APP_MODE_MENU;
    config->time_scale = 1.0;
    config->karaoke = (KaraokeOpts_t) {
        .song_file = strdup("tiny stars.txt"),
        .hide_ui_elements_delay_sec = 2.0,
        .enable_dynamic_fill = true,
        .enable_reading_hints = true,
        .enable_pulse_effect = true,
        .hide_past_lyrics = false,
        .draw_album_art_shadow = true,
        .draw_lyric_shadow = true,
        .show_loading_screen = true,
        .enlarge_active_line = false,
        .position_hints_under_segment = true,
        .blur_lyrics = true,
        .blur_credits = true,
    };
    config->settings = (SettingsOpts_t) {
        .show_settings = true
    };
    config->show_fps = false;
    config->vsync = true;

#ifdef __EMSCRIPTEN__
    try_load_config_web(config);
#endif

    return config;
}

Config_t *config_get(void) {
    if ( g_config == NULL ) {
        g_config = get_default_config();
    }
    return g_config;
}

void config_set_karaoke_song_file(Config_t *config, const char *id) {
    if ( config->karaoke.song_file != NULL )
        free(config->karaoke.song_file);

    asprintf(&config->karaoke.song_file, "%s.txt", id);
    str_replace_char(config->karaoke.song_file, '_', ' ');
}
