/**
 * repository.h - Responsible for the loading of dynamic assets when running under webassembly, fetching data from
 * remote repositories
 */

#ifndef ETSUKO_REPOSITORY_H
#define ETSUKO_REPOSITORY_H

#include "constants.h"

#include <stdint.h>

typedef enum LoadStatus_t {
    LOAD_NOT_STARTED = 0,
    LOAD_IN_PROGRESS,
    LOAD_DONE,
    LOAD_ERROR,
} LoadStatus_t;

typedef struct ResourceBuffer_t {
    OWNING unsigned char *data;
    uint64_t total_bytes, downloaded_bytes;
    uint64_t data_capacity;
} ResourceBuffer_t;

struct Resource_t;
typedef void (*f_resource_loaded_ptr)(const struct Resource_t *res);

typedef struct Resource_t {
    LoadStatus_t status;
    OWNING char *original_filename;
    OWNING ResourceBuffer_t *buffer;
    WEAK MAYBE_NULL f_resource_loaded_ptr on_resource_loaded;
    WEAK MAYBE_NULL void *custom_data;
    bool streaming;
} Resource_t;

typedef struct LoadRequest_t {
    WEAK const char *relative_path;
    WEAK const char *sub_dir;
    bool streaming;
    WEAK MAYBE_NULL f_resource_loaded_ptr on_resource_loaded;
    WEAK MAYBE_NULL void *custom_data;
} LoadRequest_t;

Resource_t *repo_load_resource(const LoadRequest_t *request);
void repo_resource_buffer_leak(Resource_t *resource);
void repo_resource_destroy(Resource_t *resource);
void repo_resource_buffer_destroy(ResourceBuffer_t *buffer);

#endif // ETSUKO_REPOSITORY_H
