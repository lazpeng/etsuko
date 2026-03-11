/**
 * repository.h - Responsible for the loading of dynamic assets when running under webassembly, fetching data from
 * remote repositories
 */

#ifndef ETSUKO_REPOSITORY_H
#define ETSUKO_REPOSITORY_H

#include "constants.h"

#include <stdint.h>

// The status of a resource load request
typedef enum LoadStatus_t {
    LOAD_NOT_STARTED = 0,
    LOAD_IN_PROGRESS,
    LOAD_DONE,
    LOAD_ERROR,
} LoadStatus_t;

/**
 * Holds a partial buffer to the data being streamed by the resource loader
 * In practice, due to limitations in emscripten, the data is only fully loaded once when the call is complete,
 * so this does basically nothing.
 * but it could be repurposed for loading shards using http offsets, enabling streaming of data (the thing this was written
 * to be used as)
 */
typedef struct ResourceBuffer_t {
    OWNING unsigned char *data;
    uint64_t total_bytes, downloaded_bytes;
    // Tracks fetch progress separately from downloaded_bytes, which is the write cursor.
    // On emscripten, the fetch API reports download progress before data is written to the buffer,
    // so this field allows tracking network progress without corrupting the write offset.
    uint64_t fetch_progress_bytes;
    uint64_t data_capacity;
} ResourceBuffer_t;

struct Resource_t;
// Callback when the resource loading is completed
typedef void (*f_resource_loaded_ptr)(const struct Resource_t *res);

// Represents a resource loaded from the local filesystem or from a remote server
typedef struct Resource_t {
    LoadStatus_t status;
    OWNING char *original_filename;
    OWNING ResourceBuffer_t *buffer;
    WEAK MAYBE_NULL f_resource_loaded_ptr on_resource_loaded;
    WEAK MAYBE_NULL void *custom_data;
} Resource_t;

// Info needed to load a resource
typedef struct LoadRequest_t {
    // Path to the resource. In practice, the filename
    WEAK const char *relative_path;
    // Absolute path to the resource. Will not be appended to the cdn url.
    WEAK const char *absolute_path;
    // Sub folder where the resource is located in the remote location
    WEAK const char *sub_dir;
    // Callback on completion
    WEAK MAYBE_NULL f_resource_loaded_ptr on_resource_loaded;
    // Caller custom data
    WEAK MAYBE_NULL void *custom_data;
    // Fetch the resource from a remote repository even if it otherwise exists on the local filesystem
    bool force_remote_fetch;
} LoadRequest_t;

/**
 * Loads a given resource.
 * For emscripten builds, this loads data from the configured cdn path.
 * For desktop builds, this loads from the assets directory located next to the executable (or in the working dir).
 * Due to limitations in emscripten, streaming is not possible
 */
Resource_t *repo_load_resource(const LoadRequest_t *request);
/**
 * Makes the resource "forget" about the resource so it doesn't get freed in the destroy function.
 * Only useful when another component wants to take ownership of the buffer for some reason
 */
void repo_resource_buffer_leak(Resource_t *resource);
// Frees data related to the resource, and its buffer if it was not leaked
void repo_resource_destroy(Resource_t *resource);
// Frees a buffer in particular, useful for when it was leaked earlier and needs to be freed now
void repo_resource_buffer_destroy(ResourceBuffer_t *buffer);
// Appends a sized buffer of data to the resource buffer
void append_data_to_buffer(ResourceBuffer_t *buffer, const char *data, uint64_t data_size);

#endif // ETSUKO_REPOSITORY_H
