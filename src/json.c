#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "str_utils.h"

static Vector_t *parse_list(const char *src, int32_t *idx, size_t size, JsonContext_t *ctx);
static JsonObject_t *parse_object(const char *src, int32_t *idx, size_t size, JsonContext_t *ctx);

static void cb_json_free_value(const char *key, void *value, void *user_data) { free(value); }

static void free_list(Vector_t *list) {
    for ( size_t i = 0; i < list->size; i++ ) {
        Vector_t *inner = list->data[i];
        // Free directly the data being pointed at by the inner list, which should be JsonField_t
        for ( int j = 0; j < (int32_t)inner->size; j++ ) {
            free(inner->data[j]);
        }
        vec_destroy(inner);
    }
    vec_destroy(list);
}

JsonContext_t *json_ctx_init() {
    JsonContext_t *ctx = calloc(1, sizeof(*ctx));
    if ( ctx == NULL ) {
        fprintf(stderr, "Failed to allocate context for json parsing\n");
        return NULL;
    }
    ctx->string_pool = map_init();
    ctx->vector_pool = vec_init();
    return ctx;
}

void json_ctx_destroy(JsonContext_t *ctx) {
    if ( ctx != NULL ) {
        if ( ctx->string_pool != NULL ) {
            map_iterate(ctx->string_pool, cb_json_free_value, NULL);
            map_destroy(ctx->string_pool);
        }
        if ( ctx->vector_pool != NULL ) {
            free_list(ctx->vector_pool);
        }
    }
}

typedef enum {
    JSON_TOKEN_OBJ_OPEN = 0,
    JSON_TOKEN_OBJ_CLOSE,
    JSON_TOKEN_QUOTE,
    JSON_TOKEN_COLON,
    JSON_TOKEN_COMMA,
    JSON_TOKEN_BRACKET_OPEN,
    JSON_TOKEN_BRACKET_CLOSE,
    JSON_TOKEN_NUM,
    JSON_TOKEN_BOOL,
    JSON_TOKEN_NULL,
    JSON_TOKEN_ERROR
} JsonTokenType_t;

static JsonTokenType_t peek(const char *src, int32_t idx, size_t size, int32_t *skip) {
    int32_t new_idx = idx;

    *skip = str_skip_whitespace(src, new_idx, size);
    new_idx += *skip;
    int32_t c = str_u8_next(src, size, &new_idx);

    switch ( c ) {
    case '{':
        return JSON_TOKEN_OBJ_OPEN;
    case '}':
        return JSON_TOKEN_OBJ_CLOSE;
    case ':':
        return JSON_TOKEN_COLON;
    case '"':
        return JSON_TOKEN_QUOTE;
    case ',':
        return JSON_TOKEN_COMMA;
    case '[':
        return JSON_TOKEN_BRACKET_OPEN;
    case ']':
        return JSON_TOKEN_BRACKET_CLOSE;
    }

    if ( (c >= '0' && c <= '9') || c == '-' || c == '.' )
        return JSON_TOKEN_NUM;

    // TODO: Only works with lowercase bool literals but who cares
    if ( str_equals_sized(src + idx, "true", strlen("true")) || str_equals_sized(src + idx, "false", strlen("false")) ) {
        return JSON_TOKEN_BOOL;
    }
    if ( str_equals_sized(src + idx, "null", strlen("null")) ) {
        return JSON_TOKEN_NULL;
    }

    return JSON_TOKEN_ERROR;
}

static const char *parse_string(const char *src, int32_t *idx, size_t size) {
    StrBuffer_t *buffer = str_buf_init();

    int32_t skip = 0;
    JsonTokenType_t token = peek(src, *idx, size, &skip);
    if ( token != JSON_TOKEN_QUOTE ) {
        fprintf(stderr, "Json: parse_string: Expected \" at %d\n", *idx);
        str_buf_destroy(buffer);
        return NULL;
    }
    *idx += skip + 1; // for the " character

    // TODO: Handle escape sequences
    while ( true ) {
        int32_t tmp_idx = *idx;
        int32_t c = str_u8_next(src, size, &tmp_idx);
        if ( c < 0 ) {
            fprintf(stderr, "Json: parse_string: Unexpected end of input.\n");
            str_buf_destroy(buffer);
            return NULL;
        }
        if ( c == '"' ) {
            *idx = tmp_idx;
            break;
        }
        str_buf_append_len(buffer, src + *idx, tmp_idx - *idx);
        *idx = tmp_idx;
    }

    const char *dup = strdup(buffer->data);
    str_buf_destroy(buffer);
    return dup;
}

static bool parse_number(const char *src, int32_t *idx, size_t size, double *result) {
    double num = 0;
    bool negative = false;
    double fract_divisor = 0.0;
    int32_t tmp_idx = *idx;

    const int32_t skip = str_skip_whitespace(src, *idx, (int32_t)size);
    tmp_idx += skip;

    // TODO: Handle scientific notation
    while ( tmp_idx < (int32_t)size ) {
        int32_t prev_idx = tmp_idx;
        int32_t c = str_u8_next(src, size, &tmp_idx);
        if ( c == '-' ) {
            if ( tmp_idx - skip - 1 == *idx ) { // - 1 : include amount skipped by the - character
                // Ok
                negative = true;
            } else {
                fprintf(stderr, "Json: parse_number: Unexpected - at %d\n", tmp_idx);
                return false;
            }
        }
        if ( c == '.' ) {
            if ( fract_divisor > 0.0 ) {
                fprintf(stderr, "Json: parse_number: Unexpected . at %d, probably occurred more than once\n", tmp_idx);
                return false;
            }
            fract_divisor = 1.0;
        }
        if ( c >= '0' && c <= '9' ) {
            if ( fract_divisor == 0.0 ) {
                // whole part
                num *= 10;
                num += c - '0';
            } else {
                fract_divisor *= 10.0;
                num += (c - '0') / fract_divisor;
            }
        } else {
            // Not inside the number anymore, backtrack
            tmp_idx = prev_idx;
            break;
        }
    }

    *result = negative ? -num : num;
    *idx = tmp_idx;
    return true;
}

static JsonField_t *parse_field(const char *src, int32_t *idx, size_t size, JsonContext_t *ctx, bool skip_name) {
    JsonField_t *field = calloc(1, sizeof(*field));

    if ( skip_name ) {
        field->name = strdup("");
    } else {
        const char *name = parse_string(src, idx, size);
        if ( name == NULL ) {
            free(field);
            return NULL;
        }

        field->name = name;
    }

    if ( !skip_name ) {
        int32_t skip = 0;
        JsonTokenType_t token = peek(src, *idx, size, &skip);
        *idx += skip;

        if ( token != JSON_TOKEN_COLON ) {
            fprintf(stderr, "Json: parse_field: Expected : at %d\n", *idx);
            free(field);
            return NULL;
        }
        *idx += 1; // :
    }

    int32_t skip = 0;
    JsonTokenType_t token = peek(src, *idx, size, &skip);
    *idx += skip;

    switch ( token ) {
    case JSON_TOKEN_QUOTE:
        const char *str_value = parse_string(src, idx, size);
        if ( str_value == NULL ) {
            free(field);
            return NULL;
        }
        field->type = JSON_STRING;
        field->value.str_value = str_value;
        // Add to the hashmap
        map_put(ctx->string_pool, str_value, (void *)str_value);
        break;
    case JSON_TOKEN_NUM:
        double result = 0;
        if ( !parse_number(src, idx, size, &result) ) {
            free(field);
            return NULL;
        }
        field->type = JSON_NUMBER;
        field->value.num_value = result;
        break;
    case JSON_TOKEN_BRACKET_OPEN:
        Vector_t *list = parse_list(src, idx, size, ctx);
        if ( list == NULL ) {
            free(field);
            return NULL;
        }
        field->type = JSON_LIST;
        field->value.list_value = list;
        break;
    case JSON_TOKEN_OBJ_OPEN:
        JsonObject_t *obj = parse_object(src, idx, size, ctx);
        if ( obj == NULL ) {
            free(field);
            return NULL;
        }
        field->type = JSON_OBJECT;
        field->value.obj_value = obj;
        break;
    case JSON_TOKEN_BOOL:
        field->type = JSON_BOOL;
        if ( str_equals_right_sized(src + *idx, "true") ) {
            field->value.bool_value = true;
            *idx += strlen("true");
        } else if ( str_equals_right_sized(src + *idx, "false") ) {
            field->value.bool_value = false;
            *idx += strlen("false");
        } else {
            fprintf(stderr, "Json: parse_field: Invalid value for bool literal at %d\n", *idx);
            free(field);
            return NULL;
        }
        break;
    case JSON_TOKEN_NULL:
        field->type = JSON_NULL;
        field->value.bool_value = 0;
        *idx += strlen("null");
        break;
    default:
        break;
    }

    return field;
}

static Vector_t *parse_list(const char *src, int32_t *idx, size_t size, JsonContext_t *ctx) {
    int32_t skip = 0;
    JsonTokenType_t token = peek(src, *idx, size, &skip);
    *idx += skip;

    if ( token != JSON_TOKEN_BRACKET_OPEN ) {
        fprintf(stderr, "Json: parse_list: Expected [ at %d\n", *idx);
        return NULL;
    }
    // Consume the [
    *idx += 1;

    Vector_t *list = vec_init();
    JsonFieldType_t list_type = JSON_ERROR;

    while ( true ) {
        token = peek(src, *idx, size, &skip);
        *idx += skip;

        if ( token == JSON_TOKEN_BRACKET_CLOSE ) {
            *idx += 1;
            break;
        }

        if ( list->size > 0 ) {
            if ( token != JSON_TOKEN_COMMA ) {
                fprintf(stderr, "Json: parse_list: Expected a , at %d\n", *idx);
                vec_destroy(list);
                return NULL;
            }
            *idx += 1; // For the , character
            token = peek(src, *idx, size, &skip);
            *idx += skip;
        }

        JsonField_t *field = parse_field(src, idx, size, ctx, true);
        if ( field == NULL ) {
            free_list(list);
            return NULL;
        }

        if ( list->size > 0 && field->type != list_type ) {
            fprintf(stderr, "Json: parse_list: Field at idx %d has different type from the other values.\n", *idx);
            free(field);
            free_list(list);
            return NULL;
        }

        if ( list->size == 0 ) {
            list_type = field->type;
        }

        vec_add(list, field);
    }

    return list;
}

JsonObject_t *parse_object(const char *src, int32_t *idx, size_t size, JsonContext_t *ctx) {
    int32_t skip = 0;
    JsonTokenType_t token = peek(src, *idx, size, &skip);
    if ( token == JSON_TOKEN_ERROR ) {
        fprintf(stderr, "Json: parse_object: Token error at %d\n", *idx);
        return NULL;
    }

    if ( token != JSON_TOKEN_OBJ_OPEN ) {
        fprintf(stderr, "Json: parse_object: Expected { at %d, got %d\n", *idx, token);
        return NULL;
    }

    // Consume a '{'
    *idx += skip + 1;

    JsonObject_t *obj = calloc(1, sizeof(*obj));
    obj->children = vec_init();

    while ( true ) {
        token = peek(src, *idx, size, &skip);
        *idx += skip;

        if ( token == JSON_TOKEN_OBJ_CLOSE ) {
            *idx += 1;
            break;
        }

        if ( obj->children->size > 0 ) {
            if ( token != JSON_TOKEN_COMMA ) {
                fprintf(stderr, "Json: parse_object: Expected comma at %d\n", *idx);
                json_obj_destroy(obj);
                return NULL;
            }
            *idx += 1; // consume comma
            // Peek again for next token (field name)
            token = peek(src, *idx, size, &skip);
            *idx += skip;
        }

        if ( token == JSON_TOKEN_QUOTE ) {
            int32_t tmp_idx = *idx;
            JsonField_t *field = parse_field(src, &tmp_idx, size, ctx, false);
            if ( field == NULL ) {
                json_obj_destroy(obj);
                return NULL;
            }
            *idx = tmp_idx;
            vec_add(obj->children, field);
        } else {
            fprintf(stderr, "Json: parse_object: Expected string at %d\n", *idx);
            json_obj_destroy(obj);
            return NULL;
        }
    }

    return obj;
}

JsonObject_t *json_parse(const char *src, JsonContext_t *ctx) {
    size_t size = (size_t)strlen(src);
    int32_t idx = 0;
    return parse_object(src, &idx, size, ctx);
}

void json_obj_destroy(JsonObject_t *obj) {
    if ( obj != NULL ) {
        if ( obj->children != NULL ) {
            vec_destroy(obj->children);
        }
        free(obj);
    }
}

const JsonField_t *json_obj_get(JsonObject_t *obj, const char *field) {
    if ( obj == NULL || str_is_empty(field) )
        return NULL;

    for ( size_t i = 0; i < obj->children->size; i++ ) {
        const JsonField_t *obj_field = obj->children->data[i];
        if ( strcmp(obj_field->name, field) == 0 )
            return obj_field;
    }
    return NULL;
}

double json_get_number(const JsonField_t *field) {
    if ( field == NULL || field->type != JSON_NUMBER ) {
        if ( field->type != JSON_NULL )
            fprintf(stderr, "Json: json_get_number: Warning: field is not a number. returning zero.\n");
        return 0.0;
    }

    return field->value.num_value;
}

const char *json_get_string(const JsonField_t *field) {
    if ( field == NULL || field->type != JSON_STRING )
        return NULL;

    return field->value.str_value;
}

bool json_get_bool(const JsonField_t *field) {
    if ( field == NULL || field->type != JSON_BOOL ) {
        fprintf(stderr, "Json: json_get_bool: Warning: field is not a bool. returning false.\n");
        return false;
    }

    return field->value.bool_value;
}

const Vector_t *json_get_list(const JsonField_t *field) {
    if ( field == NULL || field->type != JSON_LIST ) {
        return NULL;
    }

    return field->value.list_value;
}

bool json_is_null(const JsonField_t *field) { return field == NULL || field->type == JSON_NULL; }
