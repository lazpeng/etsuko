#include "config.h"

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
    if ( strnlen(song, MAX_TEXT_SIZE) > 0 ) {
        printf("song: %s\n", song);

        if ( config->song_file != NULL ) {
            free(config->song_file);
        }

        asprintf(&config->song_file, "%s.txt", song);
        str_replace_char(config->song_file, '_', ' ');
    } // else use the default config (for now)
}

#endif

static Config_t *get_default_config(void) {
    Config_t *config = malloc(sizeof(*config));
    if ( config == NULL ) {
        return NULL;
    }
    config->lyrics_font = strdup("NotoSans_ExtraCondensed-Bold.ttf");
    config->ui_font = strdup("NotoSans-Regular.ttf");
    config->song_file = strdup("shirushi.txt");
    config->hide_past_lyrics = true;
    config->draw_album_art_shadow = true;
    config->draw_lyric_shadow = true;
    config->show_loading_screen = true;
    config->enlarge_active_line = true;
    config->op_mode = APP_MODE_KARAOKE;
    config->enable_dynamic_fill = true;
    config->enable_reading_hints = true;
    config->enable_pulse_effect = true;

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
