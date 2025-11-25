#include "song.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "error.h"
#include "str_utils.h"

static Song_t *g_song;

typedef enum { BLOCK_HEADER = 0, BLOCK_LYRICS, BLOCK_TIMINGS, BLOCK_ASS } BlockType;

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
    } else if ( strncmp(buffer, "bgColorSecondary", equals) == 0 ) {
        song->bg_color_secondary = strtol(value, NULL, 16);
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
        song->time_offset = strtod(value, NULL);
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
        } else if ( strncmp(value, "amLike", 6) == 0 ) {
            song->bg_type = BG_AM_LIKE_GRADIENT;
        } else if ( strncmp(value, "cloud", 5) == 0 ) {
            song->bg_type = BG_CLOUD_GRADIENT;
        } else {
            error_abort("Invalid background type for the song");
        }
    } else if ( strncmp(buffer, "writtenBy", equals) == 0 ) {
        song->credits = value;
    }
}

static void read_lyrics_opts(Song_Line_t *line, const char *opts) {
    const char *comma = strchr(opts, ',');
    const size_t end = comma == NULL ? strlen(opts) : comma - opts;

    if ( end == 0 )
        return;

    const char *equals = strchr(opts, '=');
    if ( equals != NULL ) {
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

    if ( comma != NULL ) {
        read_lyrics_opts(line, comma + 1);
    }
}

static void read_lyrics(const Song_t *song, const char *buffer) {
    if ( song->lyrics_lines->size == 0 ) {
        error_abort("Lyrics were placed before the timings");
    }

    // Find the first that full_text is NULL
    for ( size_t i = 0; i < song->lyrics_lines->size; i++ ) {
        Song_Line_t *line = song->lyrics_lines->data[i];
        if ( line->full_text == NULL ) {
            const char *hash = strchr(buffer, '#');
            const size_t end = hash == NULL ? strlen(buffer) : (hash - buffer);
            line->full_text = strndup(buffer, end);
            if ( hash != NULL ) {
                read_lyrics_opts(line, hash + 1);
            }
            return;
        }
    }
}

static double convert_timing(const char *str, const size_t len) {
    const char *colon = strchr(str, ':');
    char *minutes_str = strndup(str, colon - str);
    const double minutes = strtod(minutes_str, NULL);
    free(minutes_str);
    char *seconds_str = strndup(colon + 1, len - (colon - str));
    const double seconds = strtod(seconds_str, NULL);
    free(seconds_str);

    return minutes * 60.0 + seconds;
}

static void read_timings(const Song_t *song, const char *buffer) {
    Song_Line_t *line = calloc(1, sizeof(*line));

    const char *comma = strchr(buffer, ',');
    const size_t start_len = comma == NULL ? strlen(buffer) : (comma - buffer);

    line->base_start_time = convert_timing(buffer, start_len);
    if ( song->lyrics_lines->size > 0 ) {
        Song_Line_t *last_line = song->lyrics_lines->data[song->lyrics_lines->size - 1];
        if ( last_line->base_duration == 0.0 ) {
            last_line->base_duration = line->base_start_time - last_line->base_start_time;
        }
    }
    if ( comma != NULL ) {
        const double end = convert_timing(comma + 1, strlen(comma + 1));
        line->base_duration = end - line->base_start_time;
    }

    line->alignment = song->line_alignment;
    vec_add(song->lyrics_lines, line);
}

static void read_ass_line_content(Song_t *song, Song_Line_t *line, const char *start, const char *end) {
    if ( end == NULL )
        end = start + strlen(start);

    line->alignment = song->line_alignment;

    const int32_t brace = str_find(start, '{', 0, (int32_t)(end - start));
    if ( brace < 0 ) {
        // No sub timings
        const size_t len = end - start;
        line->full_text = strndup(start, len);
        // return early
        return;
    }

    song->has_sub_timings = true;
    const Song_LineTiming_t *prev = NULL;
    
    const char *ptr = start;
    StrBuffer_t *buffer = str_buf_init();
    while ( ptr != end ) {
        if ( *ptr != '{' ) {
            printf("value of *ptr: %s value of start: %s\n", ptr, start);
            error_abort("Invalid sub timing: *start does not start with a {");
        }
        // Now we read a centisecond value from inside the braces that tell us for how long this part of the string
        // should remain highlighted from the base start offset
        const int32_t closing_brace = str_find(ptr, '}', 1+2, (int32_t)(end-ptr));
        // Unfortunately we have to dup this shit because the stdlib is dumb
        char *dup = strndup(ptr+1+2, closing_brace-1-2); // Also skip 2 chars which is the \k before the number
        const int64_t ms = strtoll(dup, NULL, 10);
        free(dup);

        if ( line->num_timings >= MAX_TIMINGS_PER_LINE ) {
            error_abort("Number of max timings per line exceeded. Maybe consider increasing this number");
        }
        Song_LineTiming_t *timing = &line->timings[line->num_timings++];
        timing->duration = (double)ms / 100.0;
        timing->cumulative_duration = prev != NULL ? prev->cumulative_duration + prev->duration : 0;
        // Find when this sub timing ends
        const int32_t next_brace = str_find(ptr, '{', 1, (int32_t)(end-ptr));
        const char *prev_ptr = ptr;
        ptr = next_brace < 0 ? end : ptr + next_brace;
        timing->start_idx = prev != NULL ? prev->end_idx : 0;
        timing->end_idx = timing->start_idx + (int32_t)(ptr - (prev_ptr + 1 + closing_brace));
        // Copy the line contents into the buffer
        str_buf_append(buffer, prev_ptr+1+closing_brace, ptr);

        prev = timing;
    }

    line->full_text = strndup(buffer->data, buffer->len);
    str_buf_destroy(buffer);
}

static void read_ass(Song_t *song, const char *buffer) {
    if ( str_is_empty(buffer) )
        return;

    // VERY naively processes a .ass dialogue line as if it's completely correct and sanitized
    Song_Line_t *line = calloc(1, sizeof(*line));
    // Find the first : which is after Dialogue: and the first colon, and the first argument
    // AND the hours portion of the first timing, which we ignore
    const char *comma = strchr(buffer, ',');
    const char *colon = strchr(comma + 1, ':');
    // The next comma which denotes the start of the end timing
    comma = strchr(colon, ',');
    const double start_timing = convert_timing(colon + 1, comma - colon + 1);
    // The next comma after that
    comma = strchr(colon + 1, ',');
    colon = strchr(comma + 1, ':');
    const double end_timing = convert_timing(colon + 1, comma - colon + 1);
    line->base_start_time = start_timing;
    line->base_duration = end_timing - line->base_start_time;

    // Now we skip more 7 commas in order to get to the final text
    const int commas_to_skip = 7;
    for ( int i = 0; i < commas_to_skip; i++ ) {
        comma = strchr(comma + 1, ',');
    }

    // Now what's left is the actual line text
    // Before, set the end to wherever is the properties part (or NULL if this line doesn't have any)
    const char *properties = strchr(comma + 1, '#');
    read_ass_line_content(song, line, comma + 1, properties);
    // If we have any properties, read those now
    if ( properties != NULL ) {
        read_lyrics_opts(line, properties + 1);
    }

    vec_add(song->lyrics_lines, line);
}

void song_load(const char *filename, const char *src, const int src_size) {
    g_song = calloc(1, sizeof(*g_song));
    g_song->lyrics_lines = vec_init();

    g_song->id = strdup(filename);

    char *buffer = calloc(1, BUFFER_SIZE);
    if ( buffer == NULL )
        error_abort("Failed to allocate buffer");
    size_t offset = 0;

    BlockType current_block = BLOCK_HEADER;
    size_t bytes_read = 0;
    while ( (bytes_read = str_buffered_read(buffer, BUFFER_SIZE, src, src_size, offset)) > 0 ) {
        offset += bytes_read;
        buffer[strcspn(buffer, "\r")] = 0;
        buffer[strcspn(buffer, "\n")] = 0;
        const size_t len = strnlen(buffer, BUFFER_SIZE);
        if ( len > 0 && buffer[0] == '#' ) {
            if ( strncmp(buffer, "#timings", BUFFER_SIZE) == 0 ) {
                current_block = BLOCK_TIMINGS;
            } else if ( strncmp(buffer, "#lyrics", BUFFER_SIZE) == 0 ) {
                current_block = BLOCK_LYRICS;
            } else if ( strncmp(buffer, "#ass", BUFFER_SIZE) == 0 ) {
                current_block = BLOCK_ASS;
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
        case BLOCK_ASS:
            read_ass(g_song, buffer);
            break;
        }
    }

    if ( g_song->lyrics_lines->size > 0 ) {
        // Since the last line will have a 0 duration, set it here to a reasonable number so we can see the last line
        ((Song_Line_t *)g_song->lyrics_lines->data[g_song->lyrics_lines->size - 1])->base_duration = 100.0;
    }
}

Song_t *song_get(void) { return g_song; }

void song_destroy(void) {
    if ( g_song != NULL ) {
        // Free lyrics lines
        for ( size_t i = 0; i < g_song->lyrics_lines->size; i++ ) {
            free(((Song_Line_t *)g_song->lyrics_lines->data[i])->full_text);
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
        if ( g_song->font_override != NULL ) {
            free(g_song->font_override);
        }
        if ( g_song->credits != NULL ) {
            free(g_song->credits);
        }
        // Free the song
        free(g_song);
    }
    g_song = NULL;
}
