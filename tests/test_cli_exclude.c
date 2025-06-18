#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include <stdbool.h>
#include "test_framework.h"
#include "../arena.h"

/**
 * Unit tests for CLI exclude pattern functionality
 * 
 * These tests focus on:
 * - Pattern matching logic
 * - Basename vs full path matching
 * - Various glob patterns
 */

/* Mock globals and functions needed for testing */
#define MAX_CLI_EXCLUDE_PATTERNS 128
static char *g_cli_exclude_patterns[MAX_CLI_EXCLUDE_PATTERNS];
static int   g_cli_exclude_count = 0;
static Arena g_arena;

/**
 * Add a CLI exclude pattern to the global list
 */
static void add_cli_exclude_pattern(const char *raw) {
    if (g_cli_exclude_count >= MAX_CLI_EXCLUDE_PATTERNS) {
        return;
    }
    g_cli_exclude_patterns[g_cli_exclude_count++] = 
        arena_strdup_safe(&g_arena, raw);
}

/**
 * Check if a path matches any CLI exclude pattern
 */
static bool matches_cli_exclude(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    
    for (int i = 0; i < g_cli_exclude_count; ++i) {
        const char *pat = g_cli_exclude_patterns[i];
        
        /* Handle ** patterns for recursive directory matching */
        const char *double_star = strstr(pat, "**");
        if (double_star) {
            /* Pattern contains **, needs special handling */
            size_t prefix_len = double_star - pat;
            
            /* Check if path starts with the prefix before ** */
            if (prefix_len > 0 && strncmp(path, pat, prefix_len) != 0) {
                continue;
            }
            
            /* If pattern ends with double-star or slash-double-star, match any path with that prefix */
            if (double_star[2] == '\0' || 
                (double_star[2] == '/' && double_star[3] == '\0')) {
                return true;
            }
            
            /* If pattern is like dir/double-star, match anything under dir/ */
            if (prefix_len > 0 && pat[prefix_len-1] == '/') {
                return true;
            }
            
            /* For patterns like dir/double-star/file, use simple substring match for now */
            const char *suffix = double_star + 2;
            if (*suffix == '/') suffix++;
            if (*suffix && strstr(path + prefix_len, suffix)) {
                return true;
            }
        } else {
            /* Standard fnmatch for patterns without ** */
            if (fnmatch(pat, path, FNM_PATHNAME | FNM_PERIOD) == 0 ||
                fnmatch(pat, base, 0) == 0)
                return true;
        }
    }
    return false;
}

/* Test functions */
TEST(test_exclude_exact_filename) {
    g_cli_exclude_count = 0;
    add_cli_exclude_pattern("test.log");
    
    ASSERT("Should match exact filename", matches_cli_exclude("test.log"));
    ASSERT("Should match filename in subdirectory", matches_cli_exclude("src/test.log"));
    ASSERT("Should not match different filename", !matches_cli_exclude("test.txt"));
}

TEST(test_exclude_wildcard_extension) {
    g_cli_exclude_count = 0;
    add_cli_exclude_pattern("*.min.js");
    
    ASSERT("Should match .min.js files", matches_cli_exclude("app.min.js"));
    ASSERT("Should match in subdirectory", matches_cli_exclude("dist/bundle.min.js"));
    ASSERT("Should not match regular .js files", !matches_cli_exclude("app.js"));
}

TEST(test_exclude_directory_pattern) {
    g_cli_exclude_count = 0;
    add_cli_exclude_pattern("node_modules/**");
    
    ASSERT("Should match files in node_modules", matches_cli_exclude("node_modules/package/index.js"));
    ASSERT("Should match nested files", matches_cli_exclude("node_modules/package/lib/util.js"));
    ASSERT("Should not match sibling directories", !matches_cli_exclude("src/index.js"));
}

TEST(test_exclude_nested_directory_pattern) {
    g_cli_exclude_count = 0;
    add_cli_exclude_pattern("src/generated/**");
    
    ASSERT("Should match files in src/generated", matches_cli_exclude("src/generated/api.js"));
    ASSERT("Should match deeply nested files", matches_cli_exclude("src/generated/models/user.js"));
    ASSERT("Should not match src files outside generated", !matches_cli_exclude("src/main.js"));
    ASSERT("Should not match other generated directories", !matches_cli_exclude("lib/generated/api.js"));
}

TEST(test_exclude_hidden_files) {
    g_cli_exclude_count = 0;
    add_cli_exclude_pattern(".*");
    
    ASSERT("Should match hidden files", matches_cli_exclude(".gitignore"));
    ASSERT("Should match hidden files in subdirectories", matches_cli_exclude("src/.eslintrc"));
    ASSERT("Should not match regular files", !matches_cli_exclude("README.md"));
}

TEST(test_exclude_specific_subdirectory) {
    g_cli_exclude_count = 0;
    add_cli_exclude_pattern("javascripts/lib/cami/**");
    
    ASSERT("Should match files in specific subdirectory", 
           matches_cli_exclude("javascripts/lib/cami/index.js"));
    ASSERT("Should match nested files in subdirectory", 
           matches_cli_exclude("javascripts/lib/cami/src/util.js"));
    ASSERT("Should not match files in parent directory", 
           !matches_cli_exclude("javascripts/lib/other.js"));
    ASSERT("Should not match files in sibling directory", 
           !matches_cli_exclude("javascripts/lib/other/index.js"));
}

TEST(test_exclude_multiple_patterns) {
    g_cli_exclude_count = 0;
    add_cli_exclude_pattern("*.log");
    add_cli_exclude_pattern("*.tmp");
    add_cli_exclude_pattern("build/**");
    
    ASSERT("Should match first pattern", matches_cli_exclude("error.log"));
    ASSERT("Should match second pattern", matches_cli_exclude("cache.tmp"));
    ASSERT("Should match third pattern", matches_cli_exclude("build/output.js"));
    ASSERT("Should not match unrelated files", !matches_cli_exclude("src/main.js"));
}

TEST(test_exclude_pattern_with_brackets) {
    g_cli_exclude_count = 0;
    add_cli_exclude_pattern("*.[oa]");
    
    ASSERT("Should match .o files", matches_cli_exclude("main.o"));
    ASSERT("Should match .a files", matches_cli_exclude("libfoo.a"));
    ASSERT("Should not match other extensions", !matches_cli_exclude("main.c"));
}

TEST(test_exclude_pattern_limit) {
    g_cli_exclude_count = 0;
    
    /* Add maximum allowed patterns */
    for (int i = 0; i < MAX_CLI_EXCLUDE_PATTERNS; i++) {
        char pattern[32];
        snprintf(pattern, sizeof(pattern), "pattern%d.txt", i);
        add_cli_exclude_pattern(pattern);
    }
    
    /* Try to add one more */
    add_cli_exclude_pattern("overflow.txt");
    
    /* Should only have MAX_CLI_EXCLUDE_PATTERNS patterns */
    ASSERT_EQUALS(MAX_CLI_EXCLUDE_PATTERNS, g_cli_exclude_count);
    
    /* First pattern should work */
    ASSERT("First pattern should match", matches_cli_exclude("pattern0.txt"));
    
    /* Overflow pattern should not be added */
    ASSERT("Overflow pattern should not match", !matches_cli_exclude("overflow.txt"));
}

TEST(test_exclude_basename_matching) {
    g_cli_exclude_count = 0;
    add_cli_exclude_pattern("Makefile");
    
    ASSERT("Should match basename in root", matches_cli_exclude("Makefile"));
    ASSERT("Should match basename in subdirectory", matches_cli_exclude("src/Makefile"));
    ASSERT("Should match basename in deep subdirectory", matches_cli_exclude("src/lib/test/Makefile"));
}

/* Main test runner */
int main(void) {
    printf("Running CLI exclude pattern tests...\n\n");
    
    /* Initialize arena for testing */
    g_arena = arena_create(64 * 1024); /* 64KB for tests */
    
    RUN_TEST(test_exclude_exact_filename);
    RUN_TEST(test_exclude_wildcard_extension);
    RUN_TEST(test_exclude_directory_pattern);
    RUN_TEST(test_exclude_nested_directory_pattern);
    RUN_TEST(test_exclude_hidden_files);
    RUN_TEST(test_exclude_specific_subdirectory);
    RUN_TEST(test_exclude_multiple_patterns);
    RUN_TEST(test_exclude_pattern_with_brackets);
    RUN_TEST(test_exclude_pattern_limit);
    RUN_TEST(test_exclude_basename_matching);
    
    /* Cleanup */
    arena_destroy(&g_arena);
    
    PRINT_TEST_SUMMARY();
}