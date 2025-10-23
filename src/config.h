/**
 * config.h - Global settings for the application to know which stuff to load and use
 */

#ifndef ETSUKO_CONFIG_H
#define ETSUKO_CONFIG_H

typedef enum etsuko_Config_OpMode_t {
    APP_MODE_KARAOKE = 0,
} etsuko_Config_OpMode_t;

typedef struct {
    char *ui_font, *lyrics_font;
    char *song_file;
    etsuko_Config_OpMode_t op_mode;
} etsuko_Config_t;

etsuko_Config_t *config_get(void);

#endif // ETSUKO_CONFIG_H
