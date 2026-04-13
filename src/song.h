/**
 * song.h - Defines the layout and parsing routines for the format the application uses for representing songs and its lyrics
 */

#ifndef ETSUKO_SONG_H
#define ETSUKO_SONG_H

#include <stdbool.h>
#include <stdint.h>

#include "constants.h"
#include "container_utils.h"

#define MAX_TIMINGS_PER_LINE (24)

/**
 * Represents a timing segment inside a line. This can be a single character, a syllable, a word or the entire line.
 */
typedef struct Song_LineTiming_t {
    // Byte index in the line string
    int32_t start_idx, end_idx;
    // Unicode character index in the line string
    int32_t start_char_idx, end_char_idx;
    // Duration in seconds for the whole segment
    double duration;
    // Sum of all the durations of segments before this one in the line it is a part of
    double cumulative_duration;
} Song_LineTiming_t;

// Tells how to align the lyric line relative to its container
typedef enum Song_LineAlignment_t { SONG_LINE_LEFT = 0, SONG_LINE_CENTER, SONG_LINE_RIGHT } Song_LineAlignment_t;

// Background type that can be specified in the song
typedef enum Song_BgType_t {
    BG_SIMPLE_GRADIENT = 0,
    BG_SOLID,
    BG_SANDS_GRADIENT,
    BG_RANDOM_GRADIENT,
    BG_AM_LIKE_GRADIENT,
} Song_BgType_t;

// Represents a part of reading hint that gets assigned to a portion of text of the original lyric text
typedef struct Song_LineReading_t {
    // Unicode character index setting the bounds this reading hint is relative of
    size_t start_ch_idx, end_ch_idx;
    // The actual reading hint text
    OWNING char *reading_text;
} Song_LineReading_t;

/**
 * Represents an entire line of lyric. Not to be confused with a visual line, as these are broken dynamically depending on screen
 * size. Instead this maps to individual lines defined in the song file
 */
typedef struct Song_Line_t {
    OWNING char *full_text;
    double base_start_time, base_duration;
    Song_LineTiming_t timings[MAX_TIMINGS_PER_LINE];
    int32_t num_timings;
    Song_LineAlignment_t alignment;
    OWNING Vector_t *readings; // Of Song_LineReading_t
} Song_Line_t;

typedef struct Song_Language_t {
    OWNING char *language;
    WEAK const char *description;
    OWNING Vector_t *lines; // of Song_Line_t*
    OWNING Vector_t *temp_readings; // of const char *
    bool is_default;
    // Whether the lyric language has reading hints
    bool has_reading_info;
    // Whether the song has timed lyrics at all
    bool has_timings;
    // Whether the song has segment timings (parts of the same line)
    bool has_sub_timings;
} Song_Language_t;

// The actual song definition and options
typedef struct Song_t {
    OWNING char *name, *translated_name, *artist, *album;
    int year;
    OWNING Vector_t *languages;
    OWNING char *id;
    OWNING char *file_path, *album_art_path;
    OWNING char *credits;
    OWNING char *karaoke, *language, *hidden;
    Song_LineAlignment_t line_alignment;
    // Default bg color
    uint32_t bg_color;
    // Secondary color to the primary one, used in the simple gradient effect
    uint32_t bg_color_secondary;
    // Time offset to compensate for when displaying the lyrics. To be used when the original timings have some kind of delay
    double time_offset;
    // Override of a font file to be used for the lyrics in particular
    OWNING char *font_override;
    Song_BgType_t bg_type;
    /**
     * When this is enabled, add a single sub-timing with the same duration as the line itself when none is provided.
     * This enables some of the dynamic fill options to work (linear will, full-word won't)
     */
    bool assume_full_sub_timing_when_absent;
} Song_t;

// Loads a song info from the given src buffer. Filename is only useful for assigning an id to the song
void song_load(const char *filename, const char *src, int src_size);
// Returns the current active song. Only applicable for the karaoke operating mode
Song_t *song_get(void);
// Frees the current active song
void song_destroy(void);

typedef struct MenuSong_t {
    OWNING char *id, *name, *artist, *album;
    OWNING char *album_art_path;
    OWNING char *tags;
    OWNING char *language;
    int year;
} MenuSong_t;


#endif // ETSUKO_SONG_H
