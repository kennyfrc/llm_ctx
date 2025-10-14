#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>

// Forward declaration
struct Arena;

typedef struct
{
    char* name;                // Template name
    char* system_prompt_file;  // System prompt file path
    char* response_guide_file; // Response guide file path
} ConfigTemplate;

typedef struct
{
    char* system_prompt_file;  // NULL → none
    char* response_guide_file; // NULL → none
    int copy_to_clipboard;     // tri-state: -1=unset, 0=false, 1=true
    size_t token_budget;       // 0 → unset
    ConfigTemplate* templates; // Array of named templates
    size_t template_count;     // Number of templates
    // FileRank weight configuration
    double filerank_weight_path;    // -1.0 → unset
    double filerank_weight_content; // -1.0 → unset
    double filerank_weight_size;    // -1.0 → unset
    double filerank_weight_tfidf;   // -1.0 → unset
    char* filerank_cutoff;          // NULL → unset
} ConfigSettings;

// Load config from standard locations
bool config_load(ConfigSettings* settings, struct Arena* arena);

// Skip config loading if environment variable is set
bool config_should_skip(void);

// Expand ~ in paths for home directory resolution
char* config_expand_path(const char* path, struct Arena* arena);

// Print config for debugging
void config_debug_print(const ConfigSettings* settings);

// Find template by name
ConfigTemplate* config_find_template(const ConfigSettings* settings, const char* name);

#endif // CONFIG_H