#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "tokenizer.h"
#include "arena.h"

/* Structure to hold per-file token counts */
typedef struct {
    const char *filename;
    size_t tokens;
} FileTokenCount;

/* Extract filename from a file context header line like "File: src/main.c" */
static const char *extract_filename(const char *line) {
    const char *prefix = "File: ";
    const char *start = strstr(line, prefix);
    if (!start) return NULL;
    
    start += strlen(prefix);
    /* Skip leading whitespace */
    while (*start == ' ' || *start == '\t') start++;
    
    return start;
}

/* Generate token diagnostics from content, counting tokens per file */
void generate_token_diagnostics(const char *content, const char *model, FILE *out, Arena *arena) {
    if (!content || !model || !out) return;
    
    /* First count total tokens */
    size_t total_tokens = llm_count_tokens(content, model);
    if (total_tokens == SIZE_MAX) {
        fprintf(out, "Error: Token counting unavailable\n");
        return;
    }
    
    /* Array to store file token counts */
    FileTokenCount *files = arena_push_array_safe(arena, FileTokenCount, 1000);
    size_t file_count = 0;
    
    /* Parse content to find file sections */
    const char *line_start = content;
    const char *file_start = NULL;
    char *current_file = NULL;
    
    while (*line_start) {
        /* Find end of current line */
        const char *line_end = strchr(line_start, '\n');
        if (!line_end) line_end = line_start + strlen(line_start);
        
        /* Check if this is a file header */
        if (strncmp(line_start, "File: ", 6) == 0) {
            /* If we were processing a previous file, count its tokens */
            if (file_start && current_file && file_count < 1000) {
                size_t file_len = line_start - file_start;
                char *file_content = arena_push_array_safe(arena, char, file_len + 1);
                if (file_content) {
                    strncpy(file_content, file_start, file_len);
                    file_content[file_len] = '\0';
                    
                    size_t tokens = llm_count_tokens(file_content, model);
                    if (tokens != SIZE_MAX) {
                        files[file_count].filename = current_file;
                        files[file_count].tokens = tokens;
                        file_count++;
                    }
                }
            }
            
            /* Extract new filename */
            size_t line_len = line_end - line_start;
            char *line_copy = arena_push_array_safe(arena, char, line_len + 1);
            if (line_copy) {
                strncpy(line_copy, line_start, line_len);
                line_copy[line_len] = '\0';
                const char *filename = extract_filename(line_copy);
                if (filename) {
                    size_t name_len = strlen(filename);
                    current_file = arena_push_array_safe(arena, char, name_len + 1);
                    if (current_file) {
                        strcpy(current_file, filename);
                    }
                }
                file_start = line_end + 1; /* Start of file content */
            }
        }
        
        /* Move to next line */
        line_start = (*line_end) ? line_end + 1 : line_end;
    }
    
    /* Handle last file if any */
    if (file_start && current_file && file_count < 1000) {
        size_t tokens = llm_count_tokens(file_start, model);
        if (tokens != SIZE_MAX) {
            files[file_count].filename = current_file;
            files[file_count].tokens = tokens;
            file_count++;
        }
    }
    
    /* Count tokens in non-file content (instructions, etc.) */
    size_t other_tokens = total_tokens;
    for (size_t i = 0; i < file_count; i++) {
        other_tokens -= files[i].tokens;
    }
    
    /* Print diagnostics table */
    fprintf(out, "  Tokens   File\n");
    fprintf(out, "  -------  ------------------------\n");
    
    /* Print file token counts */
    for (size_t i = 0; i < file_count; i++) {
        fprintf(out, "  %7zu  %s\n", files[i].tokens, files[i].filename);
    }
    
    /* Print other content if any */
    if (other_tokens > 0) {
        fprintf(out, "  %7zu  <user_instructions>\n", other_tokens);
    }
    
    /* Print total */
    fprintf(out, "  -------  ------------------------\n");
    fprintf(out, "  %7zu  Total\n", total_tokens);
}