#include "renderer_ex.h"

#include "audio.h"
#include "error.h"

#include <stdio.h>
#include <string.h>

typedef enum LineState_t {
    LINE_INACTIVE,
    LINE_ACTIVE,
    LINE_HIDDEN,
} LineState_t;

#define LINE_VERTICAL_PADDING (30)

static bool is_line_intermission(const etsuko_LyricsView_t *view, const size_t index) {
    const etsuko_SongLine_t *line = view->song->lyrics_lines->data[index];
    return strncmp(line->full_text, "", 1) == 0 && line->base_duration > 1.5;
}

etsuko_LyricsView_t *renderer_ex_make_lyrics_view(etsuko_Container_t *parent, etsuko_Song_t *song) {
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
    view->line_states = vec_init();

    if ( song->lyrics_lines->size == 0 ) {
        error_abort("Song has no lyrics");
    }

    const etsuko_Color_t color = {.r = 100, .b = 100, .g = 100, .a = 200};

    etsuko_Drawable_t *prev = NULL;
    for ( size_t i = 0; i < song->lyrics_lines->size; i++ ) {
        const etsuko_SongLine_t *line = song->lyrics_lines->data[i];

        char *line_text = line->full_text;
        if ( strncmp(line_text, "", 1) == 0 ) {
            if ( is_line_intermission(view, i) ) {
                line_text = "...";
            } else {
                line_text = " ";
            }
        }

        etsuko_DrawableAlignment_t alignment = ALIGN_CENTER;
        etsuko_LayoutFlags_t alignment_flags = LAYOUT_CENTER_X;
        if ( song->line_alignment == SONG_LINE_LEFT || true ) {
            alignment = ALIGN_LEFT;
            alignment_flags = 0;
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
            .flags = alignment_flags | LAYOUT_RELATIVE_TO_Y | LAYOUT_RELATION_Y_INCLUDE_HEIGHT,
        };
        if ( prev != NULL ) {
            layout.relative_to = prev;
        }
        prev = renderer_drawable_make_text(&data, parent, &layout);
        vec_add(view->line_drawables, prev);
    }

    for ( size_t i = 0; i < song->lyrics_lines->size; i++ ) {
        int *state = calloc(1, sizeof(*state));
        *state = LINE_INACTIVE;
        vec_add(view->line_states, state);

        renderer_animate_translation(view->line_drawables->data[i],
                                     &(etsuko_Animation_EaseTranslationData_t){.duration = 0.3, .ease = true});
        renderer_animate_fade(view->line_drawables->data[i], &(etsuko_Animation_FadeInOutData_t){.duration = 1.0});
    }

    return view;
}

static void set_line_active(const etsuko_LyricsView_t *view, const size_t index, const int32_t prev_active) {
    etsuko_Drawable_t *drawable = view->line_drawables->data[index];

    drawable->enabled = true;
    drawable->alpha_mod = 0xFF;

    etsuko_Drawable_TextData_t *data = drawable->custom_data;
    data->em = 2.0;
    data->color = (etsuko_Color_t){.r = 255, .b = 255, .g = 255, .a = 255};
    data->bold = false;

    const LineState_t new_state = LINE_ACTIVE;
    if ( *(LineState_t *)view->line_states->data[index] != new_state ) {
        *(LineState_t *)view->line_states->data[index] = new_state;
        renderer_recompute_drawable(drawable);
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

    renderer_reposition_drawable(drawable);
}

static void set_line_inactive(const etsuko_LyricsView_t *view, const size_t index, const int32_t prev_active) {
    etsuko_Drawable_t *drawable = view->line_drawables->data[index];
    if ( index > 0 ) {
        etsuko_Drawable_t *prev = view->line_drawables->data[index - 1];
        drawable->layout.relative_to = prev;
    }
    drawable->layout.offset_y = LINE_VERTICAL_PADDING;
    if ( drawable->layout.flags & LAYOUT_ANCHOR_BOTTOM_Y ) {
        drawable->layout.flags ^= LAYOUT_ANCHOR_BOTTOM_Y;
    }

    etsuko_Drawable_TextData_t *data = drawable->custom_data;
    data->em = 1.5;
    data->color = (etsuko_Color_t){.r = 100, .b = 100, .g = 100, .a = 255};
    data->bold = false;

    size_t alpha = 200;
    if ( prev_active >= 0 && prev_active != (int32_t)index ) {
        const int max_distance = 5;
        size_t distance = (int32_t)index - prev_active;

        if ( is_line_intermission(view, prev_active) ) {
            // When the current line is an intermission between two segments, make every other line have min alpha
            distance = max_distance;
        }
        alpha = 225 - 200 / max_distance * MIN(distance, max_distance);
    }

    if ( *(LineState_t *)view->line_states->data[index] != LINE_INACTIVE ) {
        // Don't animate
        drawable->alpha_mod = (int32_t)alpha;
    } else {
        // Maybe animate if our alpha changed while still being inactive
        renderer_drawable_set_alpha(drawable, (int32_t)alpha);
    }

    const LineState_t new_state = LINE_INACTIVE;
    if ( *(LineState_t *)view->line_states->data[index] != new_state ) {
        *(LineState_t *)view->line_states->data[index] = new_state;
        renderer_recompute_drawable(drawable);
    } else {
        renderer_reposition_drawable(drawable);
    }
}

static void set_line_hidden(const etsuko_LyricsView_t *view, const size_t index) {
    etsuko_Drawable_t *drawable = view->line_drawables->data[index];

    const LineState_t new_state = LINE_INACTIVE;
    if ( *(LineState_t *)view->line_states->data[index] == LINE_ACTIVE ) {
        // Line went from active to hidden
        *(LineState_t *)view->line_states->data[index] = new_state;
        drawable->layout.relative_to = NULL;
        drawable->layout.offset_y = -LINE_VERTICAL_PADDING;
        drawable->layout.flags |= LAYOUT_ANCHOR_BOTTOM_Y;

        // Animate fade out
        renderer_drawable_set_alpha(drawable, 0);
        renderer_reposition_drawable(drawable);
    }
}

void renderer_ex_lyrics_view_loop(etsuko_LyricsView_t *view) {
    if ( view == NULL ) {
        error_abort("loop: lyrics_view is null");
    }

    view->active_changed = false;

    const double elapsed_time = audio_elapsed_time();
    int32_t prev_active = -1;

    for ( size_t i = 0; i < view->song->lyrics_lines->size; i++ ) {
        const etsuko_SongLine_t *line = view->song->lyrics_lines->data[i];
        if ( elapsed_time < line->base_start_time + line->base_duration ) {
            if ( elapsed_time >= line->base_start_time ) {
                set_line_active(view, i, prev_active);
                prev_active = (int32_t)i;
            } else {
                set_line_inactive(view, i, prev_active);
            }
        } else {
            set_line_hidden(view, i);
        }
    }
}

void renderer_ex_lyrics_view_destroy(etsuko_LyricsView_t *view) {
    if ( view == NULL ) {
        error_abort("destroy: lyrics_view is null");
    }
    // The drawables themselves are owned by the renderer
    vec_destroy(view->line_drawables);
    for ( size_t i = 0; i < view->line_states->size; i++ ) {
        free(view->line_states->data[i]);
    }
    vec_destroy(view->line_states);
    free(view);
}
