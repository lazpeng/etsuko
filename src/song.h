/**
 * song.h - Defines the layout and parsing routines for the format the application uses for representing songs and its lyrics
 */

#ifndef ETSUKO_SONG_H
#define ETSUKO_SONG_H

#include <stdint.h>

#include "constants.h"
#include "container_utils.h"

#define MAX_TIMINGS_PER_LINE (24)

typedef struct Song_LineTiming_t {
    int32_t start_idx, end_idx;
    int32_t start_char_idx;
    int32_t end_char_idx;
    double duration;
    double cumulative_duration;
} Song_LineTiming_t;

typedef enum Song_LineAlignment_t {
    SONG_LINE_LEFT = 0,
    SONG_LINE_CENTER,
    SONG_LINE_RIGHT
} Song_LineAlignment_t;

typedef enum Song_BgType_t {
    BG_SIMPLE_GRADIENT = 0,
    BG_SOLID,
    BG_SANDS_GRADIENT,
    BG_RANDOM_GRADIENT,
    BG_AM_LIKE_GRADIENT,
    BG_CLOUD_GRADIENT,
} Song_BgType_t;

typedef struct Song_LineReading_t {
    size_t start_ch_idx, end_ch_idx;
    OWNING char *reading_text;
} Song_LineReading_t;

typedef struct Song_Line_t {
    OWNING char *full_text;
    double base_start_time, base_duration;
    Song_LineTiming_t timings[MAX_TIMINGS_PER_LINE];
    int32_t num_timings;
    Song_LineAlignment_t alignment;
    OWNING Vector_t *readings; // Of Song_LineReading_t
} Song_Line_t;

typedef struct Song_t {
    // Data about the song
    OWNING char *name, *translated_name, *artist, *album;
    int year;
    OWNING Vector_t *lyrics_lines; // of Song_Line_t
    // Meta data
    OWNING char *id;
    OWNING char *file_path, *album_art_path;
    OWNING char *credits;
    OWNING char *karaoke, *language, *hidden;
    Song_LineAlignment_t line_alignment;
    uint32_t bg_color;
    uint32_t bg_color_secondary;
    double time_offset;
    OWNING char *font_override;
    Song_BgType_t bg_type;
    bool has_sub_timings;
    bool has_reading_info;
    /**
     * When this is enabled, add a single sub-timing with the same duration as the line itself when none is provided.
     * This enables some of the dynamic fill options to work (linear will, full-word won't)
     */
    bool assume_full_sub_timing_when_absent;
} Song_t;

void song_load(const char *filename, const char *src, int src_size);
Song_t *song_get(void);
void song_destroy(void);

#endif // ETSUKO_SONG_H
