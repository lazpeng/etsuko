#ifndef ETSUKO_JSON_H
#define ETSUKO_JSON_H

#include "constants.h"
#include "container_utils.h"
#include "str_utils.h"
#include <stdbool.h>

// Type of a json field value
typedef enum JsonFieldType_t {
    JSON_STRING = 0,
    JSON_NUMBER,
    JSON_BOOL,
    JSON_LIST,
    JSON_OBJECT,
    JSON_NULL,
    JSON_ERROR
} JsonFieldType_t;

// Represents a boring json object
typedef struct JsonObject_t {
    OWNING Vector_t *children; // of JsonField_t
} JsonObject_t;

// Represents a field inside a json obejct, containing a name and a value of predefined types
typedef struct JsonField_t {
    JsonFieldType_t type;
    OWNING const char *name;
    union {
        WEAK const char *str_value;
        double num_value;
        bool bool_value;
        WEAK Vector_t *list_value;
        WEAK JsonObject_t *obj_value;
    } value;
} JsonField_t;

/**
 * Holds some important context info for the json parsing code, and helps with freeing all the relevant data
 * when it has finished processing
 */
typedef struct JsonContext_t {
    /**
     * A map of all the strings found in the provided json source strings.
     * They are grouped together for those with the same value and will point to the same data if repeated.
     * when _destroy() is called, all these strings are freed, so any variables still pointing to them will yield garbage
     */
    OWNING HashMap_t *string_pool; // of const char*
    /**
     * A list of lists allocated from parsing the json source. They can be of anything and will generally depend on the first
     * element inside the list. Unlike strings, these vectors are *not* deduped. These are also freed in the _destroy() function,
     * so any pointers still pointing to their data will be messed up. Also the data they point to will be freed as well, not just
     * the list itself
     */
    OWNING Vector_t *vector_pool; // of Vector_t
    // List of all objects initialized as fields (including the root object). Will be freed as part of the _destroy() function
    OWNING Vector_t *objects; // of JsonObject_t
} JsonContext_t;

// Initializes the context
JsonContext_t *json_ctx_init();
/**
 * Finalizes the context, freeing up all related info. If strings are still being referenced by JsonFields, they will
 * cause all sorts of errors, so only call finish after you're done dealing with the parsed json stuff, or strdup'ped them
 * into your own heap
 */
void json_ctx_destroy(JsonContext_t *ctx);
/**
 * Parses a json object from a given string. One quirk is that, if the given string does _not_ contain an object,
 * like a list for example, it will return an object with only that field in it
 */
JsonObject_t *json_parse(const char *src, JsonContext_t *ctx);
// Free all the data related to a json object. Will not free the strings and lists it points to, those are owned by the context.
// only the children vector and the actual field structs are freed
void json_obj_destroy(JsonObject_t *obj);
/**
 * Returns the given field from the given object. NULL if the field is not found
 * Notice that only the direct children of the object are considered.
 * So for example, if you need foo.bar.baz, you need to call
 * get("foo")->get("bar")->get("baz")
 */
const JsonField_t *json_obj_get(JsonObject_t *obj, const char *field);
// Helper function that returns the numeric value for a field of type number. Aborts the entire program and sets fire to your
// house if you call it on the wrong type
double json_get_number(const JsonField_t *field);
// Helper function that returns the string pointed at by the field with type string
const char *json_get_string(const JsonField_t *field);
// Helper function that returns the bool value for a field of type bool
bool json_get_bool(const JsonField_t *field);
// helper function that returns the list (Vector_t) pointed at by the field with type list
const Vector_t *json_get_list(const JsonField_t *field);
// helper function for whether the value is null
bool json_is_null(const JsonField_t *field);

// JSON writer helpers (StrBuffer-based)
void json_buf_begin_object(StrBuffer_t *buf, bool *first);
void json_buf_end_object(StrBuffer_t *buf);
void json_buf_append_escaped_string(StrBuffer_t *buf, const char *str);
void json_buf_add_string(StrBuffer_t *buf, bool *first, const char *name, const char *value);
void json_buf_add_number(StrBuffer_t *buf, bool *first, const char *name, double value);
void json_buf_add_bool(StrBuffer_t *buf, bool *first, const char *name, bool value);

#endif // ETSUKO_JSON_H
