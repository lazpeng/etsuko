/**
 * remote_repository.h - Fetches resources over HTTP(S) on the desktop build when they are not available locally
 */

#ifndef ETSUKO_REMOTE_REPOSITORY_H
#define ETSUKO_REMOTE_REPOSITORY_H

#include "repository.h"

/**
 * Loads the given resource from a remote server, even in the desktop build (actually, only in the desktop build...)
 * so that we can fallback to it when the resource isn't available locally, or just force it anyway for some of the
 * features like the song list json endpoint, which isn't a file in the first place (but could be now that I think about
 * it writing this documentation, and it would have saved me from writing this code)
 */
void remote_load_resource(const char *path, Resource_t *resource);

/**
 * Marks a resource whose fetch is still running as abandoned, so remote_poll_completions frees it once the fetch is
 * done instead of calling the callback. Returns false if no fetch is running on it, in which case the caller frees it as
 * usual
 */
bool remote_resource_abandon(Resource_t *resource);

/**
 * Takes every fetch the background threads finished since the last call and, on the calling thread, publishes its status
 * and runs its callback, or frees it if it was abandoned. Fetch threads never call back into the application themselves,
 * so this is what keeps callbacks on the main thread. Called through repo_poll
 */
void remote_poll_completions(void);

#endif
