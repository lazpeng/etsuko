/**
 * repository.h - Responsible for the loading of dynamic assets when running under webassembly, fetching data from
 * remote repositories
 */

#ifndef ETSUKO_REPOSITORY_H
#define ETSUKO_REPOSITORY_H

#include <stdint.h>

typedef enum LoadStatus_t {
    LOAD_NOT_STARTED = 0,
    LOAD_IN_PROGRESS,
    LOAD_DONE,
    LOAD_FINISHED
} LoadStatus_t;

typedef struct Load_t {
    LoadStatus_t status;
    const unsigned char *data;
    int data_size;
    char *filename;
    uint64_t downloaded;
    uint64_t total_size;
} Load_t;

void repository_get_resource(const char *src, const char *subdir, Load_t *load);
void repository_free_resource(Load_t *load);

#endif // ETSUKO_REPOSITORY_H
