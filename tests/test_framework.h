#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Minimalist test framework
 * - Simple assertions
 * - Clear error messages
 * - Minimal dependencies
 */

/* Test directory for creating test files (prefixed) */
#define TEST_DIR "/tmp/__llm_ctx_test"

/* Test state */
static int tests_run = 0;
static int tests_failed = 0;
static char last_failure[1024] = "";

/* Macros for test assertions */
#define ASSERT(message, test) do { if (!(test)) { \
    snprintf(last_failure, sizeof(last_failure), "%s (line %d): %s", __FILE__, __LINE__, message); \
    tests_failed++; \
    return; } \
} while (0)

#define ASSERT_EQUALS(expected, actual) do { \
    if ((expected) != (actual)) { \
        snprintf(last_failure, sizeof(last_failure), \
            "%s (line %d): Expected %d, got %d", __FILE__, __LINE__, (expected), (actual)); \
        tests_failed++; \
        return; } \
} while (0)

#define ASSERT_STR_EQUALS(expected, actual) do { \
    if (strcmp((expected), (actual)) != 0) { \
        snprintf(last_failure, sizeof(last_failure), \
            "%s (line %d): Expected \"%s\", got \"%s\"", __FILE__, __LINE__, (expected), (actual)); \
        tests_failed++; \
        return; } \
} while (0)

/* Macros for test definition and running */
#define TEST(test_name) void test_name(void)
#define RUN_TEST(test_name) do { \
    printf("- %-40s", #test_name); \
    tests_run++; \
    test_name(); \
    if (strlen(last_failure) > 0) { \
        printf("FAIL\n  %s\n", last_failure); \
        last_failure[0] = '\0'; \
    } else { \
        printf("PASS\n"); \
    } \
} while (0)

#define RUN_TESTS(...) do { \
    void (*tests[])(void) = { __VA_ARGS__ }; \
    size_t num_tests = sizeof(tests) / sizeof(tests[0]); \
    for (size_t i = 0; i < num_tests; i++) { \
        tests[i](); \
    } \
} while (0)

/* Test suite summary */
#define PRINT_TEST_SUMMARY() do { \
    printf("\n%d test%s run, %d failed\n", \
        tests_run, tests_run == 1 ? "" : "s", tests_failed); \
    return tests_failed; \
} while (0)

#endif /* TEST_FRAMEWORK_H */
