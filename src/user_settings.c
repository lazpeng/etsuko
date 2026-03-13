#include "user_settings.h"

#include "error.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "str_utils.h"

static UserSettings_t *g_settings = NULL;

static const char *read_hints_to_string(const ReadHintSetting_t value) {
    return value == SET_READ_HINTS_HIDDEN ? "hidden" : "shown";
}

static const char *lyric_fill_to_string(const LyricFillSetting_t value) {
    switch ( value ) {
    case SET_LYRIC_FILL_ONLY: return "fill";
    case SET_LYRIC_FILL_DISABLED: return "disabled";
    case SET_LYRIC_FILL_WITH_PULSE:
    default: return "pulse";
    }
}

static const char *lyric_language_to_string(const LyricLanguageSetting_t value) {
    return value == SET_LYRIC_LANGUAGE_PREFER_TRANSLATED ? "translated" : "original";
}

static const char *auto_play_to_string(const AutoPlaySetting_t value) {
    return value == SET_AUTO_PLAY_ENABLED ? "enabled" : "disabled";
}

static void read_hints_from_string(UserSettings_t *settings, const char *value) {
    if ( str_equals_right_sized(value, "hidden") ) {
        settings->read_hints_visibility = SET_READ_HINTS_HIDDEN;
    } else if ( str_equals_right_sized(value, "shown") ) {
        settings->read_hints_visibility = SET_READ_HINTS_SHOWN;
    }
}

static void lyric_fill_from_string(UserSettings_t *settings, const char *value) {
    if ( str_equals_right_sized(value, "fill") ) {
        settings->lyric_fill = SET_LYRIC_FILL_ONLY;
    } else if ( str_equals_right_sized(value, "disabled") ) {
        settings->lyric_fill = SET_LYRIC_FILL_DISABLED;
    } else if ( str_equals_right_sized(value, "pulse") ) {
        settings->lyric_fill = SET_LYRIC_FILL_WITH_PULSE;
    }
}

static void lyric_language_from_string(UserSettings_t *settings, const char *value) {
    if ( str_equals_right_sized(value, "translated") ) {
        settings->lyric_language = SET_LYRIC_LANGUAGE_PREFER_TRANSLATED;
    } else if ( str_equals_right_sized(value, "original") ) {
        settings->lyric_language = SET_LYRIC_LANGUAGE_PREFER_ORIGINAL;
    }
}

static void auto_play_from_string(UserSettings_t *settings, const char *value) {
    if ( str_equals_right_sized(value, "enabled") ) {
        settings->auto_play = SET_AUTO_PLAY_ENABLED;
    } else if ( str_equals_right_sized(value, "disabled") ) {
        settings->auto_play = SET_AUTO_PLAY_DISABLED;
    }
}

static UserSettings_t *read_settings_from_json_string(const char *src) {
    if ( str_is_empty(src) )
        return NULL;

    JsonContext_t *ctx = json_ctx_init();
    JsonObject_t *root_obj = json_parse(src, ctx);
    if ( root_obj == NULL ) {
        json_ctx_destroy(ctx);
        return NULL;
    }

    UserSettings_t *settings = calloc(1, sizeof(*settings));
    if ( settings == NULL ) {
        json_obj_destroy(root_obj);
        json_ctx_destroy(ctx);
        return NULL;
    }

    const char *read_hints = json_get_string(json_obj_get(root_obj, "read_hints_visibility"));
    const char *lyric_fill = json_get_string(json_obj_get(root_obj, "lyric_fill"));
    const char *lyric_language = json_get_string(json_obj_get(root_obj, "lyric_language"));
    const char *auto_play = json_get_string(json_obj_get(root_obj, "auto_play"));

    if ( !str_is_empty(read_hints) )
        read_hints_from_string(settings, read_hints);
    if ( !str_is_empty(lyric_fill) )
        lyric_fill_from_string(settings, lyric_fill);
    if ( !str_is_empty(lyric_language) )
        lyric_language_from_string(settings, lyric_language);
    if ( !str_is_empty(auto_play) )
        auto_play_from_string(settings, auto_play);

    json_obj_destroy(root_obj);
    json_ctx_destroy(ctx);
    return settings;
}

static StrBuffer_t *settings_to_json_string(const UserSettings_t *settings) {
    if ( settings == NULL )
        return NULL;
    StrBuffer_t *buf = str_buf_init();
    bool first = true;
    json_buf_begin_object(buf, &first);
    json_buf_add_string(buf, &first, "read_hints_visibility", read_hints_to_string(settings->read_hints_visibility));
    json_buf_add_string(buf, &first, "lyric_fill", lyric_fill_to_string(settings->lyric_fill));
    json_buf_add_string(buf, &first, "lyric_language", lyric_language_to_string(settings->lyric_language));
    json_buf_add_string(buf, &first, "auto_play", auto_play_to_string(settings->auto_play));
    json_buf_end_object(buf);
    return buf;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

static UserSettings_t *read_settings_emscripten(void) {
    FILE *file = fopen("/persist/user.json", "rb");
    if ( file == NULL ) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    const long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if ( file_size <= 0 ) {
        fclose(file);
        return NULL;
    }

    char *data = malloc((size_t)file_size + 1);
    if ( data == NULL ) {
        fclose(file);
        return NULL;
    }
    fread(data, 1, (size_t)file_size, file);
    fclose(file);
    data[file_size] = '\0';

    UserSettings_t *settings = read_settings_from_json_string(data);
    free(data);
    return settings;
}

EM_JS(void, settings_start_sync, (void), {
    if (Module.etsukoSettingsSyncStarted) return;
    Module.etsukoSettingsSyncStarted = true;
    if (Module.etsukoSettingsSyncState === undefined) Module.etsukoSettingsSyncState = 0;
    try {
        var FS = Module['FS'];
        var IDBFS = Module['IDBFS'];
        if (!FS.analyzePath('/persist').exists) {
            FS.mkdir('/persist');
            FS.mount(IDBFS, {}, '/persist');
        }
        FS.syncfs(true, function(err) {
            Module.etsukoSettingsSyncState = err ? -1 : 1;
        });
    } catch (e) {
        Module.etsukoSettingsSyncState = -1;
    }
})

EM_JS(int, settings_sync_state, (void), {
    if (Module.etsukoSettingsSyncState === undefined) return 0;
    return Module.etsukoSettingsSyncState;
})
#else
static UserSettings_t *read_settings_from_user_json(void) {
    FILE *file = fopen("./user.json", "rb");
    if ( file == NULL ) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    const long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if ( file_size <= 0 ) {
        fclose(file);
        return NULL;
    }

    char *data = malloc((size_t)file_size + 1);
    if ( data == NULL ) {
        fclose(file);
        return NULL;
    }
    fread(data, 1, (size_t)file_size, file);
    fclose(file);
    data[file_size] = '\0';

    UserSettings_t *settings = read_settings_from_json_string(data);
    free(data);
    return settings;
}
#endif

UserSettings_t *settings_get(void) {
    if ( g_settings == NULL )
        error_abort("settings_get: Settings not initialized. Call settings_ensure_loaded and poll asynchronously until complete");
    return g_settings;
}

void save_current_settings(void) {
    const UserSettings_t *settings = settings_get();
    StrBuffer_t *buf = settings_to_json_string(settings);
    if ( buf == NULL )
        return;

#ifdef __EMSCRIPTEN__
    FILE *file = fopen("/persist/user.json", "wb");
#else
    FILE *file = fopen("./user.json", "wb");
#endif
    if ( file != NULL ) {
        fwrite(buf->data, 1, buf->len, file);
        fclose(file);
    }

#ifdef __EMSCRIPTEN__
    emscripten_run_script("FS.syncfs(false, function(err) { if (err) console.error(err); });");
#endif

    str_buf_destroy(buf);
}

// ReSharper disable once CppDFAConstantFunctionResult
bool settings_ensure_loaded(void) {
    if ( g_settings != NULL )
        return true;
#ifdef __EMSCRIPTEN__
    settings_start_sync();
    const int state = settings_sync_state();
    if ( state == 0 ) {
        return false;
    }
    if ( state < 0 ) {
        g_settings = calloc(1, sizeof(*g_settings));
        return true;
    }
    g_settings = read_settings_emscripten();
    if ( g_settings == NULL ) {
        g_settings = calloc(1, sizeof(*g_settings));
    }
    return true;
#else
    g_settings = read_settings_from_user_json();
    if ( g_settings == NULL ) {
        g_settings = calloc(1, sizeof(*g_settings));
    }
    return true;
#endif
}
