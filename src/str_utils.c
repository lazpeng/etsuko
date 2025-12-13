#include "str_utils.h"

#include <stdlib.h>
#include <string.h>

#include <unicode/utf8.h>

#include "constants.h"
#include "error.h"

#define MAX_STRLEN (2048)
#define STR_BUF_START_CAP (128)

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

int32_t str_u8_find_str(const char *src, const char *sub, const int32_t start, const int32_t max_len, const int32_t sub_len) {
    const int32_t sub_count = str_u8_count(sub, 0, sub_len);
    UChar32 first_sub_c;
    int32_t sub_i = 0;
    U8_NEXT(sub, sub_i, sub_len, first_sub_c);

    int32_t i = 0;
    while ( i < max_len ) {
        const bool skip = i < start;
        const int32_t start_i = i;
        UChar32 c;
        U8_NEXT(src, i, max_len, c);
        if ( skip )
            continue;

        if ( c == first_sub_c ) {
            const int32_t start_sub_i = sub_i;
            bool found = true;

            for ( int32_t count = 1; count < sub_count; count++ ) {
                if ( sub_i >= sub_len - 1 ) {
                    found = false;
                    break;
                }

                UChar32 cur_c, sub_c;
                U8_NEXT(src, i, max_len, cur_c);
                U8_NEXT(sub, sub_i, sub_len, sub_c);

                if ( cur_c != sub_c || sub_c < 0 || cur_c < 0 ) {
                    sub_i = start_sub_i;
                    found = false;
                    break;
                }
            }

            if ( found )
                return start_i;
        }
    }

    return -1;
}

int32_t str_u8_count(const char *src, const int32_t start, const int32_t max_len) {
    int32_t count = 0;
    int32_t i = start;

    while ( i < max_len ) {
        UChar32 c;
        U8_NEXT(src, i, max_len, c);
        count++;
    }

    return count;
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

bool str_is_empty(const char *str) {
    if ( str == NULL )
        return true;
    return strnlen(str, 1) < 1;
}

int str_replace_char(char *str, const char old_c, const char new_c) {
    int count = 0;
    for ( size_t i = 0; i < strnlen(str, MAX_STRLEN); i++ ) {
        if ( str[i] == old_c ) {
            str[i] = new_c;
            count += 1;
        }
    }

    return count;
}

bool str_equals_sized(const char *a, const char *b, size_t len) {
    size_t a_len = strnlen(a, len);
    size_t b_len = strnlen(b, len);
    if ( a_len != b_len || MIN(a_len, b_len) < len )
        return false;
    return memcmp(a, b, a_len) == 0;
}

bool str_equals_right_sized(const char *a, const char *b) {
    return str_equals_sized(a, b, strlen(b));
}

static void resize_str_buffer(StrBuffer_t *buf, const size_t cap) {
    char *new_buf = realloc(buf->data, cap);
    if ( new_buf == NULL ) {
        error_abort("Failed to reallocate string buffer");
    }
    buf->data = new_buf;
    buf->cap = cap;
}

StrBuffer_t *str_buf_init(void) {
    StrBuffer_t *buf = calloc(1, sizeof(StrBuffer_t));
    if ( buf == NULL ) {
        error_abort("Failed to allocate string buffer");
    }
    resize_str_buffer(buf, STR_BUF_START_CAP);
    buf->data[0] = '\0';
    return buf;
}

void str_buf_append(StrBuffer_t *buf, const char *str, const char *end) {
    const size_t len = end != NULL ? (size_t)(end - str) : strnlen(str, MAX_STRLEN);
    if ( buf->len + len + 1 > buf->cap ) {
        resize_str_buffer(buf, (buf->len + len + 1) * 2);
    }
    memcpy(buf->data + buf->len, str, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
}

void str_buf_append_len(StrBuffer_t *buf, const char *str, const size_t len) {
    if ( len == 0 )
        return;
    str_buf_append(buf, str, str + len);
}

void str_buf_append_ch(StrBuffer_t *buf, const char ch) {
    if ( buf->len + 2 > buf->cap ) {
        resize_str_buffer(buf, (buf->len + 2) * 2);
    }
    buf->data[buf->len] = ch;
    buf->len += 1;
    buf->data[buf->len] = '\0';
}

void str_buf_destroy(StrBuffer_t *buf) {
    if ( buf != NULL ) {
        if ( buf->data != NULL )
            free(buf->data);
        free(buf);
    }
}

size_t str_buffered_read(char *destination, const size_t size, const char *src, const size_t src_len, const size_t start_offset) {
    if ( start_offset >= src_len )
        return 0;

    size_t i = 0;
    while ( i < size - 1 && start_offset + i < src_len ) {
        const char c = src[start_offset + i];
        destination[i++] = c;

        if ( c == '\n' )
            break;
    }
    destination[i] = '\0';
    return i;
}
