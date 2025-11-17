#include "str_utils.h"

#include <string.h>

#include "constants.h"

#define MAX_STRLEN (1024)

int32_t str_find(const char *src, const char c, const int32_t start, int32_t max_len) {
    const int32_t size = (int32_t)strnlen(src, MAX_STRLEN);
    if ( max_len < 0 )
        max_len = size;

    if ( start >= size )
        return -1;

    for ( int32_t i = start; i < MIN(max_len, start + max_len); i++ ) {
        if ( src[i] == c )
            return i;
    }

    return -1;
}

char *str_get_filename(const char *path) {
    const char *last_slash = strrchr(path, '/');
    if ( last_slash == NULL ) {
        last_slash = path;
    } else
        last_slash++;
    return strdup(last_slash);
}

char *str_get_filename_no_ext(const char *path) {
    const char *last_slash = strrchr(path, '/');
    if ( last_slash == NULL ) {
        last_slash = path;
    } else
        last_slash++;
    const char *last_dot = strrchr(last_slash, '.');
    if ( last_dot == NULL ) {
        return NULL;
    }
    return strndup(last_slash, last_dot - last_slash);
}

void str_replace_char(char *str, const char old_c, const char new_c) {
    for ( size_t i = 0; i < strnlen(str, MAX_STRLEN); i++ ) {
        if ( str[i] == old_c )
            str[i] = new_c;
    }
}

bool str_is_empty(const char *str) {
    if ( str == NULL )
        return true;
    return strnlen(str, 1) < 1;
}
