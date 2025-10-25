/**
 * song.h - Defines the layout and parsing routines for the format the application uses for representing songs and its lyrics
 */

#ifndef ETSUKO_SONG_H
#define ETSUKO_SONG_H

#include <stdint.h>

#include "constants.h"
#include "container_utils.h"

typedef struct etsuko_SongLineTiming_t {
    int32_t end_idx;
    double start_time, duration;
} etsuko_SongLineTiming_t;

typedef struct etsuko_SongLine_t {
    char *full_text;
    double base_start_time, base_duration;
    etsuko_SongLineTiming_t timings[MAX_TIMINGS_PER_LINE];
    int32_t num_timings;
} etsuko_SongLine_t;

typedef enum etsuko_Song_LineAlignment_t {
    SONG_LINE_CENTER = 0,
    SONG_LINE_LEFT = 1,
} etsuko_Song_LineAlignment_t;

typedef struct {
    // Data about the song
    char *name, *translated_name, *artist, *album;
    int year;
    Vector_t *lyrics_lines;
    // Meta data
    char *id;
    char *file_path, *album_art_path;
    char *karaoke, *language, *hidden;
    etsuko_Song_LineAlignment_t line_alignment;
    uint32_t bg_color;
} etsuko_Song_t;

void song_load(const char *src);
etsuko_Song_t *song_get(void);
void song_destroy(void);

#endif // ETSUKO_SONG_H
