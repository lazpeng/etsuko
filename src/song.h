/**
 * song.h - Defines the layout and parsing routines for the format the application uses for representing songs and its lyrics
 */

#ifndef ETSUKO_SONG_H
#define ETSUKO_SONG_H

#include "constants.h"
#include "container_utils.h"

typedef struct Song_LineTiming_t {
    int32_t end_idx;
    double start_time, duration;
} Song_LineTiming_t;

typedef enum Song_LineAlignment_t {
    SONG_LINE_LEFT = 0,
    SONG_LINE_CENTER,
    SONG_LINE_RIGHT
} Song_LineAlignment_t;

typedef enum Song_BgType_t {
    BG_SIMPLE_GRADIENT = 0,
    BG_SOLID,
    BG_DYNAMIC_GRADIENT,
    BG_RANDOM_GRADIENT,
} Song_BgType_t;

typedef struct Song_Line_t {
    char *full_text;
    double base_start_time, base_duration;
    Song_LineTiming_t timings[MAX_TIMINGS_PER_LINE];
    int32_t num_timings;
    Song_LineAlignment_t alignment;
} Song_Line_t;

typedef struct Song_t {
    // Data about the song
    char *name, *translated_name, *artist, *album;
    int year;
    Vector_t *lyrics_lines;
    // Meta data
    char *id;
    char *file_path, *album_art_path;
    char *karaoke, *language, *hidden;
    Song_LineAlignment_t line_alignment;
    uint32_t bg_color;
    uint32_t bg_color_secondary;
    double time_offset;
    char *font_override;
    Song_BgType_t bg_type;
} Song_t;

void song_load(const char *src);
Song_t *song_get();
void song_destroy();

#endif // ETSUKO_SONG_H
