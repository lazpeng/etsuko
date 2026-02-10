/**
 * container_utils.h - Defines a few useful containers to deal with dynamic allocated data
 */

#ifndef ETSUKO_CONTAINER_UTILS_H
#define ETSUKO_CONTAINER_UTILS_H
#include <stdlib.h>

#ifdef __EMSCRIPTEN__
#include <stdint.h>
#endif

#include "constants.h"

/**
 * A poorly made dynamic list that holds pointers to void.
 * It behaves weirdly if you try to store something else than a pointer here, so don't.
 * You should know what you are storing and not store more than one kind of object in this.
 */
typedef struct Vector_t {
    OWNING void **data;
    size_t size, capacity;
} Vector_t;

// Creates the vector with default capacity
Vector_t *vec_init(void);
// Frees data the vector allocated. This means just the storage for the pointers it holds, NOT the data you stored with it. That
// should be freed separately
void vec_destroy(Vector_t *v);
// Resizes the internal capacity if the given capacity is larger than the current one
void vec_reserve(Vector_t *vec, size_t capacity);
// Adds an element and resizes the internal data if needed
void vec_add(Vector_t *vec, void *data);
/**
 * Removes an element at a specific index. the other elements are relocated so an empty space is not left on the vector.
 * So you should not call this in a loop (this codebase does it anyway)
 */
void vec_remove(Vector_t *vec, size_t index);
// "removes" all elements from the vector
void vec_clear(Vector_t *vec);

typedef struct HashEntry_t {
    OWNING char *key;
    void *value;
    OWNING struct HashEntry_t *next;
} HashEntry_t;

typedef struct HashMap_t {
    OWNING HashEntry_t **buckets;
    size_t size;
    size_t capacity;
} HashMap_t;

// Creates a hashmap
HashMap_t *map_init(void);
// Destroys the hashmap and frees keys. Values are NOT freed.
void map_destroy(HashMap_t *map);
// Puts a value into the map. Key is copied.
void map_put(HashMap_t *map, const char *key, void *value);
// Gets a value from the map. Returns NULL if not found.
void *map_get(HashMap_t *map, const char *key);
// Removes an element from the map.
void map_remove(HashMap_t *map, const char *key);
// Clears the map
void map_clear(HashMap_t *map);

typedef void (*map_iterator_func)(const char *key, void *value, void *user_data);

// Iterates over all elements in the map
void map_iterate(HashMap_t *map, map_iterator_func func, void *user_data);

#endif // ETSUKO_CONTAINER_UTILS_H
