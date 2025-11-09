#include "song.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "error.h"
#include "str_utils.h"

static Song_t *g_song;

typedef enum { BLOCK_HEADER = 0, BLOCK_LYRICS, BLOCK_TIMINGS } BlockType;

static void read_header(Song_t *song, const char *buffer, const size_t length) {
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
        song->year = (int)strtol(value, nullptr, 10);
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
        song->bg_color = strtol(value, nullptr, 16);
        free(value);
    } else if ( strncmp(buffer, "bgColorSecondary", equals) == 0 ) {
        song->bg_color_secondary = strtol(value, nullptr, 16);
        free(value);
    } else if ( strncmp(buffer, "alignment", equals) == 0 ) {
        if ( strncmp(value, "left", 4) == 0 ) {
            song->line_alignment = SONG_LINE_LEFT;
        } else if ( strncmp(value, "center", 5) == 0 ) {
            song->line_alignment = SONG_LINE_CENTER;
        } else if ( strncmp(value, "right", 5) == 0 ) {
            song->line_alignment = SONG_LINE_RIGHT;
        } else {
            error_abort("Invalid song line alignment");
        }
    } else if ( strncmp(buffer, "offset", equals) == 0 ) {
        song->time_offset = strtod(value, nullptr);
        free(value);
    } else if ( strncmp(buffer, "fontOverride", equals) == 0 ) {
        song->font_override = value;
    } else if ( strncmp(buffer, "bgType", equals) == 0 ) {
        if ( strncmp(value, "simpleGradient", 14) == 0 ) {
            song->bg_type = BG_SIMPLE_GRADIENT;
        } else if ( strncmp(value, "solid", 5) == 0 ) {
            song->bg_type = BG_SOLID;
        } else if ( strncmp(value, "dynamicGradient", 15) == 0 ) {
            song->bg_type = BG_DYNAMIC_GRADIENT;
        } else if ( strncmp(value, "randomGradient", 14) == 0 ) {
            song->bg_type = BG_RANDOM_GRADIENT;
        } else {
            error_abort("Invalid background type for the song");
        }
    }
}

static void read_lyrics_opts(Song_Line_t *line, const char *opts) {
    const char *comma = strchr(opts, ',');
    const size_t end = comma == nullptr ? strlen(opts) : comma - opts;

    if ( end == 0 )
        return;

    const char *equals = strchr(opts, '=');
    if ( equals != nullptr ) {
        if ( strncmp(opts, "alignment", equals - opts) == 0 ) {
            if ( strncmp(equals + 1, "left", 4) == 0 ) {
                line->alignment = SONG_LINE_LEFT;
            } else if ( strncmp(equals + 1, "center", 5) == 0 ) {
                line->alignment = SONG_LINE_CENTER;
            } else if ( strncmp(equals + 1, "right", 5) == 0 ) {
                line->alignment = SONG_LINE_RIGHT;
            } else {
                error_abort("Invalid song line opt alignment");
            }
        }
    } // else TODO: Not supported non-key-value options

    if ( comma != nullptr ) {
        read_lyrics_opts(line, comma + 1);
    }
}

static void read_lyrics(const Song_t *song, const char *buffer) {
    if ( song->lyrics_lines->size == 0 ) {
        error_abort("Lyrics were placed before the timings");
    }

    // Find the first that full_text is nullptr
    for ( size_t i = 0; i < song->lyrics_lines->size; i++ ) {
        Song_Line_t *line = song->lyrics_lines->data[i];
        if ( line->full_text == nullptr ) {
            const char *hash = strchr(buffer, '#');
            const size_t end = hash == nullptr ? strlen(buffer) : (hash - buffer);
            line->full_text = strndup(buffer, end);
            if ( hash != nullptr ) {
                read_lyrics_opts(line, hash + 1);
            }
            return;
        }
    }
}

static double convert_timing(const char *str, const size_t len) {
    const char *colon = strchr(str, ':');
    char *minutes_str = strndup(str, colon - str);
    const double minutes = strtod(minutes_str, nullptr);
    free(minutes_str);
    char *seconds_str = strndup(colon + 1, len - (colon - str));
    const double seconds = strtod(seconds_str, nullptr);
    free(seconds_str);

    return minutes * 60.0 + seconds;
}

static void read_timings(const Song_t *song, const char *buffer) {
    Song_Line_t *line = calloc(1, sizeof(*line));

    const char *comma = strchr(buffer, ',');
    const size_t start_len = comma == nullptr ? strlen(buffer) : (comma - buffer);

    line->base_start_time = convert_timing(buffer, start_len);
    if ( song->lyrics_lines->size > 0 ) {
        Song_Line_t *last_line = song->lyrics_lines->data[song->lyrics_lines->size - 1];
        if ( last_line->base_duration == 0.0 ) {
            last_line->base_duration = line->base_start_time - last_line->base_start_time;
        }
    }
    if ( comma != nullptr ) {
        const double end = convert_timing(comma + 1, strlen(comma + 1));
        line->base_duration = end - line->base_start_time;
    }

    line->alignment = song->line_alignment;
    vec_add(song->lyrics_lines, line);
}

void song_load(const char *src) {
    FILE *file = fopen(src, "r");
    if ( file == nullptr ) {
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
            read_timings(g_song, buffer);
            break;
        }
    }

    if ( g_song->lyrics_lines->size > 0 ) {
        // Since the last line will have a 0 duration, set it here to a reasonable number so we can see the last line
        ((Song_Line_t *)g_song->lyrics_lines->data[g_song->lyrics_lines->size - 1])->base_duration = 100.0;
    }
}

Song_t *song_get() { return g_song; }

void song_destroy() {
    if ( g_song != nullptr ) {
        // Free lyrics lines
        for ( size_t i = 0; i < g_song->lyrics_lines->size; i++ ) {
            free(((Song_Line_t *)g_song->lyrics_lines->data[i])->full_text);
            free(g_song->lyrics_lines->data[i]);
        }
        vec_destroy(g_song->lyrics_lines);
        // Free strings
        if ( g_song->id != nullptr ) {
            free(g_song->id);
        }
        if ( g_song->name != nullptr ) {
            free(g_song->name);
        }
        if ( g_song->translated_name != nullptr ) {
            free(g_song->translated_name);
        }
        if ( g_song->album != nullptr ) {
            free(g_song->album);
        }
        if ( g_song->artist != nullptr ) {
            free(g_song->artist);
        }
        if ( g_song->karaoke != nullptr ) {
            free(g_song->karaoke);
        }
        if ( g_song->language != nullptr ) {
            free(g_song->language);
        }
        if ( g_song->hidden != nullptr ) {
            free(g_song->hidden);
        }
        if ( g_song->file_path != nullptr ) {
            free(g_song->file_path);
        }
        if ( g_song->album_art_path != nullptr ) {
            free(g_song->album_art_path);
        }
        if ( g_song->font_override != nullptr ) {
            free(g_song->font_override);
        }
        // Free the song
        free(g_song);
    }
    g_song = nullptr;
}
