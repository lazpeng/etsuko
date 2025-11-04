#include "ui_ex.h"

#include "audio.h"
#include "error.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define LINE_VERTICAL_PADDING (0.035)
#define LINE_FADE_MAX_DISTANCE (5)
#define SCROLL_THRESHOLD (0.05)
#ifdef __EMSCRIPTEN__
#define SCROLL_MODIFIER (50)
#else
#define SCROLL_MODIFIER (10)
#endif

static bool is_line_intermission(const etsuko_LyricsView_t *view, const size_t index) {
    const etsuko_SongLine_t *line = view->song->lyrics_lines->data[index];
    return strncmp(line->full_text, "", 1) == 0 && line->base_duration > 1.5;
}

etsuko_LyricsView_t *ui_ex_make_lyrics_view(etsuko_Container_t *parent, const etsuko_Song_t *song) {
    if ( parent == NULL ) {
        error_abort("Parent container is null");
    }

    if ( song == NULL ) {
        error_abort("Song is null");
    }

    etsuko_LyricsView_t *view = calloc(1, sizeof(*view));
    view->container = parent;
    view->song = song;
    view->line_drawables = vec_init();

    if ( song->lyrics_lines->size == 0 ) {
        error_abort("Song has no lyrics");
    }

    const etsuko_Color_t color = {.r = 100, .b = 100, .g = 100, .a = 200};

    etsuko_DrawableAlignment_t base_alignment = ALIGN_LEFT;
    etsuko_LayoutFlags_t base_alignment_flags = LAYOUT_NONE;
    double base_offset_x = 0;
    if ( song->line_alignment == SONG_LINE_CENTER ) {
        base_alignment = ALIGN_CENTER;
        base_alignment_flags = LAYOUT_CENTER_X;
    } else if ( song->line_alignment == SONG_LINE_RIGHT ) {
        base_alignment = ALIGN_RIGHT;
        base_alignment_flags = LAYOUT_ANCHOR_RIGHT_X | LAYOUT_WRAP_AROUND_X | LAYOUT_PROPORTIONAL_X;
        base_offset_x = -0.3;
    }

    etsuko_Drawable_t *prev = NULL;
    for ( size_t i = 0; i < song->lyrics_lines->size; i++ ) {
        const etsuko_SongLine_t *line = song->lyrics_lines->data[i];

        char *line_text = line->full_text;
        if ( line_text == NULL ) {
            printf("Warn: line was not initialized properly. idx: %lu\n", i);
            continue;
        }

        if ( strncmp(line_text, "", 1) == 0 ) {
            if ( is_line_intermission(view, i) ) {
                line_text = "...";
            } else {
                line_text = " ";
            }
        }

        etsuko_DrawableAlignment_t alignment = base_alignment;
        etsuko_LayoutFlags_t alignment_flags = base_alignment_flags;
        double offset_x = base_offset_x;
        // line override
        if ( line->alignment != song->line_alignment ) {
            if ( line->alignment == SONG_LINE_CENTER ) {
                alignment = ALIGN_CENTER;
                alignment_flags = LAYOUT_CENTER_X;
            } else if ( line->alignment == SONG_LINE_RIGHT ) {
                alignment = ALIGN_RIGHT;
                alignment_flags = LAYOUT_ANCHOR_RIGHT_X | LAYOUT_WRAP_AROUND_X;
                offset_x = -200;
            } else if ( line->alignment == SONG_LINE_LEFT ) {
                alignment = ALIGN_LEFT;
                alignment_flags = LAYOUT_NONE;
                offset_x = 0;
            }
        }

        etsuko_Drawable_TextData_t data = {
            .text = line_text,
            .font_type = FONT_LYRICS,
            .em = 1.5,
            .wrap_enabled = true,
            .wrap_width_threshold = 0.85,
            .measure_at_em = 2.0,
            .color = color,
            .bold = false,
            .alignment = alignment,
        };
        etsuko_Layout_t layout = {
            .offset_y = LINE_VERTICAL_PADDING,
            .offset_x = offset_x,
            .flags = alignment_flags | LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT | LAYOUT_PROPORTIONAL_Y,
        };
        if ( prev != NULL ) {
            layout.relative_to = prev;
        }
        prev = ui_make_text(&data, parent, &layout);
        vec_add(view->line_drawables, prev);
    }

    for ( size_t i = 0; i < song->lyrics_lines->size; i++ ) {
        view->line_states[i] = LINE_INACTIVE;
        ui_animate_translation(view->line_drawables->data[i],
                                     &(etsuko_Animation_EaseTranslationData_t){.duration = 0.3, .ease = true});
        ui_animate_fade(view->line_drawables->data[i], &(etsuko_Animation_FadeInOutData_t){.duration = 1.0});
    }

    return view;
}

static void set_line_active(etsuko_LyricsView_t *view, const size_t index, const int32_t prev_active) {
    etsuko_Drawable_t *drawable = view->line_drawables->data[index];

    drawable->enabled = true;
    drawable->alpha_mod = 0xFF;

    etsuko_Drawable_TextData_t *data = drawable->custom_data;
    data->em = 2.0;
    data->color = (etsuko_Color_t){.r = 255, .b = 255, .g = 255, .a = 255};
    data->bold = false;

    const LineState_t new_state = LINE_ACTIVE;
    if ( view->line_states[index] != new_state ) {
        view->line_states[index] = new_state;
        ui_recompute_drawable(drawable);
    }

    if ( prev_active >= 0 ) {
        drawable->layout.relative_to = view->line_drawables->data[prev_active];
    } else {
        drawable->layout.relative_to = NULL;
    }
    drawable->layout.offset_y = 0;
    if ( drawable->layout.flags & LAYOUT_ANCHOR_BOTTOM_Y ) {
        drawable->layout.flags ^= LAYOUT_ANCHOR_BOTTOM_Y;
    }
    drawable->layout.flags |= LAYOUT_RELATION_Y_INCLUDE_HEIGHT;

    ui_reposition_drawable(drawable);
}

static int32_t calculate_alpha(const int32_t distance) {
    return 225 - 200 / LINE_FADE_MAX_DISTANCE * MIN(distance, LINE_FADE_MAX_DISTANCE);
}

static void set_line_inactive(etsuko_LyricsView_t *view, const size_t index, const int32_t prev_active) {
    etsuko_Drawable_t *drawable = view->line_drawables->data[index];
    if ( index > 0 ) {
        etsuko_Drawable_t *prev = view->line_drawables->data[index - 1];
        drawable->layout.relative_to = prev;
    }
    drawable->layout.offset_y = LINE_VERTICAL_PADDING;
    if ( drawable->layout.flags & LAYOUT_ANCHOR_BOTTOM_Y ) {
        drawable->layout.flags ^= LAYOUT_ANCHOR_BOTTOM_Y;
    }
    drawable->layout.flags |= LAYOUT_RELATION_Y_INCLUDE_HEIGHT;

    etsuko_Drawable_TextData_t *data = drawable->custom_data;
    data->em = 1.5;
    data->color = (etsuko_Color_t){.r = 100, .b = 100, .g = 100, .a = 255};
    data->bold = false;

    int32_t alpha = 200;
    if ( prev_active >= 0 && prev_active != (int32_t)index ) {
        int32_t distance = (int32_t)index - prev_active;

        if ( is_line_intermission(view, prev_active) ) {
            // When the current line is an intermission between two segments, make every other line have min alpha
            distance = LINE_FADE_MAX_DISTANCE;
        }
        alpha = calculate_alpha(distance);
    }

    if ( view->line_states[index] != LINE_INACTIVE ) {
        // Don't animate
        drawable->alpha_mod = alpha;
    } else {
        // Maybe animate if our alpha changed while still being inactive
        ui_drawable_set_alpha(drawable, alpha);
    }

    const LineState_t new_state = LINE_INACTIVE;
    if ( view->line_states[index] != new_state ) {
        view->line_states[index] = new_state;
        ui_recompute_drawable(drawable);
    } else {
        ui_reposition_drawable(drawable);
    }
}

static void set_line_hidden(etsuko_LyricsView_t *view, const size_t index) {
    etsuko_Drawable_t *drawable = view->line_drawables->data[index];

    const LineState_t new_state = LINE_HIDDEN;
    if ( view->line_states[index] != new_state ) {
        view->line_states[index] = new_state;
        drawable->layout.relative_to = NULL;
        drawable->layout.offset_y = -LINE_VERTICAL_PADDING;
        drawable->layout.flags |= LAYOUT_ANCHOR_BOTTOM_Y;

        if ( drawable->layout.flags & LAYOUT_RELATION_Y_INCLUDE_HEIGHT ) {
            drawable->layout.flags ^= LAYOUT_RELATION_Y_INCLUDE_HEIGHT;
        }

        etsuko_Drawable_TextData_t *data = drawable->custom_data;
        data->em = 1.5;
        data->color = (etsuko_Color_t){.r = 100, .b = 100, .g = 100, .a = 255};
        data->bold = false;

        ui_recompute_drawable(drawable);
    }

    // Allow users to scroll up and see the past lyrics. if it's not scrolled, just fade to 0 as normal
    if ( view->container->viewport_y < -SCROLL_THRESHOLD ) {
        ui_drawable_set_alpha(drawable, 0);
    } else {
        int32_t distance = abs(view->current_active_index - (int32_t)index);
        if ( is_line_intermission(view, view->current_active_index) ) {
            distance = LINE_FADE_MAX_DISTANCE;
        }
        ui_drawable_set_alpha(drawable, calculate_alpha(distance));
    }
}

static void set_line_almost_hidden(etsuko_LyricsView_t *view, const size_t index) {
    etsuko_Drawable_t *drawable = view->line_drawables->data[index];

    const LineState_t new_state = LINE_ALMOST_HIDDEN;
    if ( view->line_states[index] != new_state ) {
        if ( view->line_states[index] == LINE_ACTIVE ) {
            // Don't do anything, just fade into a low alpha
            const int32_t alpha = calculate_alpha(LINE_FADE_MAX_DISTANCE - 1);
            ui_drawable_set_alpha(drawable, alpha);
        } else {
            // Position the same as the drawable
            drawable->layout.relative_to = NULL;
            drawable->layout.offset_y = 0;
            if ( drawable->layout.flags & LAYOUT_ANCHOR_BOTTOM_Y ) {
                drawable->layout.flags ^= LAYOUT_ANCHOR_BOTTOM_Y;
            }
            ui_reposition_drawable(drawable);
        }
        view->line_states[index] = new_state;
    }
}

static etsuko_Drawable_t *stack_hidden_line_recursive(const etsuko_LyricsView_t *view, size_t idx) {
    if ( idx >= view->line_drawables->size - 1 )
        return NULL;

    if ( view->line_states[idx] != LINE_HIDDEN ) {
        int32_t next_hidden = -1;
        for ( size_t i = idx + 1; i < view->line_drawables->size; i++ ) {
            if ( view->line_states[i] != LINE_HIDDEN )
                continue;
            next_hidden = (int32_t)i;
            break;
        }
        if ( next_hidden < 0 )
            return NULL;
        idx = next_hidden;
    }

    etsuko_Drawable_t *drawable = view->line_drawables->data[idx];
    drawable->layout.relative_to = stack_hidden_line_recursive(view, idx + 1);
    ui_reposition_drawable(drawable);

    return drawable;
}

void ui_ex_lyrics_view_loop(etsuko_LyricsView_t *view) {
    if ( view == NULL ) {
        error_abort("loop: lyrics_view is null");
    }

    view->active_changed = false;

    int32_t prev_active = -1;
    const double offset = view->song->time_offset;
    const double elapsed_time = audio_elapsed_time() + offset;

    for ( size_t i = 0; i < view->song->lyrics_lines->size; i++ ) {
        const etsuko_SongLine_t *line = view->song->lyrics_lines->data[i];
        if ( elapsed_time < line->base_start_time + line->base_duration ) {
            if ( elapsed_time >= line->base_start_time ) {
                set_line_active(view, i, prev_active);
                prev_active = (int32_t)i;
            } else {
                if ( prev_active < 0 ) {
                    prev_active = view->current_active_index;
                }
                set_line_inactive(view, i, prev_active);
            }
        } else {
            // If the next line still hasn't reached its start time, don't completely vanish the line just yet
            if ( i + 1 < view->song->lyrics_lines->size ) {
                const etsuko_SongLine_t *next_line = view->song->lyrics_lines->data[i + 1];
                if ( elapsed_time < next_line->base_start_time ) {
                    set_line_almost_hidden(view, i);
                    continue;
                }
            }
            // else just set it hidden (or afterward when it finally should disappear)
            set_line_hidden(view, i);
        }
    }

    view->current_active_index = prev_active;

    // Now do a reverse loop setting all the hidden lines to stack on top of each other
    stack_hidden_line_recursive(view, 0);

    view->prev_viewport_y = view->container->viewport_y;
}

static double get_hidden_height(const etsuko_LyricsView_t *view) {
    double height = 0;
    for ( size_t i = 0; i < view->line_drawables->size; i++ ) {
        const LineState_t state = view->line_states[i];
        if ( state == LINE_HIDDEN || state == LINE_ALMOST_HIDDEN ) {
            const etsuko_Drawable_t *drawable = view->line_drawables->data[i];
            height += drawable->bounds.h + LINE_VERTICAL_PADDING;
        }
    }

    return height;
}

static double get_visible_height(const etsuko_LyricsView_t *view) {
    double height = 0;
    for ( size_t i = 0; i < view->line_drawables->size; i++ ) {
        const LineState_t state = view->line_states[i];
        if ( state == LINE_ACTIVE || state == LINE_INACTIVE ) {
            const etsuko_Drawable_t *drawable = view->line_drawables->data[i];
            height += drawable->bounds.h + LINE_VERTICAL_PADDING;
        }
    }

    return -height + view->container->bounds.h * 0.5;
}

void ui_ex_lyrics_view_on_scroll(const etsuko_LyricsView_t *view, const double delta_y) {
    if ( fabs(delta_y) < SCROLL_THRESHOLD )
        return;

    double new_viewport_y = view->container->viewport_y + delta_y * SCROLL_MODIFIER;
    new_viewport_y = MIN(new_viewport_y, get_hidden_height(view));
    new_viewport_y = MAX(new_viewport_y, get_visible_height(view));

    view->container->viewport_y = new_viewport_y;
}

void ui_ex_destroy_lyrics_view(etsuko_LyricsView_t *view) {
    if ( view == NULL ) {
        error_abort("destroy: lyrics_view is null");
    }
    // No need to free the drawables individually
    vec_destroy(view->line_drawables);
    free(view);
}
