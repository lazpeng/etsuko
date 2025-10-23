#include "song.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "error.h"
#include "str_utils.h"

static etsuko_Song_t *g_song;

typedef enum { BLOCK_HEADER = 0, BLOCK_LYRICS, BLOCK_TIMINGS } BlockType;

static void read_header(etsuko_Song_t *song, const char *buffer, const size_t length) {
    const size_t equals = strcspn(buffer, "=");
    if ( equals >= length )
        return;
    char *value = strdup(buffer + equals + 1);

    if ( strncmp(buffer, "name", equals) == 0 ) {
        song->name = value;
    } else if ( strncmp(buffer, "translatedName", equals) == 0 ) {
        song->translated_name = value;
    } else if ( strncmp(buffer, "album", equals) == 0 ) {
        song->album = value;
    } else if ( strncmp(buffer, "artist", equals) == 0 ) {
        song->artist = value;
    } else if ( strncmp(buffer, "year", equals) == 0 ) {
        song->year = (int)strtol(value, NULL, 10);
        free(value);
    } else if ( strncmp(buffer, "karaoke", equals) == 0 ) {
        song->karaoke = value;
    } else if ( strncmp(buffer, "language", equals) == 0 ) {
        song->language = value;
    } else if ( strncmp(buffer, "hidden", equals) == 0 ) {
        song->hidden = value;
    } else if ( strncmp(buffer, "albumArt", equals) == 0 ) {
        song->album_art_path = value;
    } else if ( strncmp(buffer, "filePath", equals) == 0 ) {
        song->file_path = value;
    } else if ( strncmp(buffer, "bgColor", equals) == 0 ) {
        song->bg_color = strtol(value, NULL, 16);
        free(value);
    }
}

static void read_lyrics(const etsuko_Song_t *song, const char *buffer) {
    if ( song->lyrics_lines->size == 0 ) {
        error_abort("Lyrics were placed before the timings");
    }

    // Find the first that full_text is null
    for ( size_t i = 0; i < song->lyrics_lines->size; i++ ) {
        etsuko_SongLine_t *line = song->lyrics_lines->data[i];
        if ( line->full_text == NULL ) {
            line->full_text = strdup(buffer);
            return;
        }
    }
}

static void read_timings(const etsuko_Song_t *song, const char *buffer, size_t length) {
    etsuko_SongLine_t *line = calloc(1, sizeof(*line));

    const char *colon = strchr(buffer, ':');
    char *minutes_str = strndup(buffer, colon - buffer);
    const double minutes = strtod(minutes_str, NULL);
    free(minutes_str);
    const double seconds = strtod(colon + 1, NULL);

    line->base_start_time = minutes * 60.0 + seconds;
    if ( song->lyrics_lines->size > 0 ) {
        etsuko_SongLine_t *last_line = song->lyrics_lines->data[song->lyrics_lines->size - 1];
        last_line->base_duration = line->base_start_time - last_line->base_start_time;
    }

    vec_add(song->lyrics_lines, line);
}

void song_load(const char *src) {
    FILE *file = fopen(src, "r");
    if ( file == NULL ) {
        printf("trying to load song from src: %s\n", src);
        error_abort("Failed to open song file");
    }

    g_song = calloc(1, sizeof(*g_song));
    g_song->lyrics_lines = vec_init();

    g_song->id = str_get_filename_no_ext(src);

    char buffer[BUFFER_SIZE];
    buffer[0] = 0;

    BlockType current_block = BLOCK_HEADER;
    while ( fgets(buffer, sizeof(buffer), file) ) {
        buffer[strcspn(buffer, "\r")] = 0;
        buffer[strcspn(buffer, "\n")] = 0;
        const size_t len = strnlen(buffer, sizeof(buffer));
        if ( len > 0 && buffer[0] == '#' ) {
            if ( strncmp(buffer, "#timings", sizeof(buffer)) == 0 ) {
                current_block = BLOCK_TIMINGS;
            } else if ( strncmp(buffer, "#lyrics", sizeof(buffer)) == 0 ) {
                current_block = BLOCK_LYRICS;
            } else {
                error_abort("Invalid block inside song file");
            }
            continue;
        }

        switch ( current_block ) {
        case BLOCK_HEADER:
            read_header(g_song, buffer, len);
            break;
        case BLOCK_LYRICS:
            read_lyrics(g_song, buffer);
            break;
        case BLOCK_TIMINGS:
            read_timings(g_song, buffer, len);
            break;
        }
    }

    if ( g_song->lyrics_lines->size > 0 ) {
        // Since the last line will have a 0 duration, set it here to a reasonable number so we can see the last line
        ((etsuko_SongLine_t *)g_song->lyrics_lines->data[g_song->lyrics_lines->size - 1])->base_duration = 100.0;
    }
}

etsuko_Song_t *song_get(void) { return g_song; }

void song_destroy(void) {
    if ( g_song != NULL ) {
        // Free lyrics lines
        for ( size_t i = 0; i < g_song->lyrics_lines->size; i++ ) {
            free(((etsuko_SongLine_t *)g_song->lyrics_lines->data[i])->full_text);
            free(g_song->lyrics_lines->data[i]);
        }
        vec_destroy(g_song->lyrics_lines);
        // Free strings
        if ( g_song->id != NULL ) {
            free(g_song->id);
        }
        if ( g_song->name != NULL ) {
            free(g_song->name);
        }
        if ( g_song->translated_name != NULL ) {
            free(g_song->translated_name);
        }
        if ( g_song->album != NULL ) {
            free(g_song->album);
        }
        if ( g_song->artist != NULL ) {
            free(g_song->artist);
        }
        if ( g_song->karaoke != NULL ) {
            free(g_song->karaoke);
        }
        if ( g_song->language != NULL ) {
            free(g_song->language);
        }
        if ( g_song->hidden != NULL ) {
            free(g_song->hidden);
        }
        if ( g_song->file_path != NULL ) {
            free(g_song->file_path);
        }
        if ( g_song->album_art_path != NULL ) {
            free(g_song->album_art_path);
        }
        // Free the song
        free(g_song);
    }
    g_song = NULL;
}
