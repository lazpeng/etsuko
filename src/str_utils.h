/**
 * str_utils.h - Defines various functions that help deal with strings
 */

#ifndef ETSUKO_STR_UTILS_H
#define ETSUKO_STR_UTILS_H
#include <stdbool.h>
#include <stdint.h>

int32_t str_find(const char *src, char c, int32_t start, int32_t max_len);
char *str_get_filename(const char *path);
char *str_get_filename_no_ext(const char *path);
bool str_is_empty(const char *str);
void str_replace_char(char *str, char old_c, char new_c);

#endif // ETSUKO_STR_UTILS_H
