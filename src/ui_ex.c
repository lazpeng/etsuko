#include "ui_ex.h"

#include "audio.h"
#include "error.h"
#include "str_utils.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define LINE_VERTICAL_PADDING (0.035)
#define LINE_RIGHT_ALIGN_PADDING (-0.1)
#define LINE_FADE_MAX_DISTANCE (3)
#define SCROLL_THRESHOLD (0.05)
#ifdef __EMSCRIPTEN__
#define SCROLL_MODIFIER (50)
#else
#define SCROLL_MODIFIER (10)
#endif
#define LINE_COLOR_MOD_INACTIVE (0.35f)
#define LINE_SCALE_FACTOR_INACTIVE (0.75f)

static bool is_line_intermission(const LyricsView_t *view, const int32_t index) {
    const Song_Line_t *line = view->song->lyrics_lines->data[index];
    return strncmp(line->full_text, "", 1) == 0 && line->base_duration > 1.5;
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
                                    .bold = false,
                                    .alignment = alignment,
                                    .draw_shadow = true};
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
    }

    for ( size_t i = 0; i < song->lyrics_lines->size; i++ ) {
        Drawable_t *drawable = view->line_drawables->data[i];
        view->line_states[i] = LINE_NONE;
        ui_animate_translation(drawable, &(Animation_EaseTranslationData_t){.duration = 0.3, .ease = true});
        ui_animate_fade(drawable, &(Animation_FadeInOutData_t){.duration = 1.0});
        ui_animate_scale(drawable, &(Animation_ScaleData_t){.duration = 0.05});
    }

    return view;
}

static void set_line_active(Ui_t *ui, LyricsView_t *view, const int32_t index, const int32_t prev_active) {
    Drawable_t *drawable = view->line_drawables->data[index];

    drawable->enabled = true;
    ui_drawable_set_alpha_immediate(drawable, 0xFF);

    ui_drawable_set_scale_factor(ui, drawable, 1.f);
    ui_drawable_set_color_mod(drawable, 1.f);

    Drawable_t *prev_relative = NULL;
    if ( prev_active >= 0 ) {
        prev_relative = view->line_drawables->data[prev_active];
    }

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

        view->layout_dirty = true;
    } else if ( prev_relative != drawable->layout.relative_to ) {
        drawable->layout.relative_to = prev_relative;
        ui_reposition_drawable(ui, drawable);

        view->layout_dirty = true;
    }
}

static int32_t calculate_alpha(const int32_t distance) {
    return 225 - 200 / LINE_FADE_MAX_DISTANCE * MIN(distance, LINE_FADE_MAX_DISTANCE);
}

static int32_t calculate_distance(const LyricsView_t *view, const int32_t index, const int32_t prev_active) {
    int32_t distance = abs(index - prev_active);
    if ( distance == 1 ) {
        return 0;
    }
    if ( distance > 0 ) {
        distance -= 1;
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

    return MAX(0, distance);
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

        if ( prev_state == LINE_NONE ) {
            ui_drawable_set_scale_factor_immediate(ui, drawable, LINE_SCALE_FACTOR_INACTIVE);
        } else {
            ui_drawable_set_scale_factor(ui, drawable, LINE_SCALE_FACTOR_INACTIVE);
        }
        ui_drawable_set_color_mod(drawable, LINE_COLOR_MOD_INACTIVE);
        ui_reposition_drawable(ui, drawable);

        view->layout_dirty = true;
    } else if ( view->layout_dirty ) {
        ui_reposition_drawable(ui, drawable);
    }
}

static void set_line_hidden(Ui_t *ui, LyricsView_t *view, const int32_t index) {
    Drawable_t *drawable = view->line_drawables->data[index];

    const LineState_t new_state = LINE_HIDDEN;
    if ( view->line_states[index] != new_state ) {
        view->line_states[index] = new_state;
        drawable->layout.relative_to = NULL;
        drawable->layout.offset_y = -LINE_VERTICAL_PADDING;
        drawable->layout.flags |= LAYOUT_ANCHOR_BOTTOM_Y;

        if ( drawable->layout.flags & LAYOUT_RELATION_Y_INCLUDE_HEIGHT ) {
            drawable->layout.flags ^= LAYOUT_RELATION_Y_INCLUDE_HEIGHT;
        }

        ui_drawable_set_scale_factor(ui, drawable, LINE_SCALE_FACTOR_INACTIVE);
        ui_drawable_set_color_mod(drawable, LINE_COLOR_MOD_INACTIVE);
    }

    // Allow users to scroll up and see the past lyrics. if it's not scrolled, just fade to 0 as normal
    if ( view->container->viewport_y < SCROLL_THRESHOLD ) {
        ui_drawable_set_alpha(drawable, 0);
    } else {
        int32_t distance =
            calculate_distance(view, index, view->current_active_index); // abs(view->current_active_index - (int32_t)index);
        if ( is_line_intermission(view, view->current_active_index) ) {
            distance = LINE_FADE_MAX_DISTANCE;
        }
        ui_drawable_set_alpha(drawable, calculate_alpha(distance));
    }
}

static void set_line_almost_hidden(Ui_t *ui, LyricsView_t *view, const int32_t index) {
    Drawable_t *drawable = view->line_drawables->data[index];

    const LineState_t new_state = LINE_ALMOST_HIDDEN;
    if ( view->line_states[index] != new_state ) {
        if ( view->line_states[index] == LINE_ACTIVE ) {
            // Don't do anything, just fade into a low alpha
            // TODO: Animate this color change
            // ui_drawable_set_color_mod(drawable, LINE_COLOR_MOD_INACTIVE);
            const int32_t alpha = calculate_alpha(LINE_FADE_MAX_DISTANCE - 1);
            ui_drawable_set_alpha(drawable, alpha);
        } else {
            // Position the same as the drawable
            drawable->layout.relative_to = NULL;
            drawable->layout.offset_y = 0;
            if ( drawable->layout.flags & LAYOUT_ANCHOR_BOTTOM_Y ) {
                drawable->layout.flags ^= LAYOUT_ANCHOR_BOTTOM_Y;
            }
            ui_reposition_drawable(ui, drawable);

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
    ui_reposition_drawable(ui, drawable);

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
            set_line_hidden(ui, view, i);
        }
    }

    view->current_active_index = prev_active;

    // Now do a reverse loop setting all the hidden lines to stack on top of each other
    stack_hidden_line_recursive(ui, view, 0);

    view->prev_viewport_y = view->container->viewport_y;
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
    free(view);
}
