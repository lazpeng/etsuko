#include "config.h"

#include "constants.h"
#include "error.h"
#include "str_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static etsuko_Config_t *g_config = NULL;

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

EM_JS(const char *, get_song_param, (void), {
    const params = new URLSearchParams(window.location.search);
    const song = params.get("song") || "";
    return stringToNewUTF8(song);
})

static void try_load_config_web(etsuko_Config_t *config) {
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

static etsuko_Config_t *get_default_config(void) {
    etsuko_Config_t *config = malloc(sizeof(*config));
    if ( config == NULL ) {
        return NULL;
    }
    config->lyrics_font = strdup("NotoSans_ExtraCondensed-Bold.ttf");
    config->ui_font = strdup("NotoSans-Regular.ttf");
    config->song_file = strdup("stop crying your heart out.txt");
    // config->song_file = strdup("yoake.txt");

#ifdef __EMSCRIPTEN__
    try_load_config_web(config);
#endif

    return config;
}

etsuko_Config_t *config_get(void) {
    if ( g_config == NULL ) {
        g_config = get_default_config();
    }
    return g_config;
}
