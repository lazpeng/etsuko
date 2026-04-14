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

// 512 lines ought to be enough for anybody
#define MAX_SONG_LINES 512

// A state for every lyric line in the karaoke state machine
typedef enum LineState_t {
    // Transient state. Just before it is properly initialized
    LINE_NONE = 0,
    // These are lines that are yet to come and are displayed below the current line in sequence
    LINE_INACTIVE,
    // The active line(s)
    LINE_ACTIVE,
    /**
     * When an active ends its duration but the start time of another line still hasn't come,
     * it sits slightly faded (the same as the first inactive line) in the same place
     */
    LINE_ALMOST_HIDDEN,
    // Lines that have passed and replaced by another active line. Depending on config, this could mean it stacks on top of the current line, or disappears completely
    LINE_HIDDEN,
} LineState_t;

typedef struct LyricsLanguage_t {
    OWNING const char *language_str;
    WEAK Song_Language_t *song_language;
    OWNING Vector_t *line_drawables;
    OWNING Vector_t *line_read_hints;
    int32_t current_active_index;
    int32_t current_first_active_index;
    LineState_t line_states[MAX_SONG_LINES];
    OWNING Drawable_t *credit_separator, *credits_prefix, *credits_content;
    uint32_t active_line_segment_visited[MAX_TIMINGS_PER_LINE];
} LyricsLanguage_t;

// Holds the state for the karaoke lyric container
typedef struct etsuko_LyricsView_t {
    OWNING Container_t *container;
    WEAK const Song_t *song;
    OWNING Vector_t *lyrics_languages;
    WEAK LyricsLanguage_t *selected_language;
    int32_t current_hovered_index;
} LyricsView_t;

// Initializes the lyric view
LyricsView_t *ui_ex_make_lyrics_view(Ui_t *ui, Container_t *parent, const Song_t *song);
// Updates the lyric line state machine, computing timing and effects like the dynamic fill and pulse
void ui_ex_lyrics_view_loop(LyricsView_t *view);
// Called when the screen's dimensions change
void ui_ex_lyrics_view_on_screen_change(const LyricsView_t *view);
// Frees data related to the lyrics view
void ui_ex_destroy_lyrics_view(LyricsView_t *view);
// Reset container scroll to the active line
void ui_ex_lyrics_view_scroll_to_active(const LyricsView_t *view);
// Change the current language of the displayed lyrics
void ui_ex_lyrics_view_set_language(LyricsView_t *view, const char *language);

#endif // ETSUKO_RENDERER_EX_H
