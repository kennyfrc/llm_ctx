#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include "../config.h"
#include "../arena.h"
#include "test_framework.h"

// Arena implementation
#define ARENA_IMPLEMENTATION
#include "../arena.h"

// Helper to create a test config file
static void create_test_config(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) {
        perror("Failed to create test config file");
        exit(1);
    }
    fprintf(f, "%s", content);
    fclose(f);
}

// Helper to create directory if it doesn't exist
static void ensure_dir(const char *path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        mkdir(path, 0755);
    }
}

TEST(test_config_should_skip) {
    // Test with no environment variable
    unsetenv("LLM_CTX_NO_CONFIG");
    ASSERT("Should not skip when env not set", !config_should_skip());
    
    // Test with LLM_CTX_NO_CONFIG=1
    setenv("LLM_CTX_NO_CONFIG", "1", 1);
    ASSERT("Should skip when env=1", config_should_skip());
    
    // Test with LLM_CTX_NO_CONFIG=0
    setenv("LLM_CTX_NO_CONFIG", "0", 1);
    ASSERT("Should not skip when env=0", !config_should_skip());
    
    // Clean up
    unsetenv("LLM_CTX_NO_CONFIG");
}

TEST(test_config_expand_path) {
    Arena arena = arena_create(1024 * 1024);
    const char *home = getenv("HOME");
    
    // Test no tilde
    char *result = config_expand_path("/absolute/path", &arena);
    ASSERT_STR_EQUALS("/absolute/path", result);
    
    // Test simple tilde
    result = config_expand_path("~/test", &arena);
    char expected[256];
    snprintf(expected, sizeof(expected), "%s/test", home);
    ASSERT_STR_EQUALS(expected, result);
    
    // Test tilde alone
    result = config_expand_path("~", &arena);
    ASSERT_STR_EQUALS(home, result);
    
    arena_destroy(&arena);
}

TEST(test_config_load_no_file) {
    Arena arena = arena_create(1024 * 1024);
    ConfigSettings settings = {0};
    
    // Save current HOME
    char *orig_home = getenv("HOME");
    
    // Ensure no config file exists by clearing all paths
    unsetenv("LLM_CTX_CONFIG");
    unsetenv("XDG_CONFIG_HOME");
    setenv("HOME", "/nonexistent", 1);  // Set HOME to non-existent path
    
    // Should return false when no config file found
    bool loaded = config_load(&settings, &arena);
    ASSERT("Config should not load when no file exists", !loaded);
    
    // Restore HOME
    if (orig_home) {
        setenv("HOME", orig_home, 1);
    } else {
        unsetenv("HOME");
    }
    
    arena_destroy(&arena);
}

TEST(test_config_load_explicit_path) {
    Arena arena = arena_create(1024 * 1024);
    ConfigSettings settings = {0};
    
    // Create a test config file
    const char *test_config = "/tmp/test_llm_ctx_config.toml";
    create_test_config(test_config,
        "system_prompt_file = \"~/prompts/sys.md\"\n"
        "response_guide_file = \"~/prompts/guide.md\"\n"
        "copy_to_clipboard = true\n"
        "token_budget = 128000\n");
    
    // Set explicit path
    setenv("LLM_CTX_CONFIG", test_config, 1);
    
    // Should load successfully
    bool loaded = config_load(&settings, &arena);
    ASSERT("Config should load from explicit path", loaded);
    ASSERT("System prompt file should be set", settings.system_prompt_file != NULL);
    ASSERT_STR_EQUALS("~/prompts/sys.md", settings.system_prompt_file);
    ASSERT("Response guide file should be set", settings.response_guide_file != NULL);
    ASSERT_STR_EQUALS("~/prompts/guide.md", settings.response_guide_file);
    ASSERT_EQUALS(1, settings.copy_to_clipboard);
    ASSERT_EQUALS(128000, (int)settings.token_budget);
    
    // Clean up
    unlink(test_config);
    unsetenv("LLM_CTX_CONFIG");
    arena_destroy(&arena);
}

TEST(test_config_load_xdg_path) {
    Arena arena = arena_create(1024 * 1024);
    ConfigSettings settings = {0};
    
    // Create test XDG config directory and file
    const char *xdg_base = "/tmp/test_xdg_config";
    ensure_dir(xdg_base);
    ensure_dir("/tmp/test_xdg_config/llm_ctx");
    
    const char *test_config = "/tmp/test_xdg_config/llm_ctx/config.toml";
    create_test_config(test_config,
        "system_prompt_file = \"/abs/path/sys.md\"\n"
        "token_budget = 64000\n");
    
    // Set XDG_CONFIG_HOME
    unsetenv("LLM_CTX_CONFIG");
    setenv("XDG_CONFIG_HOME", xdg_base, 1);
    
    // Should load successfully
    bool loaded = config_load(&settings, &arena);
    ASSERT("Config should load from XDG path", loaded);
    ASSERT("System prompt file should be set", settings.system_prompt_file != NULL);
    ASSERT_STR_EQUALS("/abs/path/sys.md", settings.system_prompt_file);
    ASSERT("Response guide file should be null", settings.response_guide_file == NULL);
    ASSERT_EQUALS(-1, settings.copy_to_clipboard);
    ASSERT_EQUALS(64000, (int)settings.token_budget);
    
    // Clean up
    unlink(test_config);
    rmdir("/tmp/test_xdg_config/llm_ctx");
    rmdir(xdg_base);
    unsetenv("XDG_CONFIG_HOME");
    arena_destroy(&arena);
}

TEST(test_config_load_partial) {
    Arena arena = arena_create(1024 * 1024);
    ConfigSettings settings = {0};
    
    // Create a test config file with only some settings
    const char *test_config = "/tmp/test_llm_ctx_partial.toml";
    create_test_config(test_config,
        "system_prompt_file = \"test.md\"\n"
        "# response_guide_file not set\n"
        "copy_to_clipboard = false\n"
        "# token_budget not set\n");
    
    // Set explicit path
    setenv("LLM_CTX_CONFIG", test_config, 1);
    
    // Should load successfully with partial config
    bool loaded = config_load(&settings, &arena);
    ASSERT("Config should load with partial settings", loaded);
    ASSERT("System prompt file should be set", settings.system_prompt_file != NULL);
    ASSERT("Response guide file should be null", settings.response_guide_file == NULL);
    ASSERT_EQUALS(0, settings.copy_to_clipboard);
    ASSERT_EQUALS(0, (int)settings.token_budget);
    
    // Clean up
    unlink(test_config);
    unsetenv("LLM_CTX_CONFIG");
    arena_destroy(&arena);
}

TEST(test_config_load_invalid_toml) {
    Arena arena = arena_create(1024 * 1024);
    ConfigSettings settings = {0};
    
    // Create a test config file with invalid TOML
    const char *test_config = "/tmp/test_llm_ctx_invalid.toml";
    create_test_config(test_config,
        "system_prompt_file = \"test.md\n"  // Missing closing quote
        "token_budget = abc\n");  // Invalid value
    
    // Set explicit path
    setenv("LLM_CTX_CONFIG", test_config, 1);
    
    // Our simple parser might not fail on all invalid TOML
    // but it should at least not parse string values correctly
    bool loaded = config_load(&settings, &arena);
    
    // If it loaded, check that values are not parsed correctly
    if (loaded) {
        // The string with missing quote should not be parsed
        ASSERT("Invalid string should not be parsed", settings.system_prompt_file == NULL);
        // The invalid integer should not be parsed
        ASSERT_EQUALS(0, (int)settings.token_budget);
    }
    
    // Clean up
    unlink(test_config);
    unsetenv("LLM_CTX_CONFIG");
    arena_destroy(&arena);
}

int main(void) {
    printf("Running config tests\n");
    printf("===================\n");
    
    // Save original environment
    char *orig_llm_ctx_config = getenv("LLM_CTX_CONFIG");
    char *orig_xdg_config = getenv("XDG_CONFIG_HOME");
    
    // Run tests
    RUN_TEST(test_config_should_skip);
    RUN_TEST(test_config_expand_path);
    RUN_TEST(test_config_load_no_file);
    RUN_TEST(test_config_load_explicit_path);
    RUN_TEST(test_config_load_xdg_path);
    RUN_TEST(test_config_load_partial);
    RUN_TEST(test_config_load_invalid_toml);
    
    // Restore original environment
    if (orig_llm_ctx_config) {
        setenv("LLM_CTX_CONFIG", orig_llm_ctx_config, 1);
    } else {
        unsetenv("LLM_CTX_CONFIG");
    }
    if (orig_xdg_config) {
        setenv("XDG_CONFIG_HOME", orig_xdg_config, 1);
    } else {
        unsetenv("XDG_CONFIG_HOME");
    }
    
    printf("\n");
    PRINT_TEST_SUMMARY();
}