#include "repository.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "error.h"
#include "str_utils.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>

#include "secret.h" // defines CDN_BASE_PATH

#ifndef CDN_BASE_PATH
#error "No base URL defined for the CDN to fetch songs from"
#endif

static void on_fetch_success(emscripten_fetch_t *fetch) {
    Load_t *job = fetch->userData;
    unsigned char *new_buffer = calloc(1, (int)fetch->numBytes);
    memcpy(new_buffer, fetch->data, fetch->numBytes);
    job->data = new_buffer;
    job->data_size = (int)fetch->numBytes;

    job->status = LOAD_DONE;
    emscripten_fetch_close(fetch);
}

static void on_fetch_failure(emscripten_fetch_t *fetch) {
    puts(fetch->statusText);
    emscripten_fetch_close(fetch);
    error_abort("Failed to fetch resource");
}

static void on_fetch_ready(emscripten_fetch_t *fetch) {
    Load_t *job = fetch->userData;
    job->downloaded = fetch->dataOffset;
    job->total_size = fetch->totalBytes;
}

#else

const unsigned char *load_file(const char *filename, int *size) {
    FILE *file = fopen(filename, "rb");
    if ( file == NULL ) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    const int file_size = (int)ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char *data = malloc(file_size);
    fread(data, 1, file_size, file);
    fclose(file);

    *size = file_size;
    return data;
}

#endif

void repository_get_resource(const char *src, const char *subdir, Load_t *load) {
    if ( load == NULL ) {
        error_abort("Load job is NULL");
    }
    if ( load->data != NULL ) {
        error_abort("Load job already has data");
    }

    mkdir("assets", 0777);
    load->filename = str_get_filename(src);
    load->status = LOAD_IN_PROGRESS;
#ifdef __EMSCRIPTEN__
    char *full_path;
    if ( subdir != NULL ) {
        asprintf(&full_path, "%s/%s/%s", CDN_BASE_PATH, subdir, src);
    } else {
        asprintf(&full_path, "%s/%s", CDN_BASE_PATH, src);
    }
    printf("full path: '%s'\n", full_path);
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = on_fetch_success;
    attr.onerror = on_fetch_failure;
    attr.onreadystatechange = on_fetch_ready;
    attr.userData = load;

    emscripten_fetch(&attr, full_path);
    free(full_path);
#else
    char *path;
    asprintf(&path, "assets/%s", str_get_filename(src));
    load->data = load_file(path, &load->data_size);
    if ( load->data == NULL )
        error_abort("Failed to load resource");
    load->status = LOAD_DONE;
#endif
}

void repository_free_resource(Load_t *load) {
    if ( load->data != NULL ) {
        free((void *)load->data);
    }
    free(load->filename);
    load->filename = NULL;
    load->data = NULL;
    load->data_size = 0;
}
