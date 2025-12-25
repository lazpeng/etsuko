/**
 * ui_ex.h - Extensions with business logic to the routines defined in ui.h
 */

#ifndef ETSUKO_RENDERER_EX_H
#define ETSUKO_RENDERER_EX_H

#include <stdint.h>

#include "constants.h"
#include "container_utils.h"
#include "song.h"
#include "ui.h"

typedef enum LineState_t {
    LINE_NONE = 0, // Transient state
    LINE_INACTIVE,
    LINE_ACTIVE,
    LINE_ALMOST_HIDDEN,
    LINE_HIDDEN,
} LineState_t;

typedef struct etsuko_LyricsView_t {
    OWNING Container_t *container;
    WEAK const Song_t *song;
    OWNING Vector_t *line_drawables;  // of Drawable_t
    OWNING Vector_t *line_read_hints; // of Drawable_t
    int32_t current_active_index;
    LineState_t line_states[MAX_SONG_LINES];
    double prev_viewport_y;
    bool layout_dirty;
    OWNING Drawable_t *credit_separator, *credits_prefix, *credits_content;
    uint32_t active_line_segment_visited[MAX_TIMINGS_PER_LINE];
} LyricsView_t;

LyricsView_t *ui_ex_make_lyrics_view(Ui_t *ui, Container_t *parent, const Song_t *song);
void ui_ex_lyrics_view_loop(Ui_t *ui, LyricsView_t *view);
void ui_ex_lyrics_view_on_screen_change(Ui_t *ui, LyricsView_t *view);
void ui_ex_lyrics_view_on_scroll(const LyricsView_t *view, double delta_y);
void ui_ex_destroy_lyrics_view(LyricsView_t *view);

#endif // ETSUKO_RENDERER_EX_H
