#include "ui_ex.h"

#include "audio.h"
#include "config.h"
#include "error.h"
#include "str_utils.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// TODO: Refactor this shit holy shit
#define LINE_VERTICAL_PADDING (0.035)
#define LINE_RIGHT_ALIGN_PADDING (-0.1)
#define LINE_FADE_MAX_DISTANCE (5)
#define SCROLL_THRESHOLD (0.05)
#ifdef __EMSCRIPTEN__
#define SCROLL_MODIFIER (50)
#else
#define SCROLL_MODIFIER (10)
#endif
#define LINE_SCALE_FACTOR_INACTIVE (0.75f)
#define ALPHA_DISTANCE_BASE_CALC (100)
#define ALPHA_DISTANCE_MIN_VALUE (25)
#define REGION_ANIMATION_DURATION (0.2)
#define LINE_SCALE_FACTOR_INACTIVE_DURATION (0.2)
#define SCALE_ANIMATION_DURATION (0.1)
#define SCALE_ANIMATION_OUT_DURATION (0.3)
#define TRANSLATION_ANIMATION_DURATION (0.3)
#define TRANSLATION_ANIMATION_OUT_DURATION (0.3)

static bool is_line_intermission(const LyricsView_t *view, const int32_t index) {
    const Song_Line_t *line = view->song->lyrics_lines->data[index];
    return str_is_empty(line->full_text) && line->base_duration > 5;
}

static void ensure_read_hints_initialized(Ui_t *ui, LyricsView_t *view) {
    for ( size_t i = 0; i < view->line_read_hints->size; i++ ) {
        Drawable_t *hint = view->line_read_hints->data[i];

        if ( is_line_intermission(view, i) )
            continue;

        if ( hint->pending_recompute ) {
            if ( hint->texture != NULL )
                render_destroy_texture(hint->texture);
            
            const Drawable_t *drawable = view->line_drawables->data[i];
            const Drawable_TextData_t *lyric_data = drawable->custom_data;
            if ( lyric_data->line_offsets == NULL )
                continue;

            // TODO: Measure actual final size
            render_make_texture_target(drawable->bounds.w * 1.5, drawable->bounds.h * 1.5);
            const BlendMode_t blend_mode = render_get_blend_mode();
            render_set_blend_mode(BLEND_MODE_NONE);

            int pixels = render_measure_pixels_from_em(0.8);
            Color_t white = {255,255,255,255};

            const Song_Line_t *line = view->song->lyrics_lines->data[i];
            size_t read_i = 0;
            for ( size_t off_i = 0; off_i < lyric_data->line_offsets->size; off_i++ ) {
                const TextOffsetInfo_t *offset_info = lyric_data->line_offsets->data[off_i];
                int32_t y = offset_info->start_y + offset_info->height;

                int32_t x = 0;
                for ( ; read_i < line->readings->size; read_i++ ) {
                    const Song_LineReading_t *reading = line->readings->data[read_i];
                    if ( (int32_t)reading->start_ch_idx >= offset_info->start_char_idx + offset_info->num_chars )
                        break; // It's on the next line
                    
                    const int32_t index_on_this_line = MAX(0, reading->start_ch_idx - offset_info->start_char_idx);
                    const CharOffsetInfo_t *character = offset_info->char_offsets->data[index_on_this_line];
                    const int32_t character_x = character->x;

                    // TODO: Get a better anchoring for the x value from stb
                    x = MAX(x + 5, character_x + ui_compute_relative_horizontal(ui, 0.01, view->container));

                    Texture_t *test = render_make_text(reading->reading_text, pixels, &white, FONT_UI);
                    const Bounds_t bounds = {
                        .x = x, .y = y, .w = test->width, .h = test->height,
                    };
                    const DrawTextureOpts_t opts = {
                        .alpha_mod = 255,
                        .color_mod = 1.f,
                    };
                    render_draw_texture(test, &bounds, &opts);
                    x += test->width;

                    render_destroy_texture(test);
                }
            }
            hint->texture = render_restore_texture_target();
            render_set_blend_mode(blend_mode);

            hint->pending_recompute = false;
        }
    }
}

static void reposition_hint_for_line(Ui_t *ui, const LyricsView_t *view, int32_t index) {
    if ( index < (int32_t)view->line_read_hints->size ) {
        Drawable_t *hint = view->line_read_hints->data[index];
        ui_reposition_drawable(ui, hint);
    }
}

static void scale_hint_for_line(const LyricsView_t *view, int32_t index) {
    if ( index < (int32_t)view->line_read_hints->size ) {
        const Drawable_t *drawable = view->line_drawables->data[index];
        Drawable_t *hint = view->line_read_hints->data[index];
        ui_drawable_set_scale_factor(hint, 1.f + drawable->bounds.scale_mod);
    }
}

static void fade_hint_for_line(const LyricsView_t *view, int32_t index) {
    if ( index < (int32_t)view->line_read_hints->size ) {
        const Drawable_t *drawable = view->line_drawables->data[index];
        Drawable_t *hint = view->line_read_hints->data[index];
        ui_drawable_set_alpha(hint, drawable->alpha_mod);
    }
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
    view->line_drawables = vec_init();
    view->line_read_hints = vec_init();

    const bool should_generate_reading_hints = song->has_reading_info && config_get()->enable_reading_hints;

    if ( song->lyrics_lines->size == 0 ) {
        error_abort("Song has no lyrics");
    }

    const Color_t color = {.r = 255, .b = 255, .g = 255, .a = 255};

    DrawableAlignment_t base_alignment = ALIGN_LEFT;
    LayoutFlags_t base_alignment_flags = LAYOUT_NONE;
    double base_offset_x = 0;
    if ( song->line_alignment == SONG_LINE_CENTER ) {
        base_alignment = ALIGN_CENTER;
        base_alignment_flags = LAYOUT_CENTER_X;
    } else if ( song->line_alignment == SONG_LINE_RIGHT ) {
        base_alignment = ALIGN_RIGHT;
        base_alignment_flags = LAYOUT_ANCHOR_RIGHT_X | LAYOUT_WRAP_AROUND_X | LAYOUT_PROPORTIONAL_X;
        base_offset_x = LINE_RIGHT_ALIGN_PADDING;
    }

    Drawable_t *prev = NULL;
    for ( size_t i = 0; i < song->lyrics_lines->size; i++ ) {
        const Song_Line_t *line = song->lyrics_lines->data[i];

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
        if ( line->alignment != song->line_alignment ) {
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

        Drawable_TextData_t data = {.text = line_text,
                                    .font_type = FONT_LYRICS,
                                    .em = 2.5,
                                    .wrap_enabled = true,
                                    .wrap_width_threshold = 0.85,
                                    .color = color,
                                    .alignment = alignment,
                                    .draw_shadow = config_get()->draw_lyric_shadow,
                                    .compute_offsets = song->has_sub_timings || song->has_reading_info};
        Layout_t layout = {
            .offset_y = LINE_VERTICAL_PADDING,
            .offset_x = offset_x,
            .flags = alignment_flags | LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y,
        };
        if ( prev != NULL ) {
            layout.relative_to = prev;
        }
        prev = ui_make_text(ui, &data, parent, &layout);
        vec_add(view->line_drawables, prev);

        view->line_states[i] = LINE_NONE;
        ui_animate_translation(prev, &(Animation_EaseTranslationData_t){.duration = TRANSLATION_ANIMATION_DURATION, .ease_func = ANIM_EASE_OUT_CUBIC});
        ui_animate_fade(prev, &(Animation_FadeInOutData_t){.duration = 1.0, .ease_func = ANIM_EASE_OUT_CUBIC});
        ui_animate_scale(prev, &(Animation_ScaleData_t){.duration = SCALE_ANIMATION_DURATION});
        ui_animate_draw_region(prev, &(Animation_DrawRegionData_t){.duration = REGION_ANIMATION_DURATION, .ease_func = ANIM_EASE_OUT_QUAD});

        if ( should_generate_reading_hints ) {
            const Layout_t layout = {
                .offset_x = 0,
                .offset_y = 0,
                .flags = LAYOUT_RELATIVE_TO_POS | LAYOUT_PROPORTIONAL_Y,
                .relative_to = prev
            };
            Drawable_t *hint = ui_make_custom(ui, parent, &layout);
            ui_animate_translation(hint, &(Animation_EaseTranslationData_t){.duration = TRANSLATION_ANIMATION_DURATION, .ease_func = ANIM_EASE_OUT_CUBIC});
            ui_animate_fade(hint, &(Animation_FadeInOutData_t){.duration = 1.0, .ease_func = ANIM_EASE_OUT_CUBIC});
            ui_animate_scale(hint, &(Animation_ScaleData_t){.duration = SCALE_ANIMATION_DURATION});

            vec_add(view->line_read_hints, hint);
        }
    }

    if ( !str_is_empty(song->credits) ) {
        Drawable_t *last = view->line_drawables->data[view->line_drawables->size - 1];
        view->credit_separator = ui_make_rectangle(
            ui, &(Drawable_RectangleData_t){.color = {.r = 200, .g = 200, .b = 200, .a = 150}, .border_radius_em = 1.0},
            view->container,
            &(Layout_t){.offset_y = 0.05,
                        .offset_x = 0,
                        .width = 0.8,
                        .height = 1,
                        .flags = LAYOUT_PROPORTIONAL_W | LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT |
                                 LAYOUT_PROPORTIONAL_Y,
                        .relative_to = last});
        ui_animate_translation(view->credit_separator, &(Animation_EaseTranslationData_t){.duration = 0.3, .ease_func = ANIM_EASE_OUT_CUBIC});

        view->credits_prefix =
            ui_make_text(ui,
                         &(Drawable_TextData_t){.text = "Written by: ",
                                                .draw_shadow = true,
                                                .em = 0.8,
                                                .font_type = FONT_UI,
                                                .alignment = ALIGN_LEFT,
                                                .color = {200, 200, 200, 255}},
                         view->container,
                         &(Layout_t){.offset_y = 0.01,
                                     .flags = LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y,
                                     .relative_to = view->credit_separator});
        ui_drawable_set_alpha_immediate(view->credits_prefix, 150);
        ui_animate_translation(view->credits_prefix, &(Animation_EaseTranslationData_t){.duration = 0.3, .ease_func = ANIM_EASE_OUT_CUBIC});

        view->credits_content =
            ui_make_text(ui,
                         &(Drawable_TextData_t){.text = song->credits,
                                                .draw_shadow = true,
                                                .em = 0.8,
                                                .font_type = FONT_UI,
                                                .alignment = ALIGN_LEFT,
                                                .color = {200, 200, 200, 255}},
                         view->container,
                         &(Layout_t){.offset_y = 0,
                                     .offset_x = 0.001,
                                     .flags = LAYOUT_RELATIVE_TO_POS | LAYOUT_RELATION_X_INCLUDE_WIDTH | LAYOUT_PROPORTIONAL_POS,
                                     .relative_to = view->credits_prefix});
        ui_drawable_set_alpha_immediate(view->credits_prefix, 200);
        ui_animate_translation(view->credits_content, &(Animation_EaseTranslationData_t){.duration = 0.3, .ease_func = ANIM_EASE_OUT_CUBIC});
    }

    ensure_read_hints_initialized(ui, view);

    return view;
}

static int32_t calculate_alpha(const int32_t distance) {
    const int32_t dec = ALPHA_DISTANCE_BASE_CALC / LINE_FADE_MAX_DISTANCE * MIN(distance, LINE_FADE_MAX_DISTANCE);
    return MAX(ALPHA_DISTANCE_MIN_VALUE, ALPHA_DISTANCE_BASE_CALC - dec);
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
            const Song_Line_t *line = view->song->lyrics_lines->data[i];
            if ( str_is_empty(line->full_text) ) {
                distance -= 1;
            }
        }
    }

    return MAX(1, distance);
}

static void calculate_sub_region_for_active_line_linear(Drawable_t *drawable, const Song_t *song, const Song_Line_t *line) {
    const Drawable_TextData_t *text_data = drawable->custom_data;

    DrawRegionOptSet_t draw_regions = {0};
    draw_regions.num_regions = (int32_t)text_data->line_offsets->size;

    // Overshoot the time a little to compensate for the animation
    const double audio_elapsed = audio_elapsed_time() + song->time_offset + REGION_ANIMATION_DURATION;
    int32_t timing_offset_start = 0;
    // Calculate how much of each line we need to show
    for ( size_t i = 0; i < text_data->line_offsets->size; i++ ) {
        const TextOffsetInfo_t *offset_info = text_data->line_offsets->data[i];
        const float line_width = (float)(offset_info->width / drawable->bounds.w);
        // Calculate how much each segment should contribute to this line's fill
        // Start with the offset from the part where the line starts depending on the alignment
        float x1 = (float)(offset_info->start_x / drawable->bounds.w);
        for ( int32_t s = timing_offset_start; s < line->num_timings; s++ ) {
            const Song_LineTiming_t *timing = &line->timings[s];
            if ( timing->start_idx > offset_info->end_byte_offset )
                break;
            if ( timing->end_idx <= offset_info->start_byte_offset )
                continue;
            timing_offset_start += 1;

            const int timing_end_idx = MIN(timing->end_idx, offset_info->end_byte_offset);
            const int timing_start_idx = MAX(timing->start_idx, offset_info->start_byte_offset);
            const int segment_length_in_current_line = timing_end_idx - timing_start_idx;
            if ( segment_length_in_current_line < 0 )
                continue;
            const int current_line_length = offset_info->end_byte_offset - offset_info->start_byte_offset;
            const double line_contribution = (double)segment_length_in_current_line / current_line_length;
            const double elapsed_since_line = audio_elapsed - (line->base_start_time + timing->cumulative_duration);
            if ( elapsed_since_line < 0 )
                break;
            const double segment_progress = MIN(1.f, elapsed_since_line / (timing->duration));

            x1 += (float)(segment_progress * line_contribution * line_width);
        }
        draw_regions.regions[i].x1_perc = MIN(1.f, x1);

        // x0 is always at the beginning
        draw_regions.regions[i].x0_perc = 0.f;
        // y0 is the beginning of this line
        draw_regions.regions[i].y0_perc = (float)(offset_info->start_y / drawable->bounds.h);
        // y1 is the end of this line
        draw_regions.regions[i].y1_perc = (float)(offset_info->height / drawable->bounds.h);
    }
    ui_drawable_set_draw_region(drawable, &draw_regions);
}

static void calculate_sub_region_for_active_line_full_word(Drawable_t *drawable, const Song_t *song, const Song_Line_t *line) {
    // A slight variation that highlights the entire portion of the segment
    // Mainly intended when the timing is done per-syllable
    const Drawable_TextData_t *text_data = drawable->custom_data;

    DrawRegionOptSet_t draw_regions = {0};
    draw_regions.num_regions = (int32_t)text_data->line_offsets->size;

    double last_segment_remaining = 0.0;
    const double audio_elapsed = audio_elapsed_time() + song->time_offset;
    int32_t timing_offset_start = 0;
    // Calculate how much of each line we need to show
    for ( size_t i = 0; i < text_data->line_offsets->size; i++ ) {
        const TextOffsetInfo_t *offset_info = text_data->line_offsets->data[i];
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

            const double elapsed_since_segment = audio_elapsed - (line->base_start_time + timing->cumulative_duration);
            if ( elapsed_since_segment <= 0.0 )
                break;
            
            timing_offset_start += 1;

            // The secret here is that we calculate each letter boundary and always set the fill size to that
            // for the whole duration of the segment
            double segment_width = 0.0;
            // The code below assumes any line breaks are not made inside a utf-8 character (duh)
            const int32_t segment_start_in_line = MAX(0, timing->start_char_idx - offset_info->start_char_idx);
            for ( int32_t ci = 0; ci < segment_length_in_current_line; ci++ ) {
                size_t index = ci + segment_start_in_line;
                const CharOffsetInfo_t *char_info = offset_info->char_offsets->data[index];
                segment_width += char_info->width;
            }

            const double segment_fill_contribution = segment_width / drawable->bounds.w;

            x1 += (float)segment_fill_contribution;
            last_segment_remaining = timing->duration - elapsed_since_segment;
        }
        draw_regions.regions[i].x1_perc = MIN(1.f, x1);

        // x0 is always at the beginning
        draw_regions.regions[i].x0_perc = 0.f;
        // y0 is the beginning of this line
        draw_regions.regions[i].y0_perc = (float)(offset_info->start_y / drawable->bounds.h);
        // y1 is the end of this line
        draw_regions.regions[i].y1_perc = (float)(offset_info->height / drawable->bounds.h);
    }
    ui_drawable_set_draw_region_dur(drawable, &draw_regions, MAX(REGION_ANIMATION_DURATION, last_segment_remaining));
}

static void set_line_active(Ui_t *ui, LyricsView_t *view, const int32_t index, const int32_t prev_active) {
    Drawable_t *drawable = view->line_drawables->data[index];

    drawable->enabled = true;
    ui_drawable_set_alpha_immediate(drawable, 0xFF);

    ui_drawable_set_scale_factor(drawable, 1.f);
    scale_hint_for_line(view, index);
    fade_hint_for_line(view, index);

    Drawable_t *prev_relative = NULL;
    if ( prev_active >= 0 ) {
        prev_relative = view->line_drawables->data[prev_active];
    }

    const Song_Line_t *line = view->song->lyrics_lines->data[index];

    const LineState_t new_state = LINE_ACTIVE;
    if ( view->line_states[index] != new_state ) {
        view->line_states[index] = new_state;

        drawable->layout.offset_y = 0;
        if ( drawable->layout.flags & LAYOUT_ANCHOR_BOTTOM_Y ) {
            drawable->layout.flags ^= LAYOUT_ANCHOR_BOTTOM_Y;
        }
        drawable->layout.flags |= LAYOUT_RELATION_Y_INCLUDE_HEIGHT;
        drawable->layout.relative_to = prev_relative;
        ui_reposition_drawable(ui, drawable);
        reposition_hint_for_line(ui, view, index);

        if ( view->song->has_sub_timings && line->num_timings > 0 ) {
            ui_drawable_set_draw_underlay(drawable, true, calculate_alpha(0));
        }

        view->layout_dirty = true;
    } else if ( prev_relative != drawable->layout.relative_to ) {
        drawable->layout.relative_to = prev_relative;
        ui_reposition_drawable(ui, drawable);
        reposition_hint_for_line(ui, view, index);

        view->layout_dirty = true;
    }

    if ( view->song->has_sub_timings && line->num_timings > 0 ) {
        if ( view->song->fill_type == SONG_LINE_FILL_LINEAR ) {
            calculate_sub_region_for_active_line_linear(drawable, view->song, line);
        } else if ( view->song->fill_type == SONG_LINE_FILL_FULL_WORD ) {
            calculate_sub_region_for_active_line_full_word(drawable, view->song, line);
        }
    }
}

static void check_line_hover(const LyricsView_t *view, Drawable_t *drawable, const int32_t index) {
    if ( ui_mouse_hovering_drawable(drawable, 0, NULL, NULL, NULL) ) {
        ui_drawable_set_alpha_immediate(drawable, calculate_alpha(0));
    }
    if ( ui_mouse_clicked_drawable(drawable, 0, NULL, NULL, NULL) ) {
        const Song_Line_t *line = view->song->lyrics_lines->data[index];
        audio_seek(line->base_start_time);
        view->container->viewport_y = 0;
    }
}

static void set_line_inactive(Ui_t *ui, LyricsView_t *view, const int32_t index, const int32_t prev_active) {
    Drawable_t *drawable = view->line_drawables->data[index];
    if ( index > 0 ) {
        Drawable_t *prev = view->line_drawables->data[index - 1];
        drawable->layout.relative_to = prev;
    }

    int32_t alpha = 200;
    if ( prev_active >= 0 && prev_active != (int32_t)index ) {
        int32_t distance = calculate_distance(view, index, prev_active);

        if ( is_line_intermission(view, prev_active) ) {
            // When the current line is an intermission between two segments, make every other line have min alpha
            distance = LINE_FADE_MAX_DISTANCE;
        }
        alpha = calculate_alpha(distance);
    }

    if ( alpha != drawable->alpha_mod ) {
        ui_drawable_set_alpha(drawable, alpha);
    }

    const LineState_t new_state = LINE_INACTIVE;
    if ( view->line_states[index] != new_state ) {
        const LineState_t prev_state = view->line_states[index];
        view->line_states[index] = new_state;

        drawable->layout.offset_y = LINE_VERTICAL_PADDING;
        if ( drawable->layout.flags & LAYOUT_ANCHOR_BOTTOM_Y ) {
            drawable->layout.flags ^= LAYOUT_ANCHOR_BOTTOM_Y;
        }
        drawable->layout.flags |= LAYOUT_RELATION_Y_INCLUDE_HEIGHT;

        ui_drawable_disable_draw_region(drawable);
        ui_drawable_set_draw_underlay(drawable, false, 0);
        if ( prev_state == LINE_NONE ) {
            ui_drawable_set_scale_factor_immediate(drawable, LINE_SCALE_FACTOR_INACTIVE);
            scale_hint_for_line(view, index);
            fade_hint_for_line(view, index);
        } else {
            ui_drawable_set_scale_factor_dur(drawable, LINE_SCALE_FACTOR_INACTIVE, LINE_SCALE_FACTOR_INACTIVE_DURATION);
            scale_hint_for_line(view, index);
            fade_hint_for_line(view, index);
        }
        ui_reposition_drawable(ui, drawable);
        reposition_hint_for_line(ui, view, index);

        view->layout_dirty = true;
    } else if ( view->layout_dirty ) {
        ui_reposition_drawable(ui, drawable);
        reposition_hint_for_line(ui, view, index);
    }

    check_line_hover(view, drawable, index);
}

static void set_line_hidden(LyricsView_t *view, const int32_t index) {
    Drawable_t *drawable = view->line_drawables->data[index];

    // TODO: Fix vertical padding when lines are hidden. The gap is bigger than for inactive lines
    const LineState_t new_state = LINE_HIDDEN;
    if ( view->line_states[index] != new_state ) {
        view->line_states[index] = new_state;
        drawable->layout.relative_to = NULL;
        drawable->layout.offset_y = 0;//-LINE_VERTICAL_PADDING;
        drawable->layout.flags |= LAYOUT_ANCHOR_BOTTOM_Y;

        if ( drawable->layout.flags & LAYOUT_RELATION_Y_INCLUDE_HEIGHT ) {
            drawable->layout.flags ^= LAYOUT_RELATION_Y_INCLUDE_HEIGHT;
        }

        ui_drawable_disable_draw_region(drawable);
        ui_drawable_set_draw_underlay(drawable, false, 0);
        ui_drawable_set_scale_factor(drawable, LINE_SCALE_FACTOR_INACTIVE);
        scale_hint_for_line(view, index);
    }

    const double threshold = config_get()->hide_past_lyrics ? SCROLL_THRESHOLD : -SCROLL_THRESHOLD;
    // Allow users to scroll up and see the past lyrics. if it's not scrolled, just fade to 0 as normal
    if ( view->container->viewport_y < threshold ) {
        ui_drawable_set_alpha(drawable, 0);
        fade_hint_for_line(view, index);
    } else {
        int32_t distance = calculate_distance(view, index, view->current_active_index);
        if ( is_line_intermission(view, view->current_active_index) ) {
            distance = LINE_FADE_MAX_DISTANCE;
        }
        ui_drawable_set_alpha(drawable, calculate_alpha(distance));
        fade_hint_for_line(view, index);
        // If it's visible, let the user click on it to wind back to that line
        check_line_hover(view, drawable, index);
    }
}

static void set_line_almost_hidden(Ui_t *ui, LyricsView_t *view, const int32_t index) {
    Drawable_t *drawable = view->line_drawables->data[index];

    const LineState_t new_state = LINE_ALMOST_HIDDEN;
    if ( view->line_states[index] != new_state ) {
        if ( view->line_states[index] == LINE_ACTIVE ) {
            const int32_t alpha = calculate_alpha(1);
            ui_drawable_disable_draw_region(drawable);
            ui_drawable_set_draw_underlay(drawable, false, 0);
            ui_drawable_set_alpha(drawable, alpha);
        } else {
            // Position the same as the drawable
            drawable->layout.relative_to = NULL;
            drawable->layout.offset_y = 0;
            if ( drawable->layout.flags & LAYOUT_ANCHOR_BOTTOM_Y ) {
                drawable->layout.flags ^= LAYOUT_ANCHOR_BOTTOM_Y;
            }
            ui_reposition_drawable(ui, drawable);
            reposition_hint_for_line(ui, view, index);

            view->layout_dirty = true;
        }
        view->line_states[index] = new_state;
    }
}

static Drawable_t *stack_hidden_line_recursive(Ui_t *ui, const LyricsView_t *view, int32_t idx) {
    if ( idx >= (int32_t)view->line_drawables->size - 1 )
        return NULL;

    if ( view->line_states[idx] != LINE_HIDDEN ) {
        int32_t next_hidden = -1;
        for ( int32_t i = idx + 1; i < (int32_t)view->line_drawables->size; i++ ) {
            if ( view->line_states[i] != LINE_HIDDEN )
                continue;
            next_hidden = i;
            break;
        }
        if ( next_hidden < 0 )
            return NULL;
        idx = next_hidden;
    }

    Drawable_t *drawable = view->line_drawables->data[idx];
    drawable->layout.relative_to = stack_hidden_line_recursive(ui, view, idx + 1);
    ui_drawable_disable_draw_region(drawable);
    ui_drawable_set_draw_underlay(drawable, false, 0);
    ui_reposition_drawable(ui, drawable);
    reposition_hint_for_line(ui, view, idx);

    return drawable;
}

void ui_ex_lyrics_view_loop(Ui_t *ui, LyricsView_t *view) {
    if ( view == NULL ) {
        error_abort("loop: lyrics_view is NULL");
    }

    view->layout_dirty = false;

    int32_t prev_active = -1;
    const double offset = view->song->time_offset;
    const double elapsed_time = audio_elapsed_time() + offset;

    for ( int32_t i = 0; i < (int32_t)view->song->lyrics_lines->size; i++ ) {
        const Song_Line_t *line = view->song->lyrics_lines->data[i];
        if ( elapsed_time < line->base_start_time + line->base_duration ) {
            if ( elapsed_time >= line->base_start_time ) {
                set_line_active(ui, view, i, prev_active);
                prev_active = i;
            } else {
                if ( prev_active < 0 ) {
                    prev_active = view->current_active_index;
                }
                set_line_inactive(ui, view, i, prev_active);
            }
        } else {
            // If the next line still hasn't reached its start time, don't completely vanish the line just yet
            if ( i + 1 < (int32_t)view->song->lyrics_lines->size ) {
                const Song_Line_t *next_line = view->song->lyrics_lines->data[i + 1];
                if ( elapsed_time < next_line->base_start_time ) {
                    set_line_almost_hidden(ui, view, i);
                    continue;
                }
            }
            // else just set it hidden (or afterward when it finally should disappear)
            set_line_hidden(view, i);
        }
    }

    view->current_active_index = prev_active;

    // Now do a reverse loop setting all the hidden lines to stack on top of each other
    stack_hidden_line_recursive(ui, view, 0);

    if ( view->layout_dirty ) {
        if ( view->credit_separator )
            ui_recompute_drawable(ui, view->credit_separator);
        if ( view->credits_prefix )
            ui_recompute_drawable(ui, view->credits_prefix);
        if ( view->credits_content )
            ui_recompute_drawable(ui, view->credits_content);
    }

    view->prev_viewport_y = view->container->viewport_y;
}

void ui_ex_lyrics_view_on_screen_change(Ui_t *ui, LyricsView_t *view) {
    ensure_read_hints_initialized(ui, view);
}

static double get_hidden_height(const LyricsView_t *view) {
    if ( view->line_states[0] != LINE_HIDDEN )
        return 0;

    const Drawable_t *first_non_hidden = NULL;
    for ( size_t i = 0; i < view->line_drawables->size; i++ ) {
        if ( view->line_states[i] != LINE_HIDDEN ) {
            first_non_hidden = view->line_drawables->data[i];
            break;
        }
    }
    const Drawable_t *first_line = view->line_drawables->data[0];
    if ( first_non_hidden == NULL || first_non_hidden == first_line )
        return 0;

    return first_non_hidden->bounds.y - first_line->bounds.y;
}

static double get_visible_height(const LyricsView_t *view) {
    const Drawable_t *first_visible = NULL;
    for ( size_t i = 0; i < view->line_drawables->size; i++ ) {
        if ( view->line_states[i] == LINE_HIDDEN ) {
            continue;
        }
        first_visible = view->line_drawables->data[i];
        break;
    }

    const Drawable_t *last_visible = view->line_drawables->data[view->line_drawables->size - 1];
    if ( first_visible == NULL || last_visible == first_visible )
        return 0;
    return -(last_visible->bounds.y - first_visible->bounds.y);
}

void ui_ex_lyrics_view_on_scroll(const LyricsView_t *view, const double delta_y) {
    if ( fabs(delta_y) < SCROLL_THRESHOLD )
        return;

    double new_viewport_y = view->container->viewport_y + delta_y * SCROLL_MODIFIER;
    new_viewport_y = MIN(new_viewport_y, get_hidden_height(view));
    new_viewport_y = MAX(new_viewport_y, get_visible_height(view));

    view->container->viewport_y = new_viewport_y;
}

void ui_ex_destroy_lyrics_view(LyricsView_t *view) {
    if ( view == NULL ) {
        error_abort("destroy: lyrics_view is NULL");
    }
    // No need to free the drawables individually
    vec_destroy(view->line_drawables);
    vec_destroy(view->line_read_hints);
    free(view);
}
