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
    const char *output_file = job->destination;

    printf("writing file to: %s\n", output_file);
    FILE *out = fopen(output_file, "wb");
    if ( out == nullptr ) {
        error_abort("Failed to open file\n");
    }
    fwrite(fetch->data, 1, fetch->numBytes, out);
    fflush(out);
    fclose(out);

    job->status = LOAD_DONE;
    //job->destination = output_file;
    emscripten_fetch_close(fetch);
}

static void on_fetch_failure(emscripten_fetch_t *fetch) {
    puts(fetch->statusText);
    emscripten_fetch_close(fetch);
    error_abort("Failed to fetch resource");
}

#endif

void repository_get_resource(const char *src, const char *subdir, Load_t *load) {
    if ( load == nullptr ) {
        error_abort("Load job is nullptr");
    }

    mkdir("assets", 0777);
    char *filename = str_get_filename(src);
    asprintf(&load->destination, "assets/%s", filename);
    free(filename);
    load->status = LOAD_IN_PROGRESS;

    FILE *existing = fopen(load->destination, "r");
    if ( existing != nullptr ) {
        fclose(existing);
        load->status = LOAD_DONE;
        return;
    }
#ifdef __EMSCRIPTEN__
    char *full_path;
    if ( subdir != nullptr ) {
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
    attr.userData = load;

    emscripten_fetch(&attr, full_path);
    free(full_path);
#else
    load->status = LOAD_DONE;
#endif
}
