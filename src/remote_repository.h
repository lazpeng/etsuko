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

#endif
