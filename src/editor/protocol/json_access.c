/**
 * @file json_access.c
 * @brief JSON value accessor functions (object lookup, array index, string copy).
 */

#include "ferrum/editor/json_parse.h"
#include <string.h>

const json_value_t *json_object_get(const json_value_t *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;
    size_t key_len = strlen(key);
    /* Search backwards so last key wins (for duplicate keys). */
    for (uint32_t i = obj->object.count; i > 0; --i) {
        uint32_t idx = i - 1;
        if (obj->object.key_lens[idx] == (uint32_t)key_len &&
            memcmp(obj->object.keys[idx], key, key_len) == 0) {
            return &obj->object.vals[idx];
        }
    }
    return NULL;
}

const json_value_t *json_array_get(const json_value_t *arr, uint32_t index) {
    if (!arr || arr->type != JSON_ARRAY) return NULL;
    if (index >= arr->array.count) return NULL;
    return &arr->array.items[index];
}

bool json_string_copy(const json_value_t *val, char *buf, size_t buf_cap) {
    if (!val || val->type != JSON_STRING || !buf || buf_cap == 0) return false;
    if (val->string.len + 1 > buf_cap) return false;
    memcpy(buf, val->string.ptr, val->string.len);
    buf[val->string.len] = '\0';
    return true;
}
