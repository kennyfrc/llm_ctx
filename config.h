#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>

/* Configuration loading with XDG_CONFIG_HOME support */
struct Arena;

typedef struct
{
    char* name;
    char* system_prompt_file;
    char* response_guide_file;
} ConfigTemplate;

typedef struct
{
    char* system_prompt_file;
    char* response_guide_file;
    int copy_to_clipboard;
    size_t token_budget;
    ConfigTemplate* templates;
    size_t template_count;
    double filerank_weight_path;
    double filerank_weight_content;
    double filerank_weight_size;
    double filerank_weight_tfidf;
    char* filerank_cutoff;
} ConfigSettings;

bool config_load(ConfigSettings* settings, struct Arena* arena);
bool config_should_skip(void);
char* config_expand_path(const char* path, struct Arena* arena);
void config_debug_print(const ConfigSettings* settings);
ConfigTemplate* config_find_template(const ConfigSettings* settings, const char* name);

#endif // CONFIG_H