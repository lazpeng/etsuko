#include "container_utils.h"

#include <stdio.h>
#include <string.h>

#include "constants.h"
#include "error.h"

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
    memset((uintptr_t *)new_data + vec->capacity, 0, (capacity - vec->capacity) * sizeof(void *));
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
