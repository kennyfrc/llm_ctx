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

void test_config_should_skip(void) {
    printf("test_config_should_skip...\n");
    
    // Test with no environment variable
    unsetenv("LLM_CTX_NO_CONFIG");
    assert(!config_should_skip());
    
    // Test with LLM_CTX_NO_CONFIG=1
    setenv("LLM_CTX_NO_CONFIG", "1", 1);
    assert(config_should_skip());
    
    // Test with LLM_CTX_NO_CONFIG=0
    setenv("LLM_CTX_NO_CONFIG", "0", 1);
    assert(!config_should_skip());
    
    // Clean up
    unsetenv("LLM_CTX_NO_CONFIG");
    
    printf("✓ test_config_should_skip passed\n");
}

void test_config_expand_path(void) {
    printf("test_config_expand_path...\n");
    
    Arena arena = arena_create(1024 * 1024);
    const char *home = getenv("HOME");
    
    // Test no tilde
    char *result = config_expand_path("/absolute/path", &arena);
    assert(strcmp(result, "/absolute/path") == 0);
    
    // Test simple tilde
    result = config_expand_path("~/test", &arena);
    char expected[256];
    snprintf(expected, sizeof(expected), "%s/test", home);
    assert(strcmp(result, expected) == 0);
    
    // Test tilde alone
    result = config_expand_path("~", &arena);
    assert(strcmp(result, home) == 0);
    
    arena_destroy(&arena);
    
    printf("✓ test_config_expand_path passed\n");
}

void test_config_load_no_file(void) {
    printf("test_config_load_no_file...\n");
    
    Arena arena = arena_create(1024 * 1024);
    ConfigSettings settings = {0};
    
    // Ensure no config file exists
    unsetenv("LLM_CTX_CONFIG");
    unsetenv("XDG_CONFIG_HOME");
    
    // Should return false when no config file found
    bool loaded = config_load(&settings, &arena);
    assert(!loaded);
    
    arena_destroy(&arena);
    
    printf("✓ test_config_load_no_file passed\n");
}

void test_config_load_explicit_path(void) {
    printf("test_config_load_explicit_path...\n");
    
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
    assert(loaded);
    assert(settings.system_prompt_file != NULL);
    assert(strcmp(settings.system_prompt_file, "~/prompts/sys.md") == 0);
    assert(settings.response_guide_file != NULL);
    assert(strcmp(settings.response_guide_file, "~/prompts/guide.md") == 0);
    assert(settings.copy_to_clipboard == 1);
    assert(settings.token_budget == 128000);
    
    // Clean up
    unlink(test_config);
    unsetenv("LLM_CTX_CONFIG");
    arena_destroy(&arena);
    
    printf("✓ test_config_load_explicit_path passed\n");
}

void test_config_load_xdg_path(void) {
    printf("test_config_load_xdg_path...\n");
    
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
    assert(loaded);
    assert(settings.system_prompt_file != NULL);
    assert(strcmp(settings.system_prompt_file, "/abs/path/sys.md") == 0);
    assert(settings.response_guide_file == NULL);
    assert(settings.copy_to_clipboard == -1);
    assert(settings.token_budget == 64000);
    
    // Clean up
    unlink(test_config);
    rmdir("/tmp/test_xdg_config/llm_ctx");
    rmdir(xdg_base);
    unsetenv("XDG_CONFIG_HOME");
    arena_destroy(&arena);
    
    printf("✓ test_config_load_xdg_path passed\n");
}

void test_config_load_partial(void) {
    printf("test_config_load_partial...\n");
    
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
    assert(loaded);
    assert(settings.system_prompt_file != NULL);
    assert(settings.response_guide_file == NULL);
    assert(settings.copy_to_clipboard == 0);
    assert(settings.token_budget == 0);
    
    // Clean up
    unlink(test_config);
    unsetenv("LLM_CTX_CONFIG");
    arena_destroy(&arena);
    
    printf("✓ test_config_load_partial passed\n");
}

void test_config_load_invalid_toml(void) {
    printf("test_config_load_invalid_toml...\n");
    
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
        assert(settings.system_prompt_file == NULL);
        // The invalid integer should not be parsed
        assert(settings.token_budget == 0);
    }
    
    // Clean up
    unlink(test_config);
    unsetenv("LLM_CTX_CONFIG");
    arena_destroy(&arena);
    
    printf("✓ test_config_load_invalid_toml passed\n");
}

int main(void) {
    printf("Running config tests...\n\n");
    
    // Save original environment
    char *orig_llm_ctx_config = getenv("LLM_CTX_CONFIG");
    char *orig_xdg_config = getenv("XDG_CONFIG_HOME");
    
    // Run tests
    test_config_should_skip();
    test_config_expand_path();
    test_config_load_no_file();
    test_config_load_explicit_path();
    test_config_load_xdg_path();
    test_config_load_partial();
    test_config_load_invalid_toml();
    
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
    
    printf("\nAll config tests passed! ✨\n");
    return 0;
}