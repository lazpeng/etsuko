#ifndef ETSUKO_SONG_LIST_H
#define ETSUKO_SONG_LIST_H

#include "container_utils.h"
#include "song.h"

/**
 * Parses a json string with a list of songs to be displayed on the main menu and returns a
 * Vector_t of MenuSong_t, sorted by artist name (A-Z), then by song name (A-Z) within each artist.
 */
Vector_t *menu_songs_parse(const char *src);
void menu_songs_destroy(Vector_t *songs);

#endif // ETSUKO_SONG_LIST_H
