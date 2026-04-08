#include "song_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "str_utils.h"

static int cmp_str(const void *a, const void *b) { return strcmp(*(const char **)a, *(const char **)b); }

static int cmp_menu_song_name(const void *a, const void *b) {
    return strcmp((*(const MenuSong_t **)a)->name, (*(const MenuSong_t **)b)->name);
}

static void free_menu_song(MenuSong_t *song) {
    if ( !song )
        return;
    if ( song->id )
        free(song->id);
    if ( song->name )
        free(song->name);
    if ( song->artist )
        free(song->artist);
    if ( song->album )
        free(song->album);
    if ( song->album_art_path )
        free(song->album_art_path);
    if ( song->tags )
        free(song->tags);
    if ( song->language )
        free(song->language);
    free(song);
}

typedef struct {
    const char **keys;
    size_t count;
} CollectKeysCtx_t;

static void collect_keys(const char *key, const void *, void *user_data) {
    CollectKeysCtx_t *ctx = user_data;
    ctx->keys[ctx->count++] = key;
}

static void destroy_artist_vec(const char *, void *value, void *) { vec_destroy(value); }

Vector_t *menu_songs_parse(const char *src) {
    JsonContext_t *ctx = json_ctx_init();
    JsonObject_t *root_obj = json_parse(src, ctx);

    if ( root_obj == NULL ) {
        fprintf(stderr, "DEBUG: json_parse failed\n");
        json_ctx_destroy(ctx);
        return NULL;
    }

    const JsonField_t *data_field = json_obj_get(root_obj, "data");
    const Vector_t *songs_list = json_get_list(data_field);
    if ( songs_list == NULL ) {
        fprintf(stderr, "DEBUG: data list is NULL\n");
        json_obj_destroy(root_obj);
        json_ctx_destroy(ctx);
        return NULL;
    }

    HashMap_t *artist_map = map_init();

    for ( size_t i = 0; i < songs_list->size; i++ ) {
        const JsonField_t *song_field = songs_list->data[i];
        if ( song_field->type != JSON_OBJECT )
            continue;

        JsonObject_t *song_obj = song_field->value.obj_value;

        const char *name = json_get_string(json_obj_get(song_obj, "name"));
        const char *artist = json_get_string(json_obj_get(song_obj, "artist"));
        const char *album = json_get_string(json_obj_get(song_obj, "album"));

        if ( str_is_empty(name) || str_is_empty(artist) || str_is_empty(album) )
            continue;

        MenuSong_t *menu_song = calloc(1, sizeof(MenuSong_t));
        menu_song->name = strdup(name);
        menu_song->artist = strdup(artist);
        menu_song->album = strdup(album);

        const char *id = json_get_string(json_obj_get(song_obj, "id"));
        if ( id )
            menu_song->id = strdup(id);

        const char *album_art = json_get_string(json_obj_get(song_obj, "album_art"));
        if ( album_art )
            menu_song->album_art_path = strdup(album_art);

        const char *tags = json_get_string(json_obj_get(song_obj, "tags"));
        if ( tags )
            menu_song->tags = strdup(tags);

        const char *language = json_get_string(json_obj_get(song_obj, "language"));
        if ( language )
            menu_song->language = strdup(language);

        menu_song->year = (int)json_get_number(json_obj_get(song_obj, "year"));

        Vector_t *artist_vec = map_get(artist_map, artist);
        if ( !artist_vec ) {
            artist_vec = vec_init();
            map_put(artist_map, artist, artist_vec);
        }
        vec_add(artist_vec, menu_song);
    }

    // sort artists alphabetically
    const char **artist_keys = malloc(artist_map->size * sizeof(const char *));
    CollectKeysCtx_t collect_ctx = {.keys = artist_keys, .count = 0};
    map_iterate_const(artist_map, collect_keys, &collect_ctx);
    qsort(artist_keys, collect_ctx.count, sizeof(const char *), cmp_str);

    // Within each artist, sort songs by name
    Vector_t *songs = vec_init();
    for ( size_t i = 0; i < collect_ctx.count; i++ ) {
        const Vector_t *artist_vec = map_get(artist_map, artist_keys[i]);
        qsort(artist_vec->data, artist_vec->size, sizeof(void *), cmp_menu_song_name);
        for ( size_t j = 0; j < artist_vec->size; j++ )
            vec_add(songs, artist_vec->data[j]);
    }

    free(artist_keys);
    map_iterate(artist_map, destroy_artist_vec, NULL);
    map_destroy(artist_map);

    json_obj_destroy(root_obj);
    json_ctx_destroy(ctx);

    return songs;
}

void menu_songs_destroy(Vector_t *songs) {
    if ( !songs )
        return;
    for ( size_t i = 0; i < songs->size; i++ )
        free_menu_song(songs->data[i]);
    vec_destroy(songs);
}
