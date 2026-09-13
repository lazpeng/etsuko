#include "ui_ex.h"

#include "audio.h"
#include "config.h"
#include "error.h"
#include "events.h"
#include "song.h"
#include "str_utils.h"
#include "ui_widgets.h"
#include "user_settings.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// This is very messy, could be refactored into something at least consistent with the naming
#define LINE_SIZE_WITH_TIMINGS_EM (3.0)
#define LINE_SIZE_WITHOUT_TIMINGS_EM (1.5)
#define LINE_VERTICAL_PADDING (0.03)
#define LINE_VERTICAL_PADDING_WITH_READINGS (0.05)
#define LINE_FIRST_VERTICAL_OFFSET (0.35)
#define TEXT_LINE_PADDING_WITH_READINGS (1.0)
#define LINE_RIGHT_ALIGN_PADDING (-0.1)
#define LINE_FADE_MAX_DISTANCE (5)
#define LINE_SCALE_FACTOR_ACTIVE (1.0f)
#define LINE_SCALE_FACTOR_INACTIVE (0.75f)
#define ALPHA_DISTANCE_BASE_CALC (100)
#define ALPHA_DISTANCE_MIN_VALUE (25)
#define REGION_ANIMATION_DURATION (0.2)
#define LINE_SCALE_FACTOR_INACTIVE_DURATION (0.2)
#define SCALE_ANIMATION_DURATION (0.1)
#define FADE_ANIMATION_DURATION (1.0)
#define HINT_TOGGLE_FADE_ANIMATION_DURATION (0.5)
#define SCALE_ANIMATION_OUT_DURATION (0.3)
#define SCROLL_ANIMATION_DURATION (0.3)
#define SCALE_REGION_UP_DURATION (0.15)
#define SCALE_REGION_DOWN_MIN_DURATION (0.2)
#define SCALE_REGION_TARGET_SCALE (0.1)
#define FILL_ANIM_MIN_DURATION (0.2)
#define LINE_BLUR_FACTOR (1.f)
#define EMPHASIZE_EFFECT_Y_OFFSET_EM (0.3)
#define EMPHASIZE_EFFECT_X_OFFSET_EM (0)
#define EMPHASIZE_EFFECT_MIN_DURATION (0.1)
#define EMPHASIZE_EFFECT_MAX_DURATION (0.3)
#define TRANSLATION_ANIMATION_DURATION (0.5)
#define LINE_CASCADE_DELAY (0.05)
#define LINE_CASCADE_SUB_DURATION (0.75)
#define LINE_CASCADE_ADD_DURATION (0.1)
#define LINE_CASCADE_MAX_DISTANCE (4)

typedef struct LyricsState_t {
    int32_t current_active, first_active, anchor, num_lines;
} LyricsState_t;

typedef struct LyricLineWidget_t {
    WEAK Ui_t *ui;
    WEAK Container_t *parent;
    WEAK LyricsView_t *view;
    WEAK const Song_Line_t *song_line;
    OWNING Drawable_t *line;
    OWNING MAYBE_NULL Drawable_t *reading_hint;
    bool has_reading_hint;
    int entry_id;
    int32_t index;
    LineState_t state;
    uint32_t segment_visited[MAX_TIMINGS_PER_LINE];
} LyricLineWidget_t;

static bool read_hints_should_be_visible() {
    const bool enabled_in_config = config_get()->karaoke.enable_reading_hints;
    const bool enabled_in_settings = settings_get()->read_hints_visibility == SET_READ_HINTS_SHOWN;
    return enabled_in_config && enabled_in_settings;
}

static int32_t hint_target_alpha(const LyricLineWidget_t *widget) {
    if ( !read_hints_should_be_visible() )
        return 0;
    return widget->line->alpha_mod;
}

static bool is_hint_enabled(const LyricLineWidget_t *widget) {
    return widget->reading_hint != NULL && widget->reading_hint->enabled && widget->reading_hint->alpha_mod > 0;
}

static void apply_read_hint_visibility(const LyricLineWidget_t *widget) {
    if ( widget->reading_hint == NULL )
        return;
    widget->reading_hint->enabled = widget->line->enabled;
    const AnimatedSetOpts_t opts = {.duration = HINT_TOGGLE_FADE_ANIMATION_DURATION};
    ui_drawable_set_alpha_dur(widget->reading_hint, hint_target_alpha(widget), opts);
}

static Drawable_t *get_line_drawable_by_index(const LyricsView_t *view, const int32_t index) {
    assert(index >= 0);
    assert(index < (int64_t)view->selected_language->lyric_widgets->size);

    const LyricLineWidget_t *widget = view->selected_language->lyric_widgets->data[index];
    return widget->line;
}

static bool is_line_intermission(const LyricsView_t *view, const int32_t index) {
    if ( index < 0 || index >= (int32_t)view->selected_language->song_language->lines->size ) {
        return false;
    }
    const Song_Line_t *line = view->selected_language->song_language->lines->data[index];
    return str_is_empty(line->full_text) && line->base_duration > 5;
}

typedef enum CascadeDirection_t { CASCADE_AWAY = 0, CASCADE_TOWARDS } CascadeDirection_t;

static void reposition_line_drawable(const LyricsView_t *view, Drawable_t *drawable, const int32_t distance,
                                     const CascadeDirection_t direction) {
    double delay = 0;
    double duration = TRANSLATION_ANIMATION_DURATION;
    if ( !view->user_did_seek ) {
        if ( direction == CASCADE_TOWARDS ) {
            delay = LINE_CASCADE_DELAY;
            duration = duration + LINE_CASCADE_ADD_DURATION * (double)MIN(distance, LINE_CASCADE_MAX_DISTANCE);
        } else if ( direction == CASCADE_AWAY ) {
            duration = duration - LINE_CASCADE_SUB_DURATION * (double)MIN(__builtin_abs(distance), LINE_CASCADE_MAX_DISTANCE);
        }
    }
    const AnimatedSetOpts_t opts = {.delay = delay, .duration = duration, .interpolate_if_active = true};
    ui_reposition_drawable_dur(drawable, opts);
}

static void reposition_hint_for_line(const LyricsView_t *view, const int32_t index, const int32_t distance,
                                     const CascadeDirection_t direction) {
    const LyricLineWidget_t *widget = view->selected_language->lyric_widgets->data[index];
    if ( widget->reading_hint != NULL ) {
        reposition_line_drawable(view, widget->reading_hint, distance, direction);
    }
}

static void chain_line_below_drawable(Drawable_t *drawable, const Drawable_t *relative, const double offset_y) {
    drawable->layout.relative_to = relative;
    drawable->layout.offset_y = offset_y;
    drawable->layout.flags |= LAYOUT_RELATION_Y_INCLUDE_HEIGHT;
    drawable->layout.flags &= ~LAYOUT_ANCHOR_BOTTOM_Y;
}

static void chain_line_above_drawable(Drawable_t *drawable, const Drawable_t *relative, const double offset_y) {
    drawable->layout.relative_to = relative;
    drawable->layout.offset_y = offset_y;
    drawable->layout.flags |= LAYOUT_ANCHOR_BOTTOM_Y;
    drawable->layout.flags &= ~LAYOUT_RELATION_Y_INCLUDE_HEIGHT;
}

static void pin_line_drawable(Drawable_t *drawable, const Drawable_t *relative) {
    drawable->layout.relative_to = relative;
    drawable->layout.offset_x = 0;
    drawable->layout.offset_y = 0;
    drawable->layout.flags = LAYOUT_RELATIVE_TO_POS | LAYOUT_PROPORTIONAL_Y;
}

static void chain_line_below(const LyricLineWidget_t *widget, const Drawable_t *relative, const double offset_y) {
    if ( widget->reading_hint != NULL )
        pin_line_drawable(widget->reading_hint, widget->line);
    chain_line_below_drawable(widget->line, relative, offset_y);
}

static void chain_line_under_previous(const LyricsView_t *view, const int32_t index, LyricsState_t *state) {
    const LyricLineWidget_t *widget = view->selected_language->lyric_widgets->data[index];
    if ( state->anchor >= 0 ) {
        const LyricLineWidget_t *target = view->selected_language->lyric_widgets->data[index - 1];
        const Drawable_t *relative = is_hint_enabled(target) ? target->reading_hint : target->line;
        chain_line_below(widget, relative, LINE_VERTICAL_PADDING);
    } else {
        chain_line_below(widget, view->selected_language->lyric_anchor, 0);
        state->anchor = index;
    }
}

static void chain_line_above(const LyricsView_t *view, const Drawable_t *relative, const int32_t index, const int32_t distance) {
    const LyricLineWidget_t *widget = view->selected_language->lyric_widgets->data[index];
    if ( is_hint_enabled(widget) ) {
        chain_line_above_drawable(widget->reading_hint, relative, -LINE_VERTICAL_PADDING);
        pin_line_drawable(widget->line, widget->reading_hint);

        reposition_hint_for_line(view, index, distance, CASCADE_AWAY);
        reposition_line_drawable(view, widget->line, distance, CASCADE_AWAY);
    } else {
        chain_line_above_drawable(widget->line, relative, -LINE_VERTICAL_PADDING);
        if ( widget->reading_hint != NULL )
            pin_line_drawable(widget->reading_hint, widget->line);

        reposition_line_drawable(view, widget->line, distance, CASCADE_AWAY);
        reposition_hint_for_line(view, index, distance, CASCADE_AWAY);
    }
}

static void scale_hint_for_line(const LyricsView_t *view, const int32_t index) {
    const LyricLineWidget_t *widget = view->selected_language->lyric_widgets->data[index];
    if ( widget->reading_hint != NULL ) {
        const Drawable_t *drawable = widget->line;
        Drawable_t *hint = widget->reading_hint;
        ui_drawable_set_scale_factor(hint, 1.f + (float)drawable->bounds.scale_mod);
    }
}

static void fade_hint_for_line(const LyricsView_t *view, const int32_t index) {
    const LyricLineWidget_t *widget = view->selected_language->lyric_widgets->data[index];
    if ( widget->reading_hint != NULL ) {
        const AnimatedSetOpts_t opts = {.duration = HINT_TOGGLE_FADE_ANIMATION_DURATION};
        ui_drawable_set_alpha_dur(widget->reading_hint, hint_target_alpha(widget), opts);
    }
}

static void blur_hint_for_line(const LyricsView_t *view, const int32_t index) {
    const LyricLineWidget_t *widget = view->selected_language->lyric_widgets->data[index];
    if ( widget->reading_hint != NULL ) {
        const Drawable_t *drawable = widget->line;
        Drawable_t *hint = widget->reading_hint;
        hint->blur_radius = drawable->blur_radius;
    }
}

typedef struct ReadingEntry_t {
    Texture_t *texture;
    double x, y;
} ReadingEntry_t;

static void make_reading_hint(LyricLineWidget_t *widget) {
    const Drawable_TextData_t *lyric_data = widget->line->custom_data;
    assert(lyric_data->line_offsets != NULL);

    const bool place_hints_under_segment = config_get()->karaoke.position_hints_under_segment;
    const double hint_padding = widget->parent->bounds.w * 0.005;
    const int pixels = render_measure_pixels_from_em(0.8);
    const Color_t white = {.r = 255, .g = 255, .b = 255, .a = 255};

    const Song_Line_t *line = widget->song_line;

    Vector_t *entries = vec_init();
    double max_w = 0, max_h = 0;

    size_t read_i = 0;
    for ( size_t off_i = 0; off_i < lyric_data->line_offsets->size; off_i++ ) {
        const TextOffsetInfo_t *offset_info = lyric_data->line_offsets->data[off_i];
        const double y = offset_info->start_y + offset_info->height;

        double x = 0;
        for ( ; read_i < line->readings->size; read_i++ ) {
            const Song_LineReading_t *reading = line->readings->data[read_i];
            if ( (int32_t)reading->start_ch_idx >= offset_info->start_char_idx + offset_info->num_chars )
                break; // It's on the next line

            const int32_t index_on_this_line = reading->start_ch_idx > (size_t)offset_info->start_char_idx
                                                   ? (int32_t)(reading->start_ch_idx - (size_t)offset_info->start_char_idx)
                                                   : 0;
            const CharOffsetInfo_t *character = offset_info->char_offsets->data[index_on_this_line];
            const double char_padding = character->width * 0.1;
            const double character_x = offset_info->start_x + char_padding + character->x;

            // Place this hint below the segment it's supposed to hint at, but if the previous hint already
            // overshoots the length of its segment, place it a few pixels to the right of wherever the last hint ended

            const double x_padded = x + hint_padding;
            if ( place_hints_under_segment )
                x = MAX(x_padded, character_x);
            else
                x = x_padded;

            ReadingEntry_t *entry = calloc(1, sizeof(*entry));
            entry->texture = render_make_text(reading->reading_text, pixels, &white, FONT_UI);
            entry->x = x;
            entry->y = y;
            vec_add(entries, entry);

            max_w = MAX(max_w, x + entry->texture->width);
            max_h = MAX(max_h, y + entry->texture->height);
            x += entry->texture->width;
        }
    }

    if ( widget->reading_hint == NULL ) {
        widget->reading_hint = ui_make_custom(widget->ui, widget->parent,
                                              &(Layout_t){.offset_x = 0,
                                                          .offset_y = 0,
                                                          .flags = LAYOUT_RELATIVE_TO_POS | LAYOUT_PROPORTIONAL_Y,
                                                          .relative_to = widget->line});
        ui_animate_translation(widget->reading_hint, &(Animation_EaseTranslationData_t){
                                                         .duration = TRANSLATION_ANIMATION_DURATION,
                                                         .ease_func = ANIM_EASE_OUT_CUBIC,
                                                     });
        ui_animate_fade(widget->reading_hint,
                        &(Animation_FadeInOutData_t){.duration = FADE_ANIMATION_DURATION, .ease_func = ANIM_EASE_OUT_CUBIC});
        ui_animate_scale(widget->reading_hint, &(Animation_ScaleData_t){.duration = SCALE_ANIMATION_DURATION});
        ui_animate_blur(widget->reading_hint,
                        &(Animation_BlurRadiusData_t){.duration = FADE_ANIMATION_DURATION, .ease_func = ANIM_EASE_OUT_CUBIC});
        ui_drawable_set_alpha_immediate(widget->reading_hint, hint_target_alpha(widget));
    } else if ( widget->reading_hint->texture != NULL ) {
        render_destroy_texture(widget->reading_hint->texture);
        widget->reading_hint->texture = NULL;
    }

    if ( entries->size > 0 ) {
        RenderTarget_t *render_target = render_make_render_target((int32_t)ceil(max_w), (int32_t)ceil(max_h));
        render_target_bind(render_target);
        const BlendMode_t blend_mode = render_get_blend_mode();
        render_set_blend_mode(BLEND_MODE_NONE);

        for ( size_t li = 0; li < entries->size; li++ ) {
            const ReadingEntry_t *entry = entries->data[li];
            const Bounds_t bounds = {.x = entry->x, .y = entry->y, .w = entry->texture->width, .h = entry->texture->height};
            const DrawTextureOpts_t opts = {.alpha_mod = 255, .color_mod = 1.f};
            render_draw_texture(entry->texture, &bounds, &opts);
        }

        render_target_unbind(render_target);
        widget->reading_hint->texture = render_target_detach_texture(render_target);
        render_destroy_render_target(render_target);
        render_set_blend_mode(blend_mode);
    } else {
        widget->reading_hint->texture = render_make_null();
    }

    for ( size_t li = 0; li < entries->size; li++ ) {
        ReadingEntry_t *entry = entries->data[li];
        render_destroy_texture(entry->texture);
        free(entry);
    }
    vec_destroy(entries);

    ui_reposition_drawable_immediate(widget->reading_hint);

    widget->reading_hint->pending_recompute = false;
    widget->reading_hint->bounds.w = widget->reading_hint->texture->width;
    widget->reading_hint->bounds.h = widget->reading_hint->texture->height;
    apply_read_hint_visibility(widget);
}

static void lyric_line_widget_reconfigure(void *widget_data) {
    LyricLineWidget_t *widget = widget_data;
    if ( !widget->has_reading_hint )
        return;
    if ( widget->reading_hint != NULL && !widget->reading_hint->pending_recompute )
        return;

    make_reading_hint(widget);
}

static void lyric_line_widget_destroy(Ui_t *ui, void *widget_data) {
    LyricLineWidget_t *widget = widget_data;

    if ( widget->reading_hint != NULL )
        ui_destroy_drawable(ui, widget->reading_hint);
    ui_destroy_drawable(ui, widget->line);

    ui_unregister_widget(widget->parent, widget->entry_id);
    free(widget);
}

typedef struct LyricLineWidgetOpts_t {
    WEAK Container_t *parent_container;
    WEAK LyricsView_t *view;
    WEAK Drawable_t *line_drawable;
    WEAK const Song_Line_t *song_line;
    int32_t index;
    bool generate_reading_hints;
} LyricLineWidgetOpts_t;

static LyricLineWidget_t *make_line_widget(Ui_t *ui, const LyricLineWidgetOpts_t *opts) {
    LyricLineWidget_t *widget = calloc(1, sizeof(*widget));
    widget->ui = ui;
    widget->parent = opts->parent_container;
    widget->view = opts->view;
    widget->song_line = opts->song_line;
    widget->index = opts->index;
    widget->line = opts->line_drawable;
    widget->reading_hint = NULL;
    widget->has_reading_hint = opts->generate_reading_hints && opts->song_line->readings->size > 0;
    widget->state = LINE_NONE;

    if ( widget->has_reading_hint )
        make_reading_hint(widget);

    widget->entry_id =
        ui_register_widget(opts->parent_container, lyric_line_widget_reconfigure, lyric_line_widget_destroy, widget);

    return widget;
}

static float get_inactive_line_scale() {
    return config_get()->karaoke.enlarge_active_line ? LINE_SCALE_FACTOR_INACTIVE : LINE_SCALE_FACTOR_ACTIVE;
}

static int32_t calculate_alpha(const int32_t distance) {
    const int32_t dec = ALPHA_DISTANCE_BASE_CALC / LINE_FADE_MAX_DISTANCE * MIN(distance, LINE_FADE_MAX_DISTANCE);
    return MAX(ALPHA_DISTANCE_MIN_VALUE, ALPHA_DISTANCE_BASE_CALC - dec);
}

static float calculate_blur(const int32_t distance) {
    const bool enabled_in_config = config_get()->karaoke.blur_lyrics;
    const bool enabled_in_settings = settings_get()->blur_lyrics;
    if ( !enabled_in_config || !enabled_in_settings )
        return 0.f;
    return LINE_BLUR_FACTOR * MIN(3, (1.f + (float)distance));
}

// this function name sounds like a concert that is broadcasted over the internet
static void on_line_event(const UiEventOpts_t *opts, Drawable_t *, void *custom_data) {
    const LyricLineWidget_t *widget = custom_data;
    LyricsView_t *view = widget->view;

    if ( opts->event == UI_EVENT_MOUSE_HOVER_ENTERED ) {
        view->current_hovered_index = widget->index;
    } else if ( opts->event == UI_EVENT_MOUSE_HOVER_EXITED ) {
        view->current_hovered_index = -1;
    } else if ( opts->event == UI_EVENT_MOUSE_CLICK ) {
        audio_seek(widget->song_line->base_start_time);
    }
}

static void on_key_pressed(const UiEventOpts_t *opt, Drawable_t *, void *) {
    if ( opt->keyboard.key == KEY_R ) {
        UserSettings_t *settings = settings_get();
        settings->read_hints_visibility =
            settings->read_hints_visibility == SET_READ_HINTS_SHOWN ? SET_READ_HINTS_HIDDEN : SET_READ_HINTS_SHOWN;
    }
}

static LyricsLanguage_t *make_lyrics_language(Ui_t *ui, LyricsView_t *view, Song_Language_t *language) {
    if ( language->lines->size == 0 ) {
        error_abort("Song has no lyrics");
    }

    LyricsLanguage_t *result = calloc(1, sizeof(*result));
    result->song_language = language;
    result->lyric_widgets = vec_init();
    result->language_str = strdup(language->language);

    vec_add(view->lyrics_languages, result);
    ui_ex_lyrics_view_set_language(view, result->language_str);

    const bool should_generate_reading_hints = language->has_reading_info && config_get()->karaoke.enable_reading_hints;
    const Color_t color = {.r = 255, .b = 255, .g = 255, .a = 255};

    DrawableAlignment_t base_alignment = ALIGN_LEFT;
    LayoutFlags_t base_alignment_flags = LAYOUT_NONE;
    double base_offset_x = 0;
    if ( view->song->line_alignment == SONG_LINE_CENTER ) {
        base_alignment = ALIGN_CENTER;
        base_alignment_flags = LAYOUT_CENTER_X;
    } else if ( view->song->line_alignment == SONG_LINE_RIGHT ) {
        base_alignment = ALIGN_RIGHT;
        base_alignment_flags = LAYOUT_ANCHOR_RIGHT_X | LAYOUT_WRAP_AROUND_X | LAYOUT_PROPORTIONAL_X;
        base_offset_x = LINE_RIGHT_ALIGN_PADDING;
    }

    const Layout_t anchor_layout = {
        .offset_y = LINE_FIRST_VERTICAL_OFFSET,
        .offset_x = base_offset_x,
        .flags = base_alignment_flags | LAYOUT_PROPORTIONAL_Y,
    };
    result->lyric_anchor = ui_make_custom(ui, view->container, &anchor_layout);

    Drawable_t *prev = NULL;
    for ( size_t i = 0; i < language->lines->size; i++ ) {
        const Song_Line_t *line = language->lines->data[i];

        char *line_text = line->full_text;
        if ( line_text == NULL ) {
            printf("Warn: line was not initialized properly. idx: %lu\n", i);
            continue;
        }

        if ( strncmp(line_text, "", 1) == 0 ) {
            if ( is_line_intermission(view, (int32_t)i) ) {
                line_text = "...";
            } else {
                line_text = " ";
            }
        }

        DrawableAlignment_t alignment = base_alignment;
        LayoutFlags_t alignment_flags = base_alignment_flags;
        double offset_x = base_offset_x;
        // line override
        if ( line->alignment != view->song->line_alignment ) {
            if ( line->alignment == SONG_LINE_CENTER ) {
                alignment = ALIGN_CENTER;
                alignment_flags = LAYOUT_CENTER_X;
                offset_x = 0;
            } else if ( line->alignment == SONG_LINE_RIGHT ) {
                alignment = ALIGN_RIGHT;
                alignment_flags = LAYOUT_ANCHOR_RIGHT_X | LAYOUT_PROPORTIONAL_X | LAYOUT_WRAP_AROUND_X;
                offset_x = LINE_RIGHT_ALIGN_PADDING;
            } else if ( line->alignment == SONG_LINE_LEFT ) {
                alignment = ALIGN_LEFT;
                alignment_flags = LAYOUT_NONE;
                offset_x = 0;
            }
        }

        const double lyrics_em = language->has_timings ? LINE_SIZE_WITH_TIMINGS_EM : LINE_SIZE_WITHOUT_TIMINGS_EM;
        const double line_padding = should_generate_reading_hints ? TEXT_LINE_PADDING_WITH_READINGS : 0;
        Drawable_TextData_t data = {.text = line_text,
                                    .font_type = FONT_LYRICS,
                                    .em = lyrics_em,
                                    .wrap_enabled = true,
                                    .wrap_width_threshold = 0.85,
                                    .color = color,
                                    .line_padding_em = line_padding,
                                    .alignment = alignment,
                                    .draw_shadow = config_get()->karaoke.draw_lyric_shadow,
                                    .compute_offsets = language->has_sub_timings || language->has_reading_info};
        const Layout_t layout = {
            .offset_y = prev == NULL ? 0 : LINE_VERTICAL_PADDING,
            .offset_x = offset_x,
            .flags = alignment_flags | LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y,
            .relative_to = prev == NULL ? result->lyric_anchor : prev,
        };
        prev = ui_make_text(ui, &data, view->container, &layout);
        ui_drawable_set_alpha_immediate(prev, calculate_alpha(LINE_FADE_MAX_DISTANCE));

        if ( language->has_timings ) {
            Animation_EaseTranslationData_t translation_data = {.duration = TRANSLATION_ANIMATION_DURATION,
                                                                .ease_func = ANIM_EASE_OUT_CUBIC};
            ui_animate_translation(prev, &translation_data);

            Animation_FadeInOutData_t fade_data = {.duration = FADE_ANIMATION_DURATION, .ease_func = ANIM_EASE_OUT_CUBIC};
            ui_animate_fade(prev, &fade_data);

            Animation_BlurRadiusData_t blur_data = {.duration = FADE_ANIMATION_DURATION, .ease_func = ANIM_EASE_OUT_CUBIC};
            ui_animate_blur(prev, &blur_data);

            Animation_ScaleData_t scale_data = {.duration = SCALE_ANIMATION_DURATION};
            ui_animate_scale(prev, &scale_data);

            Animation_DrawRegionData_t draw_region_data = {.duration = REGION_ANIMATION_DURATION, .ease_func = ANIM_EASE_NONE};
            ui_animate_draw_region(prev, &draw_region_data);

            Animation_ScaleRegionData_t scale_region_data = {.duration = SCALE_REGION_UP_DURATION,
                                                             .default_apply = ANIM_APPLY_CONCURRENT};
            ui_animate_scale_region(prev, &scale_region_data);
        } else {
            ui_drawable_set_alpha_immediate(prev, calculate_alpha(0));
        }

        // The widget builds its own reading hint and registers itself for recomputes and cleanup
        const LyricLineWidgetOpts_t opts = {.parent_container = view->container,
                                            .view = view,
                                            .line_drawable = prev,
                                            .song_line = line,
                                            .index = (int32_t)result->lyric_widgets->size,
                                            .generate_reading_hints = should_generate_reading_hints};
        LyricLineWidget_t *widget = make_line_widget(ui, &opts);
        vec_add(result->lyric_widgets, widget);

        if ( language->has_timings ) {
            ui_add_event_callback(ui, UI_EVENT_MOUSE_HOVER_ENTERED, prev, on_line_event, widget);
            ui_add_event_callback(ui, UI_EVENT_MOUSE_HOVER_EXITED, prev, on_line_event, widget);
            ui_add_event_callback(ui, UI_EVENT_MOUSE_CLICK, prev, on_line_event, widget);
        }
    }

    if ( !str_is_empty(view->song->credits) && prev != NULL ) {
        result->credit_separator =
            ui_make_rectangle(ui, &(Drawable_RectangleData_t){.color = {.r = 200, .g = 200, .b = 200, .a = 150}}, view->container,
                              &(Layout_t){.offset_y = 0.02 + LINE_VERTICAL_PADDING,
                                          .offset_x = 0,
                                          .width = 0.8,
                                          .height = 1,
                                          .flags = LAYOUT_PROPORTIONAL_W | LAYOUT_RELATIVE_TO_Y |
                                                   LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y,
                                          .relative_to = prev});
        ui_animate_translation(result->credit_separator,
                               &(Animation_EaseTranslationData_t){.duration = 0.3, .ease_func = ANIM_EASE_OUT_CUBIC});

        result->credits_prefix =
            ui_make_text(ui,
                         &(Drawable_TextData_t){.text = "Written by: ",
                                                .draw_shadow = true,
                                                .em = 0.8,
                                                .font_type = FONT_LYRICS,
                                                .alignment = ALIGN_LEFT,
                                                .color = {.r = 200, .g = 200, .b = 200, .a = 255}},
                         view->container,
                         &(Layout_t){.offset_y = 0.01,
                                     .flags = LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y,
                                     .relative_to = result->credit_separator});
        ui_drawable_set_alpha_immediate(result->credits_prefix, 150);
        ui_animate_translation(result->credits_prefix,
                               &(Animation_EaseTranslationData_t){.duration = 0.3, .ease_func = ANIM_EASE_OUT_CUBIC});
        ui_animate_blur(result->credits_prefix, &(Animation_BlurRadiusData_t){.duration = 0.3});

        result->credits_content =
            ui_make_text(ui,
                         &(Drawable_TextData_t){.text = view->song->credits,
                                                .draw_shadow = true,
                                                .em = 0.8,
                                                .font_type = FONT_UI,
                                                .alignment = ALIGN_LEFT,
                                                .color = {.r = 200, .g = 200, .b = 200, .a = 255}},
                         view->container,
                         &(Layout_t){.offset_y = 0,
                                     .offset_x = 0.001,
                                     .flags = LAYOUT_RELATIVE_TO_POS | LAYOUT_RELATION_X_INCLUDE_WIDTH | LAYOUT_PROPORTIONAL_POS,
                                     .relative_to = result->credits_prefix});
        ui_drawable_set_alpha_immediate(result->credits_content, 200);
        ui_animate_translation(result->credits_content,
                               &(Animation_EaseTranslationData_t){.duration = 0.3, .ease_func = ANIM_EASE_OUT_CUBIC});
        ui_animate_blur(result->credits_content, &(Animation_BlurRadiusData_t){.duration = 0.3});
    }

    return result;
}

static void set_default_language(LyricsView_t *view) {
    const Song_Language_t *language = NULL;
    const LyricLanguageSetting_t preference = settings_get()->lyric_language;
    for ( size_t i = 0; i < view->song->languages->size; i++ ) {
        const Song_Language_t *current = view->song->languages->data[i];
        if ( current->is_default && preference == SET_LYRIC_LANGUAGE_PREFER_ORIGINAL ) {
            language = current;
            break;
        }
        // Set the first translated option as the target
        if ( current->is_default == false && preference == SET_LYRIC_LANGUAGE_PREFER_TRANSLATED ) {
            language = current;
            break;
        }
    }

    if ( language == NULL ) {
        printf("Warning: Unable to set default language based on settings.\n");
        if ( view->song->languages->size == 0 )
            error_abort("No languages set in song");
        language = view->song->languages->data[0];
    }

    ui_ex_lyrics_view_set_language(view, language->language);
}

LyricsView_t *ui_ex_make_lyrics_view(Ui_t *ui, Container_t *parent, const Song_t *song) {
    if ( parent == NULL ) {
        error_abort("Parent container is NULL");
    }

    if ( song == NULL ) {
        error_abort("Song is NULL");
    }

    LyricsView_t *view = calloc(1, sizeof(*view));
    view->container = parent;
    view->song = song;
    view->lyrics_languages = vec_init();
    view->current_hovered_index = -1;
    view->saved_lyric_effect_setting = settings_get()->lyric_effect;
    view->saved_lyric_fill_setting = settings_get()->lyric_fill;

    // Setup container for scrolling
    view->container->overflow_y = (ContainerOverflow_t){.kind = OVERFLOW_SCROLL, .relative_end_padding = 0.6};
    ui_container_add_vertical_scrollbar(ui, view->container, SCROLL_BAR_AUTO_HIDE);
    ui_container_animate_scroll_y(view->container, 0.1, ANIM_EASE_OUT_CUBIC);

    ui_add_global_event_callback(ui, UI_EVENT_KEY_PRESSED, on_key_pressed, view);
    for ( size_t i = 0; i < song->languages->size; i++ ) {
        Song_Language_t *language = song->languages->data[i];
        make_lyrics_language(ui, view, language);
    }
    set_default_language(view);

    return view;
}

static int32_t calculate_distance(const LyricsView_t *view, const int32_t index, const int32_t prev_active) {
    int32_t distance = abs(index - prev_active);
    if ( distance == 1 ) {
        return 1;
    }
    if ( distance > 0 ) {
        int32_t start = prev_active, end = index;
        if ( index < prev_active ) {
            start = index;
            end = prev_active;
        }
        for ( int32_t i = start; i < end; i++ ) {
            const Song_Line_t *line = view->selected_language->song_language->lines->data[i];
            if ( str_is_empty(line->full_text) ) {
                distance -= 1;
            }
        }
    }

    return MAX(1, distance);
}

static void calculate_sub_region_for_active_line(const LyricsView_t *view, LyricLineWidget_t *widget, const Song_t *song,
                                                 const Song_Line_t *line, const bool lyric_settings_changed) {
    // A slight variation that highlights the entire portion of the segment
    // Mainly intended when the timing is done per-syllable
    Drawable_t *drawable = widget->line;
    const Drawable_TextData_t *text_data = drawable->custom_data;

    DrawRegionOptSet_t draw_regions = {0};
    draw_regions.num_regions = (int32_t)text_data->line_offsets->size;

    const bool pulse_effect = settings_get()->lyric_effect == SET_LYRIC_EFFECT_PULSE;
    const bool emphasize_effect = settings_get()->lyric_effect == SET_LYRIC_EFFECT_EMPHASIZE;
    const double emphasize_offset_y = render_measure_pixels_from_em(EMPHASIZE_EFFECT_Y_OFFSET_EM);
    const double emphasize_offset_x = render_measure_pixels_from_em(EMPHASIZE_EFFECT_X_OFFSET_EM);

    double last_segment_remaining = 0.0;
    const double settings_time_offset = settings_get()->global_audio_offset_ms / 1000.0;
    const double audio_elapsed = audio_elapsed_time() + song->time_offset + settings_time_offset;
    int32_t timing_offset_start = 0;

    // Check for any visited segments that are now in the future (e.g. user seeked backwards)
    for ( int32_t s = 0; s < line->num_timings; s++ ) {
        if ( widget->segment_visited[s] ) {
            const Song_LineTiming_t *timing = &line->timings[s];
            const double start_time = line->base_start_time + timing->cumulative_duration;
            if ( audio_elapsed < start_time || lyric_settings_changed ) {
                widget->segment_visited[s] = 0;
            }
        }
    }

    bool is_only_punctuation = false;
    // Calculate how much of each line we need to show
    for ( size_t i = 0; i < text_data->line_offsets->size; i++ ) {
        const TextOffsetInfo_t *offset_info = text_data->line_offsets->data[i];

        const float y0 = (float)(offset_info->start_y / drawable->bounds.h);
        const float y1 = y0 + (float)(offset_info->height / drawable->bounds.h);
        // Compensate for alignment
        float x1 = (float)(offset_info->start_x / drawable->bounds.w);
        for ( int32_t s = timing_offset_start; s < line->num_timings; s++ ) {
            const Song_LineTiming_t *timing = &line->timings[s];
            if ( timing->start_char_idx > offset_info->start_char_idx + offset_info->num_chars )
                break;

            if ( timing->end_char_idx <= offset_info->start_char_idx )
                continue;

            const int timing_end_idx = MIN(timing->end_char_idx, offset_info->start_char_idx + offset_info->num_chars);
            const int timing_start_idx = MAX(timing->start_char_idx, offset_info->start_char_idx);
            const int segment_length_in_current_line = timing_end_idx - timing_start_idx;
            if ( segment_length_in_current_line <= 0 )
                continue;

            // If this segment started on the previous line, calculate a time per character and add a delay equivalent to the
            // characters left on the previous line so the animation looks correct
            double delay = 0.0;
            const int32_t segment_length = timing->end_char_idx - timing->start_char_idx;
            const double duration_per_character = timing->duration / segment_length;
            if ( timing_start_idx != timing->start_char_idx ) {
                delay = duration_per_character * (timing_start_idx - timing->start_char_idx);
            }

            const double elapsed_since_segment = audio_elapsed - delay - (line->base_start_time + timing->cumulative_duration);
            if ( elapsed_since_segment <= 0.0 )
                break;

            timing_offset_start = s;

            // here we calculate each letter boundary and always set the fill size to that
            // for the whole duration of the segment
            double segment_width = 0.0;
            const int32_t segment_start_in_line = MAX(0, timing->start_char_idx - offset_info->start_char_idx);
            for ( int32_t ci = 0; ci < segment_length_in_current_line; ci++ ) {
                const size_t index = ci + segment_start_in_line;
                const CharOffsetInfo_t *char_info = offset_info->char_offsets->data[index];
                segment_width += char_info->width;
            }

            const double segment_fill_contribution = segment_width / drawable->bounds.w;

            double duration = timing->duration;
            // Compensate the timing if the line doesn't fit completely in this line
            if ( segment_length_in_current_line != segment_length ) {
                duration = duration_per_character * segment_length_in_current_line;
            }

            const bool segment_visited = widget->segment_visited[s] & (1u << i);
            is_only_punctuation = timing->is_only_punctuation;

            const bool pulse_enabled_in_config = config_get()->karaoke.enable_pulse_effect;
            const bool pulse_enabled_in_settings = settings_get()->lyric_fill == SET_LYRIC_FILL_WITH_EFFECT;
            const bool should_show_effect = pulse_enabled_in_settings && pulse_enabled_in_config;
            if ( !segment_visited && should_show_effect ) {
                if ( pulse_effect && !is_only_punctuation ) {
                    ScaleRegionOpt_t region = {
                        .x0_perc = x1,
                        .x1_perc = x1 + (float)segment_fill_contribution,
                        .y0_perc = y0,
                        .y1_perc = y1,
                        .from_scale = 0.f,
                        .to_scale = SCALE_REGION_TARGET_SCALE,
                    };
                    static AnimatedSetOpts_t up_anim_opts = {.duration = SCALE_REGION_UP_DURATION,
                                                             .apply_type = ANIM_APPLY_DEFAULT};
                    ui_drawable_add_scale_region_dur(drawable, &region, up_anim_opts);
                    // Then do another scale anim for scaling back down for the duration of the segment (minus the up duration)
                    region.from_scale = SCALE_REGION_TARGET_SCALE;
                    region.to_scale = 0.f;
                    // That runs after the current one finishes
                    // This works because it will be applied sequentially to the last animation on the exection queue, which is
                    // guaranteed to be the one above because it's set to run simultaneously (so it is added to the queue no
                    // matter what) and the application is single threaded, so no other code could be pushing animations to the
                    // queue between the call to ui_drawable_add_scale_region_dur and the line below
                    const double down_duration = MAX(duration - SCALE_REGION_UP_DURATION, SCALE_REGION_DOWN_MIN_DURATION);
                    const AnimatedSetOpts_t down_anim_opts = {.duration = down_duration, .apply_type = ANIM_APPLY_SEQUENTIAL};
                    ui_drawable_add_scale_region_dur(drawable, &region, down_anim_opts);
                } else if ( emphasize_effect ) {
                    ScaleRegionOpt_t region = {
                        .x0_perc = x1,
                        .x1_perc = x1 + (float)segment_fill_contribution,
                        .y0_perc = y0,
                        .y1_perc = y1,
                        .pos_y_offset = -emphasize_offset_y,
                        .pos_x_offset = -emphasize_offset_x,
                    };
                    const double final_duration =
                        MIN(MAX(duration, EMPHASIZE_EFFECT_MIN_DURATION), EMPHASIZE_EFFECT_MAX_DURATION);
                    const AnimatedSetOpts_t up_anim_opts = {
                        .duration = final_duration, .apply_type = ANIM_APPLY_STICKY, .unique_id = timing->start_char_idx};
                    ui_drawable_add_scale_region_dur(drawable, &region, up_anim_opts);
                }

                widget->segment_visited[s] |= (1u << i);
            }

            x1 += (float)segment_fill_contribution;
            last_segment_remaining = duration - elapsed_since_segment;
        }
        draw_regions.regions[i].x1_perc = MIN(1.f, x1);

        // x0 is always at the beginning
        draw_regions.regions[i].x0_perc = 0.f;
        // y0 is the beginning of this line
        draw_regions.regions[i].y0_perc = y0;
        // y1 is the end of this line
        draw_regions.regions[i].y1_perc = y1;
    }
    const double min_duration = is_only_punctuation ? last_segment_remaining : FILL_ANIM_MIN_DURATION;
    const double fill_duration = MAX(min_duration, last_segment_remaining);
    ui_drawable_set_draw_region_dur(drawable, &draw_regions, (AnimatedSetOpts_t){.duration = fill_duration});
}

static void set_line_active(const LyricsView_t *view, const int32_t index, LyricsState_t *state) {
    LyricLineWidget_t *widget = view->selected_language->lyric_widgets->data[index];
    Drawable_t *drawable = widget->line;

    drawable->enabled = true;
    ui_drawable_set_blur_radius_immediate(drawable, 0.f);
    ui_drawable_set_alpha_immediate(drawable, 0xFF);

    ui_drawable_set_scale_factor(drawable, LINE_SCALE_FACTOR_ACTIVE);
    scale_hint_for_line(view, index);
    fade_hint_for_line(view, index);
    blur_hint_for_line(view, index);

    const Song_Line_t *line = view->selected_language->song_language->lines->data[index];

    chain_line_under_previous(view, index, state);
    if ( state->first_active < 0 )
        state->first_active = index;
    state->current_active = index;

    reposition_line_drawable(view, drawable, 0, CASCADE_TOWARDS);
    reposition_hint_for_line(view, index, 0, CASCADE_TOWARDS);

    const LineState_t new_state = LINE_ACTIVE;
    if ( widget->state != new_state ) {
        widget->state = new_state;

        // Clear visited for the current line
        for ( int i = 0; i < MAX_TIMINGS_PER_LINE; i++ ) {
            widget->segment_visited[i] = 0;
        }

        if ( view->selected_language->song_language->has_sub_timings && line->num_timings > 0 ) {
            ui_drawable_set_draw_underlay(drawable, true, calculate_alpha(0));
        }
    }

    const bool lyric_effect_changed = view->saved_lyric_effect_setting != settings_get()->lyric_effect;
    const bool lyric_fill_changed = view->saved_lyric_fill_setting != settings_get()->lyric_fill;
    if ( lyric_effect_changed || lyric_fill_changed )
        ui_clear_sticky_animations(drawable);

    const bool fill_enabled_in_settings = settings_get()->lyric_fill != SET_LYRIC_FILL_DISABLED;
    if ( view->selected_language->song_language->has_sub_timings && line->num_timings > 0 && fill_enabled_in_settings ) {
        const bool settings_changed = lyric_effect_changed || lyric_fill_changed;
        calculate_sub_region_for_active_line(view, widget, view->song, line, settings_changed);
    }
}

static void set_line_inactive(const LyricsView_t *view, const int32_t index, LyricsState_t *state) {
    LyricLineWidget_t *widget = view->selected_language->lyric_widgets->data[index];
    Drawable_t *drawable = widget->line;

    int32_t alpha = 200;
    float blur = 0.f;
    int32_t distance = 0;
    int32_t comp_index = state->current_active >= 0 ? state->current_active : state->anchor;
    if ( comp_index < 0 )
        comp_index = index;

    distance = calculate_distance(view, index, comp_index);
    int32_t tmp_distance = distance;

    if ( state->current_active >= 0 && is_line_intermission(view, state->current_active) ) {
        // When the current line is an intermission between two segments, make every other line have min alpha
        tmp_distance = LINE_FADE_MAX_DISTANCE;
    }
    alpha = calculate_alpha(tmp_distance);
    blur = calculate_blur(tmp_distance);

    chain_line_under_previous(view, index, state);
    reposition_line_drawable(view, drawable, distance, CASCADE_TOWARDS);
    reposition_hint_for_line(view, index, distance, CASCADE_TOWARDS);

    // don't change the alpha if the user is hovering over the line
    if ( view->current_hovered_index == index ) {
        ui_drawable_set_alpha(drawable, calculate_alpha(0));
        ui_drawable_set_blur_radius_immediate(drawable, 0.f);
        blur_hint_for_line(view, index);
    } else {
        if ( alpha != drawable->alpha_mod ) {
            ui_drawable_set_alpha(drawable, alpha);
            fade_hint_for_line(view, index);
        }
        if ( blur != drawable->blur_radius ) {
            ui_drawable_set_blur_radius(drawable, blur);
            blur_hint_for_line(view, index);
        }
    }

    const LineState_t new_state = LINE_INACTIVE;
    if ( widget->state != new_state ) {
        ui_clear_sticky_animations(drawable);
        ui_drawable_disable_draw_region(drawable);
        ui_drawable_set_draw_underlay(drawable, false, 0);

        if ( widget->state == LINE_NONE ) {
            // We're applying the initial values to the line, meaning it still has the defaults from creation
            // so we don't need to animate any of this or else it actually looks weird and like the ui "falls into place" after
            // the initial loading
            ui_drawable_set_scale_factor_immediate(drawable, get_inactive_line_scale());
        } else {
            static AnimatedSetOpts_t anim_opts = {.duration = LINE_SCALE_FACTOR_INACTIVE_DURATION};
            ui_drawable_set_scale_factor_dur(drawable, get_inactive_line_scale(), anim_opts);
        }
        widget->state = new_state;

        scale_hint_for_line(view, index);
        fade_hint_for_line(view, index);
    }
}

static double get_lyric_line_scroll_position(const LyricsView_t *view, const int32_t index) {
    if ( index >= 0 ) {
        const double base_position = LINE_FIRST_VERTICAL_OFFSET * view->container->bounds.h;
        return get_line_drawable_by_index(view, index)->bounds.y - base_position;
    }
    return 0;
}

static LyricLineWidget_t *set_line_hidden(const LyricsView_t *view, const int32_t index, const LyricsState_t *state) {
    LyricLineWidget_t *widget = view->selected_language->lyric_widgets->data[index];
    Drawable_t *drawable = widget->line;

    const LineState_t new_state = LINE_HIDDEN;
    if ( widget->state != new_state ) {
        widget->state = new_state;
        ui_clear_sticky_animations(drawable);

        ui_drawable_disable_draw_region(drawable);
        ui_drawable_set_draw_underlay(drawable, false, 0);
        ui_drawable_set_scale_factor(drawable, get_inactive_line_scale());
        scale_hint_for_line(view, index);
    }

    const bool should_hide_past = settings_get()->past_language_visibility == SET_PAST_LYRICS_HIDE;
    const int32_t reference_index = state->current_active >= 0 ? state->current_active : state->anchor;
    const double prev_scroll_pos = get_lyric_line_scroll_position(view, reference_index);
    const double current_scroll_pos = view->container->overflow_y.set_amount;
    // Allow users to scroll up and see the past lyrics. if it's not scrolled, just fade to 0 as normal
    if ( should_hide_past && current_scroll_pos >= prev_scroll_pos ) {
        ui_drawable_set_alpha(drawable, 0);
        fade_hint_for_line(view, index);
    } else {
        int32_t distance;
        const bool is_intermission = is_line_intermission(view, view->selected_language->current_active_index);
        if ( reference_index < 0 || is_intermission ) {
            distance = LINE_FADE_MAX_DISTANCE;
        } else {
            distance = calculate_distance(view, index, reference_index);
        }
        // Don't change the alpha if the user is hovering over the line
        if ( view->current_hovered_index == index ) {
            ui_drawable_set_alpha(drawable, calculate_alpha(0));
            ui_drawable_set_blur_radius_immediate(drawable, 0.f);
        } else {
            ui_drawable_set_alpha(drawable, calculate_alpha(distance));
            ui_drawable_set_blur_radius(drawable, calculate_blur(distance));
        }
        fade_hint_for_line(view, index);
        blur_hint_for_line(view, index);
    }

    return widget;
}

static void set_line_almost_hidden(const LyricsView_t *view, const int32_t index, LyricsState_t *state) {
    LyricLineWidget_t *widget = view->selected_language->lyric_widgets->data[index];
    Drawable_t *drawable = widget->line;

    chain_line_under_previous(view, index, state);
    reposition_line_drawable(view, drawable, 0, CASCADE_TOWARDS);
    reposition_hint_for_line(view, index, 0, CASCADE_TOWARDS);

    const LineState_t new_state = LINE_ALMOST_HIDDEN;
    if ( widget->state != new_state ) {
        if ( widget->state == LINE_ACTIVE ) {
            const int32_t alpha = calculate_alpha(1);
            // TODO: Clear sticky animations but with a wind down instead of snapping back into place
            ui_drawable_disable_draw_region(drawable);
            ui_drawable_set_draw_underlay(drawable, false, 0);
            ui_drawable_set_alpha(drawable, alpha);
            fade_hint_for_line(view, index);
        }
        widget->state = new_state;
    }
}

static void collapse_hidden_lines(const LyricsView_t *view, LyricsState_t *state) {
    if ( state->anchor < 0 ) {
        // Keep the last line in place, also indirectly update the anchor to a non-zero value
        set_line_almost_hidden(view, state->num_lines - 1, state);
    }

    const int32_t boundary = state->anchor;
    const Drawable_t *relative = view->selected_language->lyric_anchor;

    for ( int32_t index = boundary - 1; index >= 0; index-- ) {
        const LyricLineWidget_t *widget = set_line_hidden(view, index, state);
        const int32_t distance = calculate_distance(view, index, boundary);
        chain_line_above(view, relative, index, distance);

        relative = is_hint_enabled(widget) ? widget->reading_hint : widget->line;
    }
}

static void reposition_credits(const LyricsView_t *view, const int32_t reference_index) {
    if ( view->selected_language->credit_separator == NULL )
        return;
    int32_t distance = 0;
    if ( reference_index >= 0 ) {
        const int32_t last_index = (int32_t)view->selected_language->lyric_widgets->size - 1;
        distance = calculate_distance(view, last_index, reference_index) + 1;
    }
    reposition_line_drawable(view, view->selected_language->credit_separator, distance, CASCADE_TOWARDS);
    reposition_line_drawable(view, view->selected_language->credits_prefix, distance, CASCADE_TOWARDS);
    reposition_line_drawable(view, view->selected_language->credits_content, distance, CASCADE_TOWARDS);
}

void ui_ex_lyrics_view_loop(LyricsView_t *view) {
    if ( view == NULL ) {
        error_abort("loop: lyrics_view is NULL");
    }
    if ( view->container->enabled == false )
        return;

    if ( !view->selected_language->song_language->has_timings )
        return;

    const double offset = view->song->time_offset;
    const double user_offset = settings_get()->global_audio_offset_ms / 1000.0;
    const double elapsed_time = audio_elapsed_time() + offset + user_offset;
    const int32_t num_lines = (int32_t)view->selected_language->song_language->lines->size;

    if ( num_lines <= 0 )
        return;

    LyricsState_t state = {.current_active = -1, .first_active = -1, .anchor = -1, .num_lines = num_lines};

    view->user_did_seek = fabs(elapsed_time - view->prev_elapsed) > 1.0;

    if ( view->language_changed ) {
        view->selected_language->current_active_index = -1;
    }

    for ( int32_t i = 0; i < num_lines; i++ ) {
        const Song_Line_t *line = view->selected_language->song_language->lines->data[i];
        if ( elapsed_time < line->base_start_time + line->base_duration ) {
            if ( elapsed_time >= line->base_start_time ) {
                set_line_active(view, i, &state);
            } else {
                set_line_inactive(view, i, &state);
            }
        } else {
            const bool is_last = i + 1 >= num_lines;
            bool in_gap = false;
            if ( !is_last ) {
                const Song_Line_t *next_line = view->selected_language->song_language->lines->data[i + 1];
                in_gap = elapsed_time < next_line->base_start_time;
            }
            const int32_t prev_active = state.current_active;
            if ( in_gap || prev_active >= 0 ) {
                // If the next line still hasn't reached its start time, don't completely vanish the line just yet
                set_line_almost_hidden(view, i, &state);
            }
        }
    }

    collapse_hidden_lines(view, &state);
    reposition_credits(view, state.current_active >= 0 ? state.current_active : state.anchor);

    const bool active_changed = state.first_active != view->selected_language->current_first_active_index;
    const bool screen_changed = events_window_changed();
    if ( state.first_active >= 0 && (active_changed || screen_changed || view->language_changed) ) {
        view->selected_language->current_first_active_index = state.first_active;
        ui_ex_lyrics_view_scroll_to_active(view);
    }
    view->language_changed = false;
    view->saved_lyric_effect_setting = settings_get()->lyric_effect;
    view->saved_lyric_fill_setting = settings_get()->lyric_fill;

    view->selected_language->current_active_index = state.current_active;
    view->prev_elapsed = elapsed_time;
}

void ui_ex_lyrics_view_on_read_hints_changed(const LyricsView_t *view) {
    for ( size_t i = 0; i < view->selected_language->lyric_widgets->size; i++ ) {
        apply_read_hint_visibility(view->selected_language->lyric_widgets->data[i]);
    }
}

void ui_ex_destroy_lyrics_view(LyricsView_t *view) {
    if ( view == NULL ) {
        error_abort("destroy: lyrics_view is NULL");
    }
    // The line widgets are registered on the container, so ui_finish destroys them along with their drawables. Only the
    // bookkeeping vecs are ours to destroy here
    for ( size_t i = 0; i < view->lyrics_languages->size; i++ ) {
        LyricsLanguage_t *lang = view->lyrics_languages->data[i];
        vec_destroy(lang->lyric_widgets);
        free((void *)lang->language_str);
        free(lang);
    }
    vec_destroy(view->lyrics_languages);
    free(view);
}

void ui_ex_lyrics_view_scroll_to_active(const LyricsView_t *view) {
    if ( !view->selected_language->song_language->has_timings )
        return;
    // Scroll to anchor
    ui_container_scroll_y_to(view->container, 0);
}

static void set_lyrics_language_visible(const LyricsLanguage_t *target, const bool visible) {
    for ( size_t i = 0; i < target->lyric_widgets->size; i++ ) {
        const LyricLineWidget_t *widget = target->lyric_widgets->data[i];
        widget->line->enabled = visible;
        apply_read_hint_visibility(widget);
    }

    if ( target->credit_separator != NULL )
        target->credit_separator->enabled = visible;
    if ( target->credits_prefix != NULL )
        target->credits_prefix->enabled = visible;
    if ( target->credits_content != NULL )
        target->credits_content->enabled = visible;
}

void ui_ex_lyrics_view_set_language(LyricsView_t *view, const char *language) {
    if ( view->selected_language != NULL && str_equals(view->selected_language->language_str, language) )
        return;

    LyricsLanguage_t *target = view->selected_language;
    for ( size_t i = 0; i < view->lyrics_languages->size; i++ ) {
        LyricsLanguage_t *lyrics = view->lyrics_languages->data[i];
        if ( str_equals(lyrics->language_str, language) ) {
            target = lyrics;
        }
    }

    if ( target == view->selected_language ) {
        printf("Warning: Lyrics language '%s' not found\n", language);
        return;
    }

    // Hide previous lyrics and show current ones
    if ( view->selected_language != NULL )
        set_lyrics_language_visible(view->selected_language, false);

    set_lyrics_language_visible(target, true);
    view->selected_language = target;
    view->current_hovered_index = -1;
    view->language_changed = true;
    // Recompute scroll bar bounds
    view->container->content_size_dirty = true;
}
