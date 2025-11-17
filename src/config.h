/**
 * config.h - Global settings for the application to know which stuff to load and use
 */

#ifndef ETSUKO_CONFIG_H
#define ETSUKO_CONFIG_H

#include <stdbool.h>

typedef enum Config_OpMode_t {
    APP_MODE_KARAOKE = 0,
} Config_OpMode_t;

typedef struct {
    char *ui_font, *lyrics_font;
    char *song_file;
    Config_OpMode_t op_mode;
    bool hide_past_lyrics;
} Config_t;

Config_t *config_get(void);

#endif // ETSUKO_CONFIG_H
