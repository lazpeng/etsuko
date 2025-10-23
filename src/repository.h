/**
 * repository.h - Responsible for the loading of dynamic assets when running under webassembly, fetching data from
 * remote repositories
 */

#ifndef ETSUKO_REPOSITORY_H
#define ETSUKO_REPOSITORY_H

typedef enum {
    LOAD_NOT_STARTED = 0,
    LOAD_IN_PROGRESS,
    LOAD_DONE,
    LOAD_FINISHED
} etsuko_LoadStatus_t;

typedef struct {
    etsuko_LoadStatus_t status;
    char *destination;
} etsuko_Load_t;

void repository_get_resource(const char *src, const char *subdir, etsuko_Load_t *load);

#endif // ETSUKO_REPOSITORY_H
