/**
 * str_utils.h - Defines various functions that help deal with strings
 */

#ifndef ETSUKO_STR_UTILS_H
#define ETSUKO_STR_UTILS_H

#include <stddef.h>
#include <stdint.h>

/**
 * Implements a basic string buffer with dynamic size that you can gradually
 * append to.
 */
typedef struct StrBuffer_t {
    // Actual string data
    char *data;
    size_t len, cap;
} StrBuffer_t;

/**
 * Finds the byte index of the first occurrence of a given character in a string,
 * starting from the given index.
 * Returns -1 if not found.
 */
int32_t str_find(const char *src, char c, int32_t start, int32_t max_len);
/**
 * Finds the byte index of the first occurrence of the given utf8 encoded substring in a
 * similarly utf8 encoded string, starting from the given index.
 * Returns -1 if not found.
 */
int32_t str_u8_find_str(const char *src, const char *sub, int32_t start, int32_t max_len, int32_t sub_len);
/**
 * Counts the number of utf8 encoded characters in a string, starting from the given index.
 */
int32_t str_u8_count(const char *src, int32_t start, int32_t max_len);
/**
 * Finds the next utf8 encoded character in a string, starting from the given index.
 */
int32_t str_u8_next(const char *const bytes, size_t size, int32_t *index);
/**
 * Returns the filename portion of a given path string, including the extension.
 * Example:
 *     str_get_filename("/home/user/file.txt") returns "file.txt"
 */
char *str_get_filename(const char *path);
/**
 * Returns the filename portion of a given path string, excluding the extension.
 * Example:
 *     str_get_filename_no_ext("/home/user/file.txt") returns "file"
 */
char *str_get_filename_no_ext(const char *path);
/**
 * Returns true if the given string is empty or null.
 */
bool str_is_empty(const char *str);
/**
 * Replaces a given byte character in a string.
 * Returns the number of characters replaced.
 */
int str_replace_char(char *str, char old_c, char new_c);
/**
 * Compares two strings byte by byte, up to the length of the string on the
 * right side.
 * same as str_equals_sized(left, right, strlen(right))
 */
bool str_equals_right_sized(const char *a, const char *b);
/**
 * Returns true if the first len bytes of both strings are equal. Returns false if the
 * strings don't match or any of the strings is shorter than len bytes.
 */
bool str_equals_sized(const char *a, const char *b, size_t len);
/**
 * Initializes a new StrBuffer_t.
 */
StrBuffer_t *str_buf_init(void);
/**
 * Appends a string slice to a StrBuffer_t. In case end is NULL, the whole remaining
 * string is appended.
 */
void str_buf_append(StrBuffer_t *buf, const char *str, const char *end);
/**
 * Appends len bytes of the given string to a StrBuffer_t.
 */
void str_buf_append_len(StrBuffer_t *buf, const char *str, size_t len);
/**
 * Appends a single byte to a StrBuffer_t.
 */
void str_buf_append_ch(StrBuffer_t *buf, char ch);
/**
 * Frees all resources associated with a StrBuffer_t.
 */
void str_buf_destroy(StrBuffer_t *buf);
/**
 * Reads up to size bytes from a string buffer into a destination buffer.
 * Returns the number of bytes read.
 */
size_t str_buffered_read(char *destination, size_t size, const char *src, size_t src_len, size_t start_offset);
/**
 * Checks whether the given unicode codepoint is a japanese kanji
 */
bool str_ch_is_kanji(int32_t c);
/**
 * Checks whether the given unicode codepoint is a japanese hiragana character
 */
bool str_ch_is_hiragana(int32_t c);
/**
 * Checks whether the given unicode codepoint is a japanese katakana character
 */
bool str_ch_is_katakana(int32_t c);
/**
 * Checks whether the given unicode codepoint is either a japanese katakana or hiragana character
 */
bool str_ch_is_kana(int32_t c);
/**
 * Checks whether the given unicode codepoint is a japanese particle
 */
bool str_ch_is_japanese_particle(int32_t c);
/**
 * Checks whether the given unicode codepoint is a japanese comma or period
 */
bool str_ch_is_japanese_comma_or_period(int32_t c);

#endif // ETSUKO_STR_UTILS_H
