#include "song.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "str_utils.h"

static Song_t *g_song;

typedef enum { BLOCK_HEADER = 0, BLOCK_LYRICS, BLOCK_TIMINGS, BLOCK_ASS, BLOCK_READINGS, BLOCK_UNKNOWN } BlockType;

static void read_header(Song_t *song, const char *buffer, const size_t length) {
    const size_t equals = strcspn(buffer, "=");
    if ( equals >= length )
        return;
    char *value = strdup(buffer + equals + 1);

    if ( str_equals_sized(buffer, "name", equals) ) {
        song->name = value;
    } else if ( str_equals_sized(buffer, "translatedName", equals) ) {
        song->translated_name = value;
    } else if ( str_equals_sized(buffer, "album", equals) ) {
        song->album = value;
    } else if ( str_equals_sized(buffer, "artist", equals) ) {
        song->artist = value;
    } else if ( str_equals_sized(buffer, "year", equals) ) {
        song->year = (int)strtol(value, NULL, 10);
        free(value);
    } else if ( str_equals_sized(buffer, "karaoke", equals) ) {
        song->karaoke = value;
    } else if ( str_equals_sized(buffer, "language", equals) ) {
        song->language = value;
    } else if ( str_equals_sized(buffer, "hidden", equals) ) {
        song->hidden = value;
    } else if ( str_equals_sized(buffer, "albumArt", equals) ) {
        song->album_art_path = value;
    } else if ( str_equals_sized(buffer, "filePath", equals) ) {
        song->file_path = value;
    } else if ( str_equals_sized(buffer, "bgColor", equals) ) {
        song->bg_color = strtol(value, NULL, 16);
        free(value);
    } else if ( str_equals_sized(buffer, "bgColorSecondary", equals) ) {
        song->bg_color_secondary = strtol(value, NULL, 16);
        free(value);
    } else if ( str_equals_sized(buffer, "alignment", equals) ) {
        if ( str_equals_right_sized(value, "left") ) {
            song->line_alignment = SONG_LINE_LEFT;
        } else if ( str_equals_right_sized(value, "center") ) {
            song->line_alignment = SONG_LINE_CENTER;
        } else if ( str_equals_right_sized(value, "right") ) {
            song->line_alignment = SONG_LINE_RIGHT;
        } else {
            printf("Invalid song line alignment: %s\n", value);
        }
        free(value);
    } else if ( str_equals_sized(buffer, "offset", equals) ) {
        song->time_offset = strtod(value, NULL);
        free(value);
    } else if ( str_equals_sized(buffer, "fontOverride", equals) ) {
        song->font_override = value;
    } else if ( str_equals_sized(buffer, "bgType", equals) ) {
        if ( str_equals_right_sized(value, "simpleGradient") ) {
            song->bg_type = BG_SIMPLE_GRADIENT;
        } else if ( str_equals_right_sized(value, "solid") ) {
            song->bg_type = BG_SOLID;
        } else if ( str_equals_right_sized(value, "sands") ) {
            song->bg_type = BG_SANDS_GRADIENT;
        } else if ( str_equals_right_sized(value, "randomGradient") ) {
            song->bg_type = BG_RANDOM_GRADIENT;
        } else if ( str_equals_right_sized(value, "amLike") ) {
            song->bg_type = BG_AM_LIKE_GRADIENT;
        } else {
            printf("Invalid background type: %s\n", value);
        }
        free(value);
    } else if ( str_equals_sized(buffer, "writtenBy", equals) ) {
        song->credits = value;
    } else if ( str_equals_sized(buffer, "assumeFullSubTiming", equals) ) {
        song->assume_full_sub_timing_when_absent = str_equals_right_sized(value, "yes");
        free(value);
    } else {
        char *option_name = strndup(buffer, equals);
        printf("Unrecognized option: %s\n", option_name);
        free(option_name);
        free(value);
    }
}

static void read_lyrics_opts(Song_Line_t *line, const char *opts) {
    const char *comma = strchr(opts, ',');
    const size_t end = comma == NULL ? strlen(opts) : comma - opts;

    if ( end == 0 )
        return;

    const char *equals = strchr(opts, '=');
    if ( equals != NULL ) {
        if ( str_equals_sized(opts, "alignment", equals - opts) ) {
            equals = equals + 1;
            if ( str_equals_right_sized(equals, "left") ) {
                line->alignment = SONG_LINE_LEFT;
            } else if ( str_equals_right_sized(equals, "center") ) {
                line->alignment = SONG_LINE_CENTER;
            } else if ( str_equals_right_sized(equals, "right") ) {
                line->alignment = SONG_LINE_RIGHT;
            } else {
                error_abort("Invalid song line opt alignment");
            }
        }
    }

    if ( comma != NULL ) {
        read_lyrics_opts(line, comma + 1);
    }
}

static Song_Line_t *make_line(void) {
    Song_Line_t *line = calloc(1, sizeof(*line));
    line->readings = vec_init();
    return line;
}

static void read_lyrics(const Song_Language_t *lang, const char *buffer, const size_t index) {
    Song_Line_t *line;
    if ( index < lang->lines->size ) {
        line = lang->lines->data[index];
    } else {
        line = make_line();
    }

    const char *hash = strchr(buffer, '#');
    const size_t end = hash == NULL ? strlen(buffer) : (hash - buffer);
    line->full_text = strndup(buffer, end);
    if ( hash != NULL ) {
        read_lyrics_opts(line, hash + 1);
    }

    if ( index >= lang->lines->size )
        vec_add(lang->lines, line);
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

static void read_timings(const Song_t *song, const Song_Language_t *lang, const char *buffer, const size_t index) {
    Song_Line_t *line;
    if ( index < lang->lines->size ) {
        line = lang->lines->data[index];
    } else {
        line = make_line();
    }

    const char *comma = strchr(buffer, ',');
    const size_t start_len = comma == NULL ? strlen(buffer) : (comma - buffer);

    line->base_start_time = convert_timing(buffer, start_len);
    if ( index > 0 ) {
        Song_Line_t *last_line = lang->lines->data[index - 1];
        if ( last_line->base_duration == 0.0 ) {
            last_line->base_duration = line->base_start_time - last_line->base_start_time;
        }
    }
    if ( comma != NULL ) {
        const double end = convert_timing(comma + 1, strlen(comma + 1));
        line->base_duration = end - line->base_start_time;
    }

    line->alignment = song->line_alignment;
    if ( index >= lang->lines->size )
        vec_add(lang->lines, line);
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
        if ( song->assume_full_sub_timing_when_absent ) {
            Song_LineTiming_t *timing = &line->timings[line->num_timings++];
            timing->duration = line->base_duration;
            timing->start_idx = 0;
            timing->end_idx = (int32_t)len;
            timing->cumulative_duration = 0;
        }
        // return early
        return;
    }

    song->has_sub_timings = true;
    const Song_LineTiming_t *prev = NULL;

    const char *ptr = start;
    StrBuffer_t *buffer = str_buf_init();
    while ( ptr != end ) {
        if ( *ptr != '{' ) {
            error_abort("Invalid sub timing: *start does not start with a {");
        }
        // Now we read a centisecond value from inside the braces that tell us for how long this part of the string
        // should remain highlighted from the base start offset
        const int32_t closing_brace = str_find(ptr, '}', 1 + 2, (int32_t)(end - ptr));
        // Unfortunately we have to dup this shit because the stdlib is dumb
        char *dup = strndup(ptr + 1 + 2, closing_brace - 1 - 2); // Also skip 2 chars which is the \k before the number
        const int64_t cs = strtoll(dup, NULL, 10);
        free(dup);

        if ( line->num_timings >= MAX_TIMINGS_PER_LINE ) {
            error_abort("Number of max timings per line exceeded. Maybe consider increasing this number");
        }
        Song_LineTiming_t *timing = &line->timings[line->num_timings++];
        timing->duration = (double)cs / 100.0;
        timing->cumulative_duration = prev != NULL ? prev->cumulative_duration + prev->duration : 0;
        // Find when this sub timing ends
        const int32_t next_brace = str_find(ptr, '{', 1, (int32_t)(end - ptr));
        const char *prev_ptr = ptr;
        ptr = next_brace < 0 ? end : ptr + next_brace;
        timing->start_idx = prev != NULL ? prev->end_idx : 0;
        timing->end_idx = timing->start_idx + (int32_t)(ptr - (prev_ptr + 1 + closing_brace));
        // Char counts
        timing->start_char_idx = prev != NULL ? prev->end_char_idx : 0;
        // wtf is this shit
        timing->end_char_idx = timing->start_char_idx +
                               str_u8_count(prev_ptr + 1 + closing_brace, 0, (int32_t)(ptr - (prev_ptr + 1 + closing_brace)));
        // Copy the line contents into the buffer
        str_buf_append(buffer, prev_ptr + 1 + closing_brace, ptr);

        prev = timing;
    }

    line->full_text = strndup(buffer->data, buffer->len);
    str_buf_destroy(buffer);
}

static void read_ass(Song_t *song, const Song_Language_t *lang, const char *buffer) {
    if ( str_is_empty(buffer) )
        return;

    // VERY naively processes a .ass dialogue line as if it's completely correct and sanitized
    Song_Line_t *line = make_line();
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

    vec_add(lang->lines, line);
}

static void read_readings(const Song_Language_t *lang, const char *buffer, const int32_t len, const int32_t index) {
    if ( len == 0 )
        return;

    // Format of the line is
    // 1 - Lines are in the same exact order as the (original) lyrics
    // 2 - We'll have pairs in the same order as the original line, where a bunch of characters in whatever language
    // will map to a string of ascii characters that tell us how those characters are read (this is mostly for asian languages,
    // or ones that use a writing system other than the latin alphabet).
    // 3 - Pairs are in original=reading form, accepting spaces in both the original and reading portions
    // and 4 - Separated by commas.
    //
    // Any parts of the line that are not declared in this segment will not be shown up on the software

    // Where to find the given part of the lyric
    int32_t lyric_idx = 0;

    if ( (size_t)index >= lang->lines->size ) {
        error_abort("read_readings: There are more reading hint lines than lyrics");
    }

    const Song_Line_t *line = lang->lines->data[index];
    const int32_t lyric_len = (int32_t)strlen(line->full_text);

    int32_t start = 0;
    while ( start < len - 1 ) {
        int32_t end = str_find(buffer, ',', start, len);
        if ( end < 0 )
            end = (int32_t)len;

        const int32_t eq = str_find(buffer, '=', start, end);
        if ( eq < 0 )
            goto end;

        const int32_t idx = str_u8_find_str(line->full_text, buffer + start, lyric_idx, lyric_len, eq - start);
        const int32_t part_count = str_u8_count(buffer, start, eq);

        Song_LineReading_t *reading = calloc(1, sizeof(*reading));
        reading->start_ch_idx = str_u8_count(line->full_text, 0, idx);
        reading->end_ch_idx = reading->start_ch_idx + part_count;
        reading->reading_text = strndup(buffer + eq + 1, end - eq - 1);
        vec_add(line->readings, reading);

        lyric_idx = idx + (eq - start);
    end:
        start = end + 1;
    }
}

static Song_Language_t *select_language(const Song_t *song, const char *language, const bool is_default) {
    Song_Language_t *target = NULL;

    for ( size_t i = 0; i < song->languages->size; i++ ) {
        Song_Language_t *current = song->languages->data[i];
        if ( str_equals(current->language, language) ) {
            target = current;
            break;
        }
    }

    if ( target == NULL ) {
        target = calloc(1, sizeof(*target));
        target->language = strdup(language);
        target->lines = vec_init();
        target->temp_readings = vec_init();
        target->is_default = is_default;
        vec_add(song->languages, target);
    }

    return target;
}

void song_load(const char *filename, const char *src, const int src_size) {
    g_song = calloc(1, sizeof(*g_song));
    g_song->languages = vec_init();

    g_song->id = strdup(filename);

    // This controls whether the lyrics portion of the song is already
    bool has_lyrics = false;

    bool old_lyrics_compat = false;
    BlockType current_block = BLOCK_HEADER;
    int32_t block_line_index = 0;
    StrBuffer_t *str_buffer = str_buf_init();
    Song_Language_t *language = NULL;

    int32_t offset = 0;
    while ( true ) {
        str_buf_clear(str_buffer);
        const int32_t bytes_read = str_buf_append_line(str_buffer, src, src_size, offset);
        if ( bytes_read <= 0 )
            break;

        offset += bytes_read;
        const size_t len = str_buffer->len;
        const char *buffer = str_buffer->data;

        if ( len > 0 && buffer[0] == '#' ) {
            block_line_index = 0;
            const int32_t language_idx = str_find(buffer, ':', 0, (int32_t)strlen(buffer));
            if ( language_idx > 0 ) {
                const char *current_language_str = buffer + language_idx + 1;
                language = select_language(g_song, current_language_str, false);
            } else {
                language = NULL;
            }

            if ( str_equals_sized(buffer, "#timings", 8) ) {
                current_block = BLOCK_TIMINGS;
                g_song->has_timings = true;
            } else if ( str_equals_sized(buffer, "#lyrics", 7) ) {
                old_lyrics_compat = true;
                current_block = BLOCK_LYRICS;
                has_lyrics = true;
            } else if ( str_equals_sized(buffer, "#ass", 4) ) {
                current_block = BLOCK_ASS;
                has_lyrics = true;
                g_song->has_timings = true;
            } else if ( str_equals_sized(buffer, "#readings", 9) ) {
                current_block = BLOCK_READINGS;
                g_song->has_reading_info = true;
            } else {
                printf("Unknown block type: %s\n", buffer);
                current_block = BLOCK_UNKNOWN;
            }
            continue;
        }

        if ( current_block != BLOCK_HEADER && language == NULL ) {
            if ( g_song->language == NULL ) {
                printf("Warning: language is not set in the header, using empty\n");
                g_song->language = "";
            }
            language = select_language(g_song, g_song->language, true);
        }

        switch ( current_block ) {
        case BLOCK_HEADER:
            read_header(g_song, buffer, len);
            break;
        case BLOCK_LYRICS:
            read_lyrics(language, buffer, block_line_index);
            break;
        case BLOCK_TIMINGS:
            read_timings(g_song, language, buffer, block_line_index);
            break;
        case BLOCK_ASS:
            read_ass(g_song, language, buffer);
            break;
        case BLOCK_READINGS:
            if ( has_lyrics ) {
                // Process readings directly
                read_readings(language, buffer, (int32_t)len, block_line_index);
            } else if (language != NULL ) {
                vec_add(language->temp_readings, strdup(buffer));
            }
            break;
        case BLOCK_UNKNOWN:
            break;
        }

        block_line_index += 1;
    }

    str_buf_destroy(str_buffer);

    const Song_Language_t *default_language = select_language(g_song, g_song->language, true);
    // Compat: Add lyrics lines from the original language
    g_song->lyrics_lines = default_language->lines;

    // Process things that have been postponed to until we have all the lyrics
    for ( size_t i = 0; i < g_song->languages->size; i++ ) {
        Song_Language_t *lang = g_song->languages->data[i];
        for ( size_t j = 0; j < lang->temp_readings->size; j++ ) {
            char *line = lang->temp_readings->data[j];
            read_readings(lang, line, (int32_t)strlen(line), (int32_t)i);
            free(line);
        }
        vec_destroy(lang->temp_readings);
        lang->temp_readings = NULL;
    }

    if ( g_song->lyrics_lines->size > 0 && old_lyrics_compat ) {
        // Since the last line will have a 0 duration, set it here to a reasonable number so we can see the last line
        Song_Line_t *line = g_song->lyrics_lines->data[g_song->lyrics_lines->size - 1];
        if ( line->base_duration == 0 )
            line->base_duration = 100.0;
    }
}

Song_t *song_get(void) { return g_song; }

static void free_song_languages(const Song_t *song) {
    for ( size_t i = 0; i < song->languages->size; i++ ) {
        Song_Language_t *language = song->languages->data[i];
        for ( size_t j = 0; j < language->lines->size; j++ ) {
            Song_Line_t *line = language->lines->data[j];
            if ( line->readings != NULL ) {
                for ( size_t h = 0; h < line->readings->size; h++ ) {
                    Song_LineReading_t *reading = line->readings->data[h];
                    free(reading->reading_text);
                    free(reading);
                }
            }
            free(line->full_text);
            free(line);
        }
        vec_destroy(song->lyrics_lines);
        if ( language->temp_readings != NULL ) {
            for ( size_t j = 0; j < language->temp_readings->size; j++ ) {
                free(language->temp_readings->data[j]);
            }
            vec_destroy(language->temp_readings);
        }
        free(language->language);
        free(language);
    }
    vec_destroy(song->languages);
}

void song_destroy(void) {
    if ( g_song != NULL ) {
        // Free lyrics lines
        free_song_languages(g_song);
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
