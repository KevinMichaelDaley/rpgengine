/**
 * @file edit_script_api_entity.c
 * @brief engine.write_entity — validated entity attribute writes.
 *
 * Validates all inputs (entity_id, key, type, value) before writing
 * to the update blob via script_env_write_attr().
 *
 * Lua signature:
 *   engine.write_entity(entity_id, key, type_str, value)
 *     → true on success
 *     → false, reason on validation failure
 *
 * Supported type_str values: "f32", "vec3", "i32", "u32", "bool", "str"
 */

#ifdef LUAJIT_ENABLE

#include "ferrum/editor/edit_script_env.h"

#include <string.h>
#include "lua.h"
#include "lauxlib.h"

/* Maximum string payload size (must fit in uint8_t). */
#define MAX_STR_PAYLOAD 255

/* Upvalue index for the script_env pointer. */
#define UV_ENV 1

/**
 * Parse the type_str argument and return the SCRIPT_ATTR_* enum value.
 * Returns -1 if the type string is not recognized.
 */
static int parse_type_str(const char *s)
{
    if (strcmp(s, "f32")  == 0) return SCRIPT_ATTR_F32;
    if (strcmp(s, "vec3") == 0) return SCRIPT_ATTR_VEC3;
    if (strcmp(s, "i32")  == 0) return SCRIPT_ATTR_I32;
    if (strcmp(s, "u32")  == 0) return SCRIPT_ATTR_U32;
    if (strcmp(s, "bool") == 0) return SCRIPT_ATTR_BOOL;
    if (strcmp(s, "str")  == 0) return SCRIPT_ATTR_STR;
    if (strcmp(s, "blob") == 0) return SCRIPT_ATTR_BLOB;
    return -1;
}

/**
 * Return (false, reason) to Lua.
 */
static int fail_with(lua_State *L, const char *reason)
{
    lua_pushboolean(L, 0);
    lua_pushstring(L, reason);
    return 2;
}

/**
 * engine.write_entity(entity_id, key, type_str, value)
 *
 * Validates all arguments, then writes to the update blob.
 */
static int l_engine_write_entity(lua_State *L)
{
    script_env_t *env =
        (script_env_t *)lua_touserdata(L, lua_upvalueindex(UV_ENV));

    /* Arg 1: entity_id (number). */
    if (!lua_isnumber(L, 1)) {
        return fail_with(L, "entity_id must be a number");
    }
    lua_Number raw_id = lua_tonumber(L, 1);
    if (raw_id < 0 || raw_id > 0xFFFFFFFF) {
        return fail_with(L, "entity_id out of range");
    }
    uint32_t entity_id = (uint32_t)raw_id;

    /* Bounds check: entity_id must be < snapshot capacity. */
    if (entity_id >= env->entities.capacity) {
        return fail_with(L, "entity_id exceeds snapshot capacity");
    }

    /* Arg 2: key (number). */
    if (!lua_isnumber(L, 2)) {
        return fail_with(L, "key must be a number");
    }
    lua_Number raw_key = lua_tonumber(L, 2);
    if (raw_key < 0 || raw_key > 65535) {
        return fail_with(L, "key out of range (0-65535)");
    }
    uint16_t key = (uint16_t)raw_key;

    /* Arg 3: type_str (string). */
    const char *type_str = lua_tostring(L, 3);
    if (!type_str) {
        return fail_with(L, "type must be a string");
    }
    int attr_type = parse_type_str(type_str);
    if (attr_type < 0) {
        return fail_with(L, "unknown type (valid: f32, vec3, i32, u32, bool, str)");
    }

    /* Arg 4: value — type-dependent extraction and validation. */
    uint8_t payload[256];
    uint8_t payload_size = 0;

    switch (attr_type) {
    case SCRIPT_ATTR_F32: {
        if (!lua_isnumber(L, 4)) {
            return fail_with(L, "f32 value must be a number");
        }
        float val = (float)lua_tonumber(L, 4);
        memcpy(payload, &val, sizeof(val));
        payload_size = sizeof(float);
        break;
    }
    case SCRIPT_ATTR_VEC3: {
        if (!lua_istable(L, 4)) {
            return fail_with(L, "vec3 value must be a table {x, y, z}");
        }
        float vec[3];
        for (int i = 0; i < 3; i++) {
            lua_rawgeti(L, 4, i + 1);
            if (!lua_isnumber(L, -1)) {
                lua_pop(L, 1);
                return fail_with(L, "vec3 elements must be numbers");
            }
            vec[i] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        memcpy(payload, vec, sizeof(vec));
        payload_size = sizeof(vec);
        break;
    }
    case SCRIPT_ATTR_I32: {
        if (!lua_isnumber(L, 4)) {
            return fail_with(L, "i32 value must be a number");
        }
        int32_t val = (int32_t)lua_tonumber(L, 4);
        memcpy(payload, &val, sizeof(val));
        payload_size = sizeof(int32_t);
        break;
    }
    case SCRIPT_ATTR_U32: {
        if (!lua_isnumber(L, 4)) {
            return fail_with(L, "u32 value must be a number");
        }
        lua_Number raw = lua_tonumber(L, 4);
        if (raw < 0 || raw > 0xFFFFFFFF) {
            return fail_with(L, "u32 value out of range");
        }
        uint32_t val = (uint32_t)raw;
        memcpy(payload, &val, sizeof(val));
        payload_size = sizeof(uint32_t);
        break;
    }
    case SCRIPT_ATTR_BOOL: {
        if (!lua_isboolean(L, 4)) {
            return fail_with(L, "bool value must be a boolean");
        }
        uint8_t val = lua_toboolean(L, 4) ? 1 : 0;
        payload[0] = val;
        payload_size = 1;
        break;
    }
    case SCRIPT_ATTR_STR: {
        size_t len = 0;
        const char *str = lua_tolstring(L, 4, &len);
        if (!str) {
            return fail_with(L, "str value must be a string");
        }
        /* +1 for null terminator, total must fit in uint8_t. */
        if (len + 1 > MAX_STR_PAYLOAD) {
            return fail_with(L, "string too long (max 254 chars)");
        }
        memcpy(payload, str, len);
        payload[len] = '\0';
        payload_size = (uint8_t)(len + 1);
        break;
    }
    case SCRIPT_ATTR_BLOB: {
        /* Blob: accept a string as raw bytes. */
        size_t len = 0;
        const char *data = lua_tolstring(L, 4, &len);
        if (!data) {
            return fail_with(L, "blob value must be a string");
        }
        if (len > MAX_STR_PAYLOAD) {
            return fail_with(L, "blob too large (max 255 bytes)");
        }
        memcpy(payload, data, len);
        payload_size = (uint8_t)len;
        break;
    }
    default:
        return fail_with(L, "internal error: unhandled type");
    }

    /* Write to the update blob. */
    script_env_write_attr(env, entity_id, 0, key,
                          (uint8_t)attr_type, payload, payload_size);

    lua_pushboolean(L, 1);
    return 1;
}

void script_api_register_entity(lua_State *L, script_env_t *env)
{
    /* The engine table is on top of the stack. */
    lua_pushlightuserdata(L, env);
    lua_pushcclosure(L, l_engine_write_entity, 1);
    lua_setfield(L, -2, "write_entity");
}

#endif /* LUAJIT_ENABLE */
