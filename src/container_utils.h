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
// Frees data the vector allocated. This means just the storage for the pointers it holds, NOT the data you stored with it. That should be freed separately
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

#endif // ETSUKO_CONTAINER_UTILS_H
