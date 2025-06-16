#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>

// Forward declaration
struct Arena;

typedef struct {
    char *name;                   // Template name
    char *system_prompt_file;     // System prompt file path
    char *response_guide_file;    // Response guide file path
} ConfigTemplate;

typedef struct {
    char *system_prompt_file;     // NULL → none
    char *response_guide_file;    // NULL → none
    int copy_to_clipboard;        // tri-state: -1=unset, 0=false, 1=true
    size_t token_budget;          // 0 → unset
    ConfigTemplate *templates;    // Array of named templates
    size_t template_count;        // Number of templates
    // FileRank weight configuration
    double filerank_weight_path;      // -1.0 → unset
    double filerank_weight_content;   // -1.0 → unset
    double filerank_weight_size;      // -1.0 → unset
    double filerank_weight_tfidf;     // -1.0 → unset
} ConfigSettings;

// Load configuration from default locations
// Returns true if config was loaded successfully, false otherwise
bool config_load(ConfigSettings *settings, struct Arena *arena);

// Check if configuration loading should be skipped
bool config_should_skip(void);

// Expand tilde (~) in file paths using arena allocation
char *config_expand_path(const char *path, struct Arena *arena);

// Debug helper to print loaded config
void config_debug_print(const ConfigSettings *settings);

// Find a template by name
ConfigTemplate *config_find_template(const ConfigSettings *settings, const char *name);

#endif // CONFIG_H