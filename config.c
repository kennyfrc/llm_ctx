#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <ctype.h>
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
    settings->templates = NULL;
    settings->template_count = 0;
    
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
    fprintf(stderr, "[DEBUG]   template_count: %zu\n", settings->template_count);
    
    for (size_t i = 0; i < settings->template_count; i++) {
        ConfigTemplate *tmpl = &settings->templates[i];
        fprintf(stderr, "[DEBUG]   Template '%s':\n", tmpl->name);
        fprintf(stderr, "[DEBUG]     system_prompt_file: %s\n", 
                tmpl->system_prompt_file ? tmpl->system_prompt_file : "(null)");
        fprintf(stderr, "[DEBUG]     response_guide_file: %s\n", 
                tmpl->response_guide_file ? tmpl->response_guide_file : "(null)");
    }
}

// Parse templates from TOML file using a stack-based state machine
static void parse_templates(FILE *fp, ConfigSettings *settings, Arena *arena) {
    // State machine states
    enum {
        STATE_TOP,              // Top level
        STATE_IN_TEMPLATES,     // Inside [templates]
        STATE_IN_TEMPLATE       // Inside [templates.name]
    } state = STATE_TOP;
    
    char line[1024];
    char current_template[256] = {0};
    ConfigTemplate *current_tmpl = NULL;
    
    // Pre-scan to count templates
    size_t template_count = 0;
    long start_pos = ftell(fp);
    
    while (fgets(line, sizeof(line), fp)) {
        // Skip whitespace
        char *p = line;
        while (*p && isspace(*p)) p++;
        
        // Skip comments and empty lines
        if (*p == '#' || *p == '\0') continue;
        
        // Check for [templates.name] pattern
        if (*p == '[' && strncmp(p, "[templates.", 11) == 0) {
            template_count++;
        }
    }
    
    // Allocate templates array
    if (template_count > 0) {
        settings->templates = arena_push_array_safe(arena, ConfigTemplate, template_count);
        settings->template_count = 0;
    }
    
    // Reset to start and parse
    fseek(fp, start_pos, SEEK_SET);
    
    while (fgets(line, sizeof(line), fp)) {
        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        
        // Skip whitespace
        char *p = line;
        while (*p && isspace(*p)) p++;
        
        // Skip comments and empty lines
        if (*p == '#' || *p == '\0') continue;
        
        // Handle section headers
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (!end) continue;
            *end = '\0';
            p++; // Skip '['
            
            if (strcmp(p, "templates") == 0) {
                state = STATE_IN_TEMPLATES;
            } else if (strncmp(p, "templates.", 10) == 0) {
                state = STATE_IN_TEMPLATE;
                strcpy(current_template, p + 10);
                
                // Create new template
                current_tmpl = &settings->templates[settings->template_count++];
                current_tmpl->name = arena_strdup_safe(arena, current_template);
                current_tmpl->system_prompt_file = NULL;
                current_tmpl->response_guide_file = NULL;
            } else {
                state = STATE_TOP;
                current_tmpl = NULL;
            }
            continue;
        }
        
        // Handle key-value pairs
        if (state == STATE_IN_TEMPLATE && current_tmpl) {
            char *eq = strchr(p, '=');
            if (!eq) continue;
            
            *eq = '\0';
            char *key = p;
            char *value = eq + 1;
            
            // Trim whitespace from key
            char *key_end = key + strlen(key) - 1;
            while (key_end > key && isspace(*key_end)) *key_end-- = '\0';
            
            // Trim whitespace from value
            while (*value && isspace(*value)) value++;
            
            // Remove quotes if present
            if (*value == '"') {
                value++;
                char *quote_end = strrchr(value, '"');
                if (quote_end) *quote_end = '\0';
            }
            
            // Store the values
            if (strcmp(key, "system_prompt_file") == 0) {
                current_tmpl->system_prompt_file = arena_strdup_safe(arena, value);
            } else if (strcmp(key, "response_guide_file") == 0) {
                current_tmpl->response_guide_file = arena_strdup_safe(arena, value);
            }
        }
    }
}

// Parse TOML configuration file
static bool parse_toml_file(const char *path, ConfigSettings *settings, Arena *arena) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return false;
    }
    
    // Parse templates first using our custom parser
    parse_templates(fp, settings, arena);
    rewind(fp);
    
    // Create a temporary file without template sections for TOML parser
    char temp_path[PATH_MAX];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    FILE *temp_fp = fopen(temp_path, "w");
    if (!temp_fp) {
        fclose(fp);
        return false;
    }
    
    // Copy config file excluding template sections
    char line[1024];
    int skip_until_next_section = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '[') {
            if (strstr(line, "[templates.") != NULL) {
                skip_until_next_section = 1;
                continue;
            } else {
                skip_until_next_section = 0;
            }
        }
        if (!skip_until_next_section) {
            fputs(line, temp_fp);
        }
    }
    fclose(temp_fp);
    
    // Parse the temp file with TOML library
    temp_fp = fopen(temp_path, "r");
    if (!temp_fp) {
        fclose(fp);
        unlink(temp_path);
        return false;
    }
    
    char errbuf[200];
    toml_table_t *conf = toml_parse_file(temp_fp, errbuf, sizeof(errbuf));
    fclose(temp_fp);
    unlink(temp_path);
    
    if (!conf) {
        fprintf(stderr, "Error parsing config file %s: %s\n", path, errbuf);
        fclose(fp);
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
    fclose(fp);
    return true;
}

// Find a template by name
ConfigTemplate *config_find_template(const ConfigSettings *settings, const char *name) {
    if (!settings || !name || !settings->templates) {
        return NULL;
    }
    
    for (size_t i = 0; i < settings->template_count; i++) {
        if (strcmp(settings->templates[i].name, name) == 0) {
            return &settings->templates[i];
        }
    }
    
    return NULL;
}