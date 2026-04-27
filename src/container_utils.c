#include "container_utils.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "error.h"

#define DEFAULT_VEC_CAPACITY (16)
#define DEFAULT_MAP_CAPACITY (16)
#define LOAD_FACTOR_THRESHOLD (0.75)

Vector_t *vec_init(void) {
    Vector_t *v = calloc(1, sizeof(*v));
    if ( v == NULL )
        return NULL;
    v->data = calloc(DEFAULT_VEC_CAPACITY, sizeof(void *));
    if ( v->data == NULL ) {
        free(v);
        return NULL;
    }
    v->capacity = DEFAULT_VEC_CAPACITY;
    return v;
}

void vec_destroy(Vector_t *v) {
    free(v->data);
    free(v);
}

void vec_reserve(Vector_t *vec, const size_t capacity) {
    if ( capacity <= vec->capacity )
        return;

    void *new_data = realloc(vec->data, capacity * sizeof(void *));
    if ( new_data == NULL ) {
        error_abort("Failed to reallocate vector");
    }
    vec->data = new_data;
    vec->capacity = capacity;
}

void vec_add(Vector_t *vec, void *data) {
    if ( vec->size == vec->capacity ) {
        vec_reserve(vec, vec->capacity * 2);
    }
    vec->data[vec->size++] = data;
}

void vec_remove(Vector_t *vec, const size_t index) {
    if ( index == vec->size - 1 ) {
        // Just decrement and set to null
        vec->size--;
        vec->data[index] = NULL;
    } else {
        // Copy all items over from the right to the left
        for ( size_t i = index; i < vec->size - 1; i++ ) {
            vec->data[i] = vec->data[i + 1];
        }
        vec->data[--vec->size] = NULL;
    }
}

void vec_clear(Vector_t *vec) {
    vec->size = 0;
    memset(vec->data, 0, vec->capacity * sizeof(void *));
}

static uint32_t murmur3_32(const char *key, uint32_t len, uint32_t seed) {
    uint32_t c1 = 0xcc9e2d51;
    uint32_t c2 = 0x1b873593;
    uint32_t r1 = 15;
    uint32_t r2 = 13;
    uint32_t m = 5;
    uint32_t n = 0xe6546b64;
    uint32_t h = 0;
    uint32_t k = 0;
    uint8_t *d = (uint8_t *)key; // 32 bit extract from the key
    const uint32_t *chunks = NULL;
    const uint8_t *tail = NULL; // tail - last 8 bytes
    int i = 0;
    int l = len / 4; // chunk length

    h = seed;

    chunks = (const uint32_t *)(d + l * 4); // body
    tail = (const uint8_t *)(d + l * 4);    // last 8 byte chunk of key

    for ( i = -l; i != 0; ++i ) {
        k = chunks[i];

        k *= c1;
        k = (k << r1) | (k >> (32 - r1));
        k *= c2;

        h ^= k;
        h = (h << r2) | (h >> (32 - r2));
        h = h * m + n;
    }

    k = 0;

    // remainder
    switch ( len & 3 ) {
    case 3:
        k ^= (tail[2] << 16);
        [[fallthrough]];
    case 2:
        k ^= (tail[1] << 8);
        [[fallthrough]];
    case 1:
        k ^= tail[0];
        k *= c1;
        k = (k << r1) | (k >> (32 - r1));
        k *= c2;
        h ^= k;
    }

    h ^= len;

    h ^= (h >> 16);
    h *= 0x85ebca6b;
    h ^= (h >> 13);
    h *= 0xc2b2ae35;
    h ^= (h >> 16);

    return h;
}

static uint32_t hash_string(const char *str) { return murmur3_32(str, (uint32_t)strlen(str), 0); }

HashMap_t *map_init(void) {
    HashMap_t *map = calloc(1, sizeof(HashMap_t));
    if ( !map )
        error_abort("Failed to allocate HashMap");

    map->capacity = DEFAULT_MAP_CAPACITY;
    map->buckets = calloc(map->capacity, sizeof(HashEntry_t *));
    if ( !map->buckets ) {
        free(map);
        error_abort("Failed to allocate HashMap buckets");
    }
    return map;
}

void map_clear(HashMap_t *map) {
    if ( !map )
        error_abort("map_clear: Map is NULL");
    for ( size_t i = 0; i < map->capacity; i++ ) {
        HashEntry_t *entry = map->buckets[i];
        while ( entry ) {
            HashEntry_t *next = entry->next;
            free(entry->key);
            free(entry);
            entry = next;
        }
        map->buckets[i] = NULL;
    }
    map->size = 0;
}

void map_destroy(HashMap_t *map) {
    if ( !map )
        error_abort("map_destroy: Map is NULL");
    map_clear(map);
    free(map->buckets);
    free(map);
}

static void map_resize(HashMap_t *map) {
    size_t new_capacity = map->capacity * 2;
    HashEntry_t **new_buckets = calloc(new_capacity, sizeof(HashEntry_t *));
    if ( !new_buckets )
        error_abort("Failed to reallocate HashMap buckets for resize");

    for ( size_t i = 0; i < map->capacity; i++ ) {
        HashEntry_t *entry = map->buckets[i];
        while ( entry ) {
            HashEntry_t *next = entry->next;

            // Rehash
            uint32_t hash = hash_string(entry->key);
            size_t index = hash % new_capacity;

            entry->next = new_buckets[index];
            new_buckets[index] = entry;

            entry = next;
        }
    }

    free(map->buckets);
    map->buckets = new_buckets;
    map->capacity = new_capacity;
}

void map_put(HashMap_t *map, const char *key, void *value) {
    if ( map == NULL )
        error_abort("map_put: Map is NULL");
    if ( key == NULL )
        error_abort("map_put: Key is NULL");

    if ( (double)map->size / map->capacity >= LOAD_FACTOR_THRESHOLD ) {
        map_resize(map);
    }

    uint32_t hash = hash_string(key);
    size_t index = hash % map->capacity;

    HashEntry_t *entry = map->buckets[index];
    while ( entry ) {
        if ( strcmp(entry->key, key) == 0 ) {
            entry->value = value;
            return;
        }
        entry = entry->next;
    }

    HashEntry_t *new_entry = malloc(sizeof(HashEntry_t));
    if ( !new_entry )
        error_abort("Failed to allocate HashEntry");

    new_entry->key = strdup(key);
    if ( !new_entry->key ) {
        free(new_entry);
        error_abort("Failed to allocate key string in map_put");
    }
    new_entry->value = value;
    new_entry->next = map->buckets[index];
    map->buckets[index] = new_entry;
    map->size++;
}

void *map_get(HashMap_t *map, const char *key) {
    if ( map == NULL )
        error_abort("map_get: Map is NULL");
    if ( key == NULL )
        error_abort("map_get: Key is NULL");

    uint32_t hash = hash_string(key);
    size_t index = hash % map->capacity;

    HashEntry_t *entry = map->buckets[index];
    while ( entry ) {
        if ( strcmp(entry->key, key) == 0 ) {
            return entry->value;
        }
        entry = entry->next;
    }
    return NULL;
}

void map_remove(HashMap_t *map, const char *key) {
    if ( map == NULL )
        error_abort("map_remove: Map is NULL");
    if ( key == NULL )
        error_abort("map_remove: Key is NULL");

    uint32_t hash = hash_string(key);
    size_t index = hash % map->capacity;

    HashEntry_t *entry = map->buckets[index];
    HashEntry_t *prev = NULL;

    while ( entry ) {
        if ( strcmp(entry->key, key) == 0 ) {
            if ( prev ) {
                prev->next = entry->next;
            } else {
                map->buckets[index] = entry->next;
            }
            free(entry->key);
            free(entry);
            map->size--;
            return;
        }
        prev = entry;
        entry = entry->next;
    }
}

void map_iterate(const HashMap_t *map, const map_iterator_func func, void *user_data) {
    if ( map == NULL )
        error_abort("map_iterate: Map is NULL");
    if ( func == NULL )
        error_abort("map_iterate: Func is NULL");

    for ( size_t i = 0; i < map->capacity; i++ ) {
        const HashEntry_t *entry = map->buckets[i];
        while ( entry ) {
            const HashEntry_t *next = entry->next;
            func(entry->key, entry->value, user_data);
            entry = next;
        }
    }
}

void map_iterate_const(const HashMap_t *map, const map_iterator_func_const func, void *user_data) {
    if ( map == NULL )
        error_abort("map_iterate: Map is NULL");
    if ( func == NULL )
        error_abort("map_iterate: Func is NULL");

    for ( size_t i = 0; i < map->capacity; i++ ) {
        const HashEntry_t *entry = map->buckets[i];
        while ( entry ) {
            const HashEntry_t *next = entry->next;
            func(entry->key, entry->value, user_data);
            entry = next;
        }
    }
}
