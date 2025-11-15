/**
 * container_utils.h - Defines a few useful containers to deal with dynamic allocated data
 */

#ifndef ETSUKO_CONTAINER_UTILS_H
#define ETSUKO_CONTAINER_UTILS_H
#include <stdlib.h>

#ifdef __EMSCRIPTEN__
#include <stdint.h>
#endif

typedef struct Vector_t {
    void **data;
    size_t size, capacity;
} Vector_t;

Vector_t *vec_init(void);
void vec_destroy(Vector_t *v);
void vec_reserve(Vector_t *vec, size_t capacity);
void vec_add(Vector_t *vec, void *data);
void vec_remove(Vector_t *vec, size_t index);
void vec_clear(Vector_t *vec);

#endif // ETSUKO_CONTAINER_UTILS_H
