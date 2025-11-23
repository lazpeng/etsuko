/**
 * str_utils.h - Defines various functions that help deal with strings
 */

#ifndef ETSUKO_STR_UTILS_H
#define ETSUKO_STR_UTILS_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct StrBuffer_t {
    char *data;
    size_t len, cap;
} StrBuffer_t;

int32_t str_find(const char *src, char c, int32_t start, int32_t max_len);
char *str_get_filename(const char *path);
char *str_get_filename_no_ext(const char *path);
bool str_is_empty(const char *str);
void str_replace_char(char *str, char old_c, char new_c);

StrBuffer_t *str_buf_init(void);
void str_buf_append(StrBuffer_t *buf, const char *str, const char *end);
void str_buf_append_len(StrBuffer_t *buf, const char *str, size_t len);
void str_buf_append_ch(StrBuffer_t *buf, char ch);
void str_buf_destroy(StrBuffer_t *buf);

size_t str_buffered_read(char *destination, size_t size, const char *src, size_t src_len, size_t start_offset);

#endif // ETSUKO_STR_UTILS_H
