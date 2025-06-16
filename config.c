#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include "toml.h"
#include "config.h"
#include "arena.h"
#include "debug.h"

// Forward declaration for TOML parsing (will be implemented in slice 1)
static bool parse_toml_file(const char *path, ConfigSettings *settings, Arena *arena);

// Check if configuration loading should be skipped
bool config_should_skip(void) {
    // Check environment variable
    const char *no_config = getenv("LLM_CTX_NO_CONFIG");
    if (no_config && strcmp(no_config, "1") == 0) {
        debug_printf("Config loading disabled by LLM_CTX_NO_CONFIG=1");
        return true;
    }
    
    // Note: --ignore-config flag check will be done in main.c
    return false;
}

// Expand tilde (~) in file paths using arena allocation
char *config_expand_path(const char *path, Arena *arena) {
    if (!path || path[0] != '~') {
        // No tilde to expand, return a copy
        return arena_strdup_safe(arena, path);
    }
    
    const char *home_dir = NULL;
    
    if (path[1] == '/' || path[1] == '\0') {
        // Simple ~ or ~/...
        home_dir = getenv("HOME");
        if (!home_dir) {
            struct passwd *pw = getpwuid(getuid());
            if (pw) {
                home_dir = pw->pw_dir;
            }
        }
        
        if (!home_dir) {
            // Can't expand, return as-is
            return arena_strdup_safe(arena, path);
        }
        
        // Construct expanded path
        size_t home_len = strlen(home_dir);
        size_t path_len = strlen(path);
        size_t total_len = home_len + path_len; // -1 for ~, +1 for \0
        
        char *expanded = arena_push_array_safe(arena, char, total_len);
        strcpy(expanded, home_dir);
        strcat(expanded, path + 1); // Skip the ~
        
        return expanded;
    } else {
        // ~username/... format - not supported for now
        return arena_strdup_safe(arena, path);
    }
}

// Try to load config from a specific path
static bool try_load_config(const char *path, ConfigSettings *settings, Arena *arena) {
    struct stat st;
    if (stat(path, &st) != 0) {
        // File doesn't exist
        return false;
    }
    
    if (!S_ISREG(st.st_mode)) {
        // Not a regular file
        fprintf(stderr, "Warning: %s is not a regular file\n", path);
        return false;
    }
    
    debug_printf("Found config file: %s", path);
    
    // Parse the TOML file
    return parse_toml_file(path, settings, arena);
}

// Load configuration from default locations
bool config_load(ConfigSettings *settings, Arena *arena) {
    if (!settings || !arena) {
        return false;
    }
    
    // Initialize settings to defaults
    memset(settings, 0, sizeof(ConfigSettings));
    settings->copy_to_clipboard = -1; // unset
    settings->token_budget = 0; // unset
    
    // Check if we should skip config loading
    if (config_should_skip()) {
        return false;
    }
    
    // Try paths in order
    const char *paths_to_try[3] = {NULL, NULL, NULL};
    int path_count = 0;
    
    // 1. $LLM_CTX_CONFIG (explicit path)
    const char *explicit_path = getenv("LLM_CTX_CONFIG");
    if (explicit_path) {
        paths_to_try[path_count++] = explicit_path;
    }
    
    // 2. $XDG_CONFIG_HOME/llm_ctx/config.toml
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    char xdg_path[PATH_MAX];
    if (xdg_config) {
        snprintf(xdg_path, sizeof(xdg_path), "%s/llm_ctx/config.toml", xdg_config);
        paths_to_try[path_count++] = xdg_path;
    }
    
    // 3. ~/.config/llm_ctx/config.toml
    const char *home = getenv("HOME");
    char home_path[PATH_MAX];
    if (home) {
        snprintf(home_path, sizeof(home_path), "%s/.config/llm_ctx/config.toml", home);
        paths_to_try[path_count++] = home_path;
    }
    
    // Try each path
    for (int i = 0; i < path_count; i++) {
        if (try_load_config(paths_to_try[i], settings, arena)) {
            return true;
        }
    }
    
    // No config found
    debug_printf("No config file found");
    return false;
}

// Debug helper to print loaded config
void config_debug_print(const ConfigSettings *settings) {
    if (!settings) return;
    
    fprintf(stderr, "[DEBUG] ConfigSettings:\n");
    fprintf(stderr, "[DEBUG]   system_prompt_file: %s\n", 
            settings->system_prompt_file ? settings->system_prompt_file : "(null)");
    fprintf(stderr, "[DEBUG]   response_guide_file: %s\n", 
            settings->response_guide_file ? settings->response_guide_file : "(null)");
    fprintf(stderr, "[DEBUG]   copy_to_clipboard: %d\n", settings->copy_to_clipboard);
    fprintf(stderr, "[DEBUG]   token_budget: %zu\n", settings->token_budget);
}

// Parse TOML configuration file
static bool parse_toml_file(const char *path, ConfigSettings *settings, Arena *arena) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return false;
    }
    
    char errbuf[200];
    toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    
    if (!conf) {
        fprintf(stderr, "Error parsing config file %s: %s\n", path, errbuf);
        return false;
    }
    
    // Parse system_prompt_file
    toml_datum_t system_prompt = toml_string_in(conf, "system_prompt_file");
    if (system_prompt.ok) {
        settings->system_prompt_file = arena_strdup_safe(arena, system_prompt.u.s);
        free(system_prompt.u.s);
    }
    
    // Parse response_guide_file
    toml_datum_t response_guide = toml_string_in(conf, "response_guide_file");
    if (response_guide.ok) {
        settings->response_guide_file = arena_strdup_safe(arena, response_guide.u.s);
        free(response_guide.u.s);
    }
    
    // Parse copy_to_clipboard
    toml_datum_t copy_clipboard = toml_bool_in(conf, "copy_to_clipboard");
    if (copy_clipboard.ok) {
        settings->copy_to_clipboard = copy_clipboard.u.b ? 1 : 0;
    }
    
    // Parse token_budget
    toml_datum_t token_budget = toml_int_in(conf, "token_budget");
    if (token_budget.ok) {
        settings->token_budget = (size_t)token_budget.u.i;
    }
    
    toml_free(conf);
    return true;
}