#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/gl_loader.h"

#define ASSERT_TRUE(cond)                                                                               \
    do {                                                                                                \
        if (!(cond)) {                                                                                  \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                         \
    do {                                                                                                \
        if ((exp) != (act)) {                                                                           \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,    \
                    (int)(exp), (int)(act));                                                            \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)

#define ASSERT_STR_EQ(exp, act)                                                                         \
    do {                                                                                                \
        if (strcmp((exp), (act)) != 0) {                                                                \
            fprintf(stderr, "ASSERT_STR_EQ failed: %s:%d: expected %s got %s\n", __FILE__, __LINE__,    \
                    (exp), (act));                                                                      \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)

struct proc_entry {
    const char *name;
    void *ptr;
};

struct proc_table {
    struct proc_entry *entries;
    size_t count;
};

static void *get_proc_stub(const char *name, void *user_data) {
    struct proc_table *table = (struct proc_table *)user_data;
    for (size_t i = 0; i < table->count; ++i) {
        if (strcmp(table->entries[i].name, name) == 0) {
            return table->entries[i].ptr;
        }
    }
    return NULL;
}

static int test_missing_loader_is_error(void) {
    const char *missing = NULL;
    ASSERT_INT_EQ(GL_LOADER_ERR_MISSING, gl_loader_validate(NULL, &missing));
    ASSERT_STR_EQ("get_proc_address", missing);
    return 0;
}

static int test_missing_required_symbol_reports_name(void) {
    struct proc_entry entries[] = {
        {"glCreateShader", (void *)0x1}
    };
    struct proc_table table = {entries, 1};
    gl_loader_t loader = {get_proc_stub, &table};

    const char *missing = NULL;
    ASSERT_INT_EQ(GL_LOADER_ERR_MISSING, gl_loader_validate(&loader, &missing));
    ASSERT_TRUE(missing != NULL);
    ASSERT_STR_EQ("glShaderSource", missing);
    return 0;
}

static int test_all_required_symbols_ok(void) {
    struct proc_entry entries[] = {
        {"glCreateShader", (void *)0x1},
        {"glShaderSource", (void *)0x1},
        {"glCompileShader", (void *)0x1},
        {"glGetShaderiv", (void *)0x1},
        {"glGetShaderInfoLog", (void *)0x1},
        {"glDeleteShader", (void *)0x1},
        {"glCreateProgram", (void *)0x1},
        {"glAttachShader", (void *)0x1},
        {"glLinkProgram", (void *)0x1},
        {"glGetProgramiv", (void *)0x1},
        {"glGetProgramInfoLog", (void *)0x1},
        {"glUseProgram", (void *)0x1},
        {"glDeleteProgram", (void *)0x1},
        {"glGetUniformLocation", (void *)0x1},
        {"glUniformMatrix4fv", (void *)0x1},
        {"glUniform3fv", (void *)0x1},
        {"glUniform1i", (void *)0x1},
        {"glUniform1f", (void *)0x1},
        {"glGenBuffers", (void *)0x1},
        {"glDeleteBuffers", (void *)0x1},
        {"glBindBuffer", (void *)0x1},
        {"glBufferData", (void *)0x1},
        {"glGenVertexArrays", (void *)0x1},
        {"glDeleteVertexArrays", (void *)0x1},
        {"glBindVertexArray", (void *)0x1},
        {"glEnableVertexAttribArray", (void *)0x1},
        {"glVertexAttribPointer", (void *)0x1},
        {"glVertexAttribIPointer", (void *)0x1}
    };
    struct proc_table table = {entries, sizeof(entries) / sizeof(entries[0])};
    gl_loader_t loader = {get_proc_stub, &table};

    const char *missing = NULL;
    ASSERT_INT_EQ(GL_LOADER_OK, gl_loader_validate(&loader, &missing));
    ASSERT_TRUE(missing == NULL);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"missing_loader_is_error", test_missing_loader_is_error},
    {"missing_required_symbol_reports_name", test_missing_required_symbol_reports_name},
    {"all_required_symbols_ok", test_all_required_symbols_ok}
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        printf("RUN %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) {
            printf("OK %s\n", TESTS[i].name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", TESTS[i].name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
