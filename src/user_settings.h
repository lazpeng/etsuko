/**
 * user_settings.h - Control and keep user-specific settings on how the application should behave
 * This is in contrast with the config.h definitions, that either keep track of the app state or define rigid rules for
 * things that should be possible or allowed.
 * In user settings, things that can be changed during runtime and can be saved for future use are kept instead
 */

#ifndef ETSUKO_USER_SETTINGS_H
#define ETSUKO_USER_SETTINGS_H

#include "ui.h"

typedef enum ReadHintSetting_t {
    SET_READ_HINTS_SHOWN = 0,
    SET_READ_HINTS_HIDDEN
} ReadHintSetting_t;

typedef enum LyricFillSetting_t {
    SET_LYRIC_FILL_WITH_PULSE = 0,
    SET_LYRIC_FILL_ONLY,
    SET_LYRIC_FILL_DISABLED
} LyricFillSetting_t;

typedef enum LyricLanguageSetting_t {
    SET_LYRIC_LANGUAGE_PREFER_ORIGINAL = 0,
    SET_LYRIC_LANGUAGE_PREFER_TRANSLATED,
} LyricLanguageSetting_t;

typedef enum AutoPlaySetting_t {
    SET_AUTO_PLAY_DISABLED = 0,
    SET_AUTO_PLAY_ENABLED
} AutoPlaySetting_t;

typedef struct UserSettings_t {
    ReadHintSetting_t read_hints_visibility;
    LyricFillSetting_t lyric_fill;
    LyricLanguageSetting_t lyric_language;
    AutoPlaySetting_t auto_play;
} UserSettings_t;

UserSettings_t *settings_get(void);
void save_current_settings(void);
bool settings_ensure_loaded(void);
void settings_show(Ui_t *ui);
void settings_on_frame_end(Ui_t *ui);

#endif // ETSUKO_USER_SETTINGS_H
