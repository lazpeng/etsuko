/**
 * renderer_ex.h - Extensions with business logic to the routines defined in renderer.h
 */

#ifndef ETSUKO_RENDERER_EX_H
#define ETSUKO_RENDERER_EX_H

#include "renderer.h"
#include "song.h"

typedef struct etsuko_LyricsView_t {
    etsuko_Container_t *container;
    etsuko_Song_t *song;
    Vector_t *line_drawables;
    int32_t current_active_index;
    Vector_t *line_states;
    bool active_changed;
} etsuko_LyricsView_t;

etsuko_LyricsView_t *renderer_ex_make_lyrics_view(etsuko_Container_t *parent, etsuko_Song_t *song);
void renderer_ex_lyrics_view_loop(etsuko_LyricsView_t *view);
void renderer_ex_lyrics_view_destroy(etsuko_LyricsView_t *view);

#endif // ETSUKO_RENDERER_EX_H
