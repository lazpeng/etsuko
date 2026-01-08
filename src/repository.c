#include "repository.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "error.h"
#include "str_utils.h"

#define DEFAULT_BUFFER_CAP (64)

static void append_data_to_buffer(ResourceBuffer_t *buffer, const char *data, const uint64_t data_size) {
    if ( buffer == NULL )
        error_abort("append_data_to_buffer: buffer is NULL");

    if ( buffer->data == NULL ) {
        buffer->data = calloc(1, DEFAULT_BUFFER_CAP);
        buffer->data_capacity = DEFAULT_BUFFER_CAP;
    }

    if ( buffer->data_capacity < buffer->downloaded_bytes + data_size ) {
        const uint64_t new_cap = MAX(buffer->data_capacity * 2, buffer->data_capacity + data_size);
        unsigned char *new_buf = realloc(buffer->data, new_cap);
        if ( new_buf == NULL ) {
            printf("Failed to realloc buffer at %llu bytes\n", new_cap);
            error_abort("Failed to realloc resource buffer");
        }
        buffer->data = new_buf;
        buffer->data_capacity = new_cap;
    }

    memcpy(buffer->data + buffer->downloaded_bytes, data, data_size);
    buffer->downloaded_bytes += data_size;
}

#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>

#include "secret.h" // defines CDN_BASE_PATH

#ifndef CDN_BASE_PATH
#error "No base URL defined for the CDN to fetch songs from"
#endif

static void on_fetch_success(emscripten_fetch_t *fetch) {
    Resource_t *resource = fetch->userData;
    if ( !resource->streaming && fetch->numBytes > 0 ) {
        append_data_to_buffer(resource->buffer, fetch->data, fetch->numBytes);
        if ( resource->on_resource_loaded != NULL ) {
            resource->on_resource_loaded(resource);
        }
    }
    if ( resource->buffer->total_bytes == 0 ) {
        error_abort("Fatal: on_fetch_success: total bytes for the resource returned 0");
    }
    if ( resource->buffer->downloaded_bytes > resource->buffer->total_bytes ) {
        error_abort("Fatal: on_fetch_success: downloaded bytes exceed total bytes reported by the server");
    }
    if ( resource->buffer->downloaded_bytes != resource->buffer->total_bytes ) {
        error_abort("Maybe wrong: on_fetch_success: downloaded bytes different than reported by the server in total bytes");
    }

    resource->status = LOAD_DONE;
    emscripten_fetch_close(fetch);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static void on_fetch_progress(emscripten_fetch_t *fetch) {
    if ( fetch->numBytes == 0 )
        return;

    const Resource_t *resource = fetch->userData;
    append_data_to_buffer(resource->buffer, fetch->data, fetch->numBytes);
}

static void on_fetch_failure(emscripten_fetch_t *fetch) {
    Resource_t *resource = fetch->userData;
    resource->status = LOAD_ERROR;
    printf("Fetch failed for '%s': %s\n", resource->original_filename, fetch->statusText);
    emscripten_fetch_close(fetch);
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static void on_fetch_ready(emscripten_fetch_t *fetch) {
    Resource_t *resource = fetch->userData;
    resource->buffer->total_bytes = fetch->totalBytes;

    if ( resource->streaming && resource->on_resource_loaded != NULL ) {
        resource->on_resource_loaded(resource);
    }
}

#else

char *load_file(const char *filename, uint64_t *size) {
    FILE *file = fopen(filename, "rb");
    if ( file == NULL ) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    const int file_size = (int)ftell(file);
    fseek(file, 0, SEEK_SET);
    char *data = malloc(file_size);
    fread(data, 1, file_size, file);
    fclose(file);

    *size = file_size;
    return data;
}

#endif

Resource_t *repo_load_resource(const LoadRequest_t *request) {
    if ( str_is_empty(request->relative_path) ) {
        error_abort("Invalid path passed to repo_load_resource");
    }

    Resource_t *resource = calloc(1, sizeof(*resource));
    resource->on_resource_loaded = request->on_resource_loaded;
    resource->status = LOAD_IN_PROGRESS;
    resource->original_filename = str_get_filename(request->relative_path);
    resource->custom_data = request->custom_data;

    resource->buffer = calloc(1, sizeof(*resource->buffer));
    if ( resource->buffer == NULL )
        error_abort("Failed to allocate buffer for resource");

    resource->buffer->data = NULL;
    resource->buffer->data_capacity = 0;
    resource->buffer->downloaded_bytes = 0;
    resource->buffer->total_bytes = 0;

#ifdef __EMSCRIPTEN__
    StrBuffer_t *path_buf = str_buf_init();
    str_buf_append(path_buf, CDN_BASE_PATH, NULL);
    str_buf_append_ch(path_buf, '/');
    if ( !str_is_empty(request->sub_dir) ) {
        str_buf_append(path_buf, request->sub_dir, NULL);
    }
    str_buf_append(path_buf, request->relative_path, NULL);
    printf("relative path: %s sub_dir: %s buf: %s\n", request->relative_path, request->sub_dir, path_buf->data);

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    if ( request->streaming )
        attr.attributes |= EMSCRIPTEN_FETCH_STREAM_DATA;
    attr.onsuccess = on_fetch_success;
    attr.onerror = on_fetch_failure;
    attr.onreadystatechange = on_fetch_ready;
    attr.onprogress = on_fetch_progress;
    attr.userData = resource;

    emscripten_fetch(&attr, path_buf->data);
    str_buf_destroy(path_buf);
#else
    StrBuffer_t *path_buf = str_buf_init();
    str_buf_append(path_buf, "assets/", NULL);
    str_buf_append(path_buf, resource->original_filename, NULL);

    uint64_t file_size = 0;
    char *file_data = load_file(path_buf->data, &file_size);
    str_buf_destroy(path_buf);
    if ( file_data == NULL )
        error_abort("Failed to load resource");

    append_data_to_buffer(resource->buffer, file_data, file_size);
    free(file_data);

    resource->status = LOAD_DONE;

    if ( resource->on_resource_loaded != NULL ) {
        resource->on_resource_loaded(resource);
    }
#endif

    return resource;
}

void repo_resource_buffer_leak(Resource_t *resource) { resource->buffer = NULL; }

void repo_resource_destroy(Resource_t *resource) {
    if ( resource != NULL ) {
        if ( resource->buffer != NULL ) {
            repo_resource_buffer_destroy(resource->buffer);
        }
        if ( resource->original_filename != NULL ) {
            free(resource->original_filename);
        }
        free(resource);
    }
}

void repo_resource_buffer_destroy(ResourceBuffer_t *buffer) {
    if ( buffer != NULL ) {
        if ( buffer->data != NULL ) {
            free(buffer->data);
        }
        free(buffer);
    }
}
