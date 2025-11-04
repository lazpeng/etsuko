/**
 * ui_ex.h - Extensions with business logic to the routines defined in ui.h
 */

#ifndef ETSUKO_RENDERER_EX_H
#define ETSUKO_RENDERER_EX_H

#include "song.h"
#include "ui.h"

typedef enum LineState_t {
    LINE_INACTIVE,
    LINE_ACTIVE,
    LINE_ALMOST_HIDDEN,
    LINE_HIDDEN,
} LineState_t;

typedef struct etsuko_LyricsView_t {
    etsuko_Container_t *container;
    const etsuko_Song_t *song;
    Vector_t *line_drawables;
    int32_t current_active_index;
    LineState_t line_states[MAX_SONG_LINES];
    bool active_changed;
    double prev_viewport_y;
} etsuko_LyricsView_t;

etsuko_LyricsView_t *ui_ex_make_lyrics_view(etsuko_Container_t *parent, const etsuko_Song_t *song);
void ui_ex_lyrics_view_loop(etsuko_LyricsView_t *view);
void ui_ex_lyrics_view_on_scroll(const etsuko_LyricsView_t *view, double delta_y);
void ui_ex_destroy_lyrics_view(etsuko_LyricsView_t *view);

#endif // ETSUKO_RENDERER_EX_H
