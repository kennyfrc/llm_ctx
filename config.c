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

static bool parse_toml_file(const char *path, ConfigSettings *settings, Arena *arena);

// Skip config loading if environment variable is set
bool config_should_skip(void) {
    const char *no_config = getenv("LLM_CTX_NO_CONFIG");
    if (no_config && strcmp(no_config, "1") == 0) {
        debug_printf("Config loading disabled by LLM_CTX_NO_CONFIG=1");
        return true;
    }
    
    return false;
}

// Expand ~ in paths for home directory resolution
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
        size_t total_len = home_len + path_len;
        char *expanded = arena_push_array_safe(arena, char, total_len);
        if (!expanded) {
            return arena_strdup_safe(arena, path);
        }

        int written = snprintf(expanded, total_len, "%s%s", home_dir, path + 1);
        if (written < 0 || (size_t)written >= total_len) {
            return arena_strdup_safe(arena, path);
        }

        return expanded;
    } else {
        // ~username/... format - not supported for now
        return arena_strdup_safe(arena, path);
    }
}

// Attempt to load config from a single path
static bool try_load_config(const char *path, ConfigSettings *settings, Arena *arena) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    
    if (!S_ISREG(st.st_mode)) {
        (void)fprintf(stderr, "Warning: %s is not a regular file\n", path);
        return false;
    }
    
    debug_printf("Found config file: %s", path);
    
    return parse_toml_file(path, settings, arena);
}

// Load config from standard locations in priority order
bool config_load(ConfigSettings *settings, Arena *arena) {
    if (!settings || !arena) {
        return false;
    }
    
    memset(settings, 0, sizeof(ConfigSettings));
    settings->copy_to_clipboard = -1;
    settings->token_budget = 0;
    settings->templates = NULL;
    settings->template_count = 0;
    settings->filerank_weight_path = -1.0;
    settings->filerank_weight_content = -1.0;
    settings->filerank_weight_size = -1.0;
    settings->filerank_weight_tfidf = -1.0;
    settings->filerank_cutoff = NULL;
    
    if (config_should_skip()) {
        return false;
    }
    
    const char *paths_to_try[3] = {NULL, NULL, NULL};
    int path_count = 0;
    
    const char *explicit_path = getenv("LLM_CTX_CONFIG");
    if (explicit_path) {
        paths_to_try[path_count++] = explicit_path;
    }
    
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    char xdg_path[PATH_MAX];
    if (xdg_config) {
        int written = snprintf(xdg_path, sizeof(xdg_path), "%s/llm_ctx/config.toml", xdg_config);
        if (written >= 0 && (size_t)written < sizeof(xdg_path)) {
            paths_to_try[path_count++] = xdg_path;
        } else {
            debug_printf("Skipping truncated XDG config path");
        }
    }
    
    const char *home = getenv("HOME");
    char home_path[PATH_MAX];
    if (home) {
        int written = snprintf(home_path, sizeof(home_path), "%s/.config/llm_ctx/config.toml", home);
        if (written >= 0 && (size_t)written < sizeof(home_path)) {
            paths_to_try[path_count++] = home_path;
        } else {
            debug_printf("Skipping truncated HOME config path");
        }
    }
    
    for (int i = 0; i < path_count; i++) {
        if (try_load_config(paths_to_try[i], settings, arena)) {
            return true;
        }
    }
    
    debug_printf("No config file found");
    return false;
}

// Print config for debugging
void config_debug_print(const ConfigSettings *settings) {
    if (!settings) return;
    
    (void)fprintf(stderr, "[DEBUG] ConfigSettings:\n");
    (void)fprintf(stderr, "[DEBUG]   system_prompt_file: %s\n",
                  settings->system_prompt_file ? settings->system_prompt_file : "(null)");
    (void)fprintf(stderr, "[DEBUG]   response_guide_file: %s\n",
                  settings->response_guide_file ? settings->response_guide_file : "(null)");
    (void)fprintf(stderr, "[DEBUG]   copy_to_clipboard: %d\n", settings->copy_to_clipboard);
    (void)fprintf(stderr, "[DEBUG]   token_budget: %zu\n", settings->token_budget);
    (void)fprintf(stderr, "[DEBUG]   filerank_weight_path: %.2f\n", settings->filerank_weight_path);
    (void)fprintf(stderr, "[DEBUG]   filerank_weight_content: %.2f\n", settings->filerank_weight_content);
    (void)fprintf(stderr, "[DEBUG]   filerank_weight_size: %.2f\n", settings->filerank_weight_size);
    (void)fprintf(stderr, "[DEBUG]   filerank_weight_tfidf: %.2f\n", settings->filerank_weight_tfidf);
    (void)fprintf(stderr, "[DEBUG]   filerank_cutoff: %s\n",
                  settings->filerank_cutoff ? settings->filerank_cutoff : "(unset)");
    (void)fprintf(stderr, "[DEBUG]   template_count: %zu\n", settings->template_count);
    
    for (size_t i = 0; i < settings->template_count; i++) {
        ConfigTemplate *tmpl = &settings->templates[i];
        (void)fprintf(stderr, "[DEBUG]   Template '%s':\n", tmpl->name);
        (void)fprintf(stderr, "[DEBUG]     system_prompt_file: %s\n",
                      tmpl->system_prompt_file ? tmpl->system_prompt_file : "(null)");
        (void)fprintf(stderr, "[DEBUG]     response_guide_file: %s\n",
                      tmpl->response_guide_file ? tmpl->response_guide_file : "(null)");
    }
}

// Parse templates using state machine for [templates.name] sections
static void parse_templates(FILE *fp, ConfigSettings *settings, Arena *arena) {
    enum {
        STATE_TOP,
        STATE_IN_TEMPLATES,
        STATE_IN_TEMPLATE
    } state = STATE_TOP;
    
    char line[1024];
    char current_template[256] = {0};
    ConfigTemplate *current_tmpl = NULL;
    
    size_t template_count = 0;
    long start_pos = ftell(fp);
    if (start_pos < 0) {
        return;
    }
    
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p && isspace(*p)) p++;
        
        if (*p == '#' || *p == '\0') continue;
        
        if (*p == '[' && strncmp(p, "[templates.", 11) == 0) {
            template_count++;
        }
    }
    
    if (template_count > 0) {
        settings->templates = arena_push_array_safe(arena, ConfigTemplate, template_count);
        if (!settings->templates) {
            settings->template_count = 0;
            return;
        }
        settings->template_count = 0;
    }
    
    if (fseek(fp, start_pos, SEEK_SET) != 0) {
        return;
    }
    
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
                int written = snprintf(current_template, sizeof(current_template), "%s", p + 10);
                if (written < 0 || (size_t)written >= sizeof(current_template)) {
                    debug_printf("Skipping oversize template name");
                    current_template[0] = '\0';
                    current_tmpl = NULL;
                    state = STATE_IN_TEMPLATES;
                    continue;
                }

                // Create new template
                if (!settings->templates) {
                    state = STATE_IN_TEMPLATES;
                    current_tmpl = NULL;
                    continue;
                }

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

static bool parse_toml_file(const char *path, ConfigSettings *settings, Arena *arena) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return false;
    }
    
    parse_templates(fp, settings, arena);
    if (fseek(fp, 0, SEEK_SET) != 0) {
        (void)fclose(fp);
        return false;
    }
    
    char temp_path[PATH_MAX];
    int written = snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    if (written < 0 || (size_t)written >= sizeof(temp_path)) {
        (void)fclose(fp);
        return false;
    }
    FILE *temp_fp = fopen(temp_path, "w");
    if (!temp_fp) {
        (void)fclose(fp);
        return false;
    }
    
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
            if (fputs(line, temp_fp) == EOF) {
                (void)fclose(fp);
                (void)fclose(temp_fp);
                unlink(temp_path);
                return false;
            }
        }
    }
    if (fclose(temp_fp) != 0) {
        (void)fclose(fp);
        unlink(temp_path);
        return false;
    }

    temp_fp = fopen(temp_path, "r");
    if (!temp_fp) {
        (void)fclose(fp);
        unlink(temp_path);
        return false;
    }

    char errbuf[200];
    toml_table_t *conf = toml_parse_file(temp_fp, errbuf, sizeof(errbuf));
    if (fclose(temp_fp) != 0) {
        (void)fclose(fp);
        unlink(temp_path);
        return false;
    }
    unlink(temp_path);

    if (!conf) {
        (void)fprintf(stderr, "Error parsing config file %s: %s\n", path, errbuf);
        (void)fclose(fp);
        return false;
    }
    
    toml_datum_t system_prompt = toml_string_in(conf, "system_prompt_file");
    if (system_prompt.ok) {
        settings->system_prompt_file = arena_strdup_safe(arena, system_prompt.u.s);
        free(system_prompt.u.s);
    }
    
    toml_datum_t response_guide = toml_string_in(conf, "response_guide_file");
    if (response_guide.ok) {
        settings->response_guide_file = arena_strdup_safe(arena, response_guide.u.s);
        free(response_guide.u.s);
    }
    
    toml_datum_t copy_clipboard = toml_bool_in(conf, "copy_to_clipboard");
    if (copy_clipboard.ok) {
        settings->copy_to_clipboard = copy_clipboard.u.b ? 1 : 0;
    }
    
    toml_datum_t token_budget = toml_int_in(conf, "token_budget");
    if (token_budget.ok) {
        settings->token_budget = (size_t)token_budget.u.i;
    }
    
    toml_datum_t weight_path = toml_int_in(conf, "filerank_weight_path_x100");
    if (weight_path.ok) {
        settings->filerank_weight_path = (double)weight_path.u.i / 100.0;
    }
    
    toml_datum_t weight_content = toml_int_in(conf, "filerank_weight_content_x100");
    if (weight_content.ok) {
        settings->filerank_weight_content = (double)weight_content.u.i / 100.0;
    }
    
    toml_datum_t weight_size = toml_int_in(conf, "filerank_weight_size_x100");
    if (weight_size.ok) {
        settings->filerank_weight_size = (double)weight_size.u.i / 100.0;
    }
    
    toml_datum_t weight_tfidf = toml_int_in(conf, "filerank_weight_tfidf_x100");
    if (weight_tfidf.ok) {
        settings->filerank_weight_tfidf = (double)weight_tfidf.u.i / 100.0;
    }
    
    toml_datum_t cutoff = toml_string_in(conf, "filerank_cutoff");
    if (cutoff.ok) {
        settings->filerank_cutoff = arena_strdup_safe(arena, cutoff.u.s);
        free(cutoff.u.s);
    }
    
    toml_free(conf);
    if (fclose(fp) != 0) {
        return false;
    }
    return true;
}

// Find template by name
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