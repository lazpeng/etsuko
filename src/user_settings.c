#include "user_settings.h"

#include "error.h"
#include "ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "str_utils.h"

struct SettingsModal_t;

static UserSettings_t *g_settings = NULL;
static struct SettingsModal_t *g_modal = NULL;

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

typedef struct SettingsModal_t {
    Ui_t *ui;
    Container_t *container;
    bool should_close;
} SettingsModal_t;

static void on_close_settings(const UiEventOpts_t *, const Drawable_t *, void *) {
    g_modal->should_close = true;
}

void settings_show(Ui_t *ui) {
    if ( g_modal != NULL )
        error_abort("Settings modal opened more than once");
    g_modal = calloc(1, sizeof(*g_modal));
    g_modal->ui = ui;

    const Layout_t layout = {
        .flags = LAYOUT_CENTER | LAYOUT_PROPORTIONAL_SIZE,
        .width = 0.6,
        .height = 0.6,
        .z_index = 100,
        .absolute = true,
    };
    g_modal->container = ui_make_container(ui, ui_root_container(ui), &layout, CONTAINER_NONE);

    const Color_t bg_color = {.r = 30, .g = 30, .b = 30, .a = 240};
    const Color_t bg_color_secondary = {.r = 30, .g = 30, .b = 30, .a = 150};
    static Color_t colors[2] = {0};
    colors[0] = bg_color;
    colors[1] = bg_color_secondary;
    ui_container_update_background_colors_immediate(g_modal->container, colors, 2);
    g_modal->container->background->type = BACKGROUND_GRADIENT;
    g_modal->container->background->border_radius_em = 2.0;
    g_modal->container->background->blur = true;

    const Layout_t close_layout = {
        .flags = LAYOUT_PROPORTIONAL_POS | LAYOUT_ANCHOR_RIGHT_X | LAYOUT_WRAP_AROUND_X,
        .offset_x = -0.02,
        .offset_y = 0.02,
        .absolute = true,
    };
    const Drawable_TextData_t close_data = {
        .text = "X",
        .font_type = FONT_UI,
        .em = 1.0,
        .color = {.r = 255, .g = 255, .b = 255, .a = 255},
    };
    Drawable_t *close_btn = ui_make_text(ui, &close_data, g_modal->container, &close_layout);
    ui_add_event_callback(ui, UI_EVENT_MOUSE_CLICK, close_btn, on_close_settings, NULL);

    const Layout_t text_layout = {
        .flags = LAYOUT_CENTER,
        .absolute = true,
    };
    const Drawable_TextData_t text_data = {
        .text = "this is the settings screen",
        .font_type = FONT_UI,
        .em = 1.0,
        .color = {.r = 255, .g = 255, .b = 255, .a = 255},
    };
    ui_make_text(ui, &text_data, g_modal->container, &text_layout);

    const Layout_t toggle_layout = {
        .flags = LAYOUT_CENTER_X | LAYOUT_PROPORTIONAL_Y,
        .offset_y = 0.1,
    };
    const char *opts[] = {"test 1", "test 2", "test 3"};
    const ToggleWidgetOpts_t toggle_opts = {
        .opts = opts,
        .num_opts = sizeof (opts) / sizeof (const char *),
        .text_em = 1.0,
        .text_color = {.r=255,.g=255,.b=255,.a=255},
        .background_color = {.r=100,.g=100,.b=100,.a=70},
        .active_color = {.r=210,.g=210,.b=210,.a=90},
        .active_index = 1,
    };
    ui_build_toggle_widget(ui, g_modal->container, &toggle_layout, &toggle_opts);
}

void settings_on_frame_end(Ui_t *ui) {
    if ( g_modal == NULL )
        return;

    if ( g_modal->should_close ) {
        ui_destroy_container(ui, g_modal->container);
        free(g_modal);
        g_modal = NULL;
    }
}
