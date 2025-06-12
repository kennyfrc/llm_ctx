#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <ctype.h>
#include "tokenizer.h"
#include "arena.h"

/* Structure to hold per-file token counts */
typedef struct {
    const char *filename;
    size_t tokens;
} FileTokenCount;

/* Structure to hold section token counts */
typedef struct {
    const char *name;
    size_t tokens;
} SectionTokenCount;

/* Parser states */
typedef enum {
    STATE_TOP_LEVEL,
    STATE_IN_FILE_TREE,
    STATE_IN_FILE_CONTEXT,
    STATE_IN_FILE,
    STATE_IN_CODE_BLOCK,
    STATE_IN_USER_INSTRUCTIONS,
    STATE_IN_SYSTEM_INSTRUCTIONS,
    STATE_IN_RESPONSE_GUIDE,
    STATE_IN_OTHER_SECTION
} ParserState;

/* Stack entry for tracking parser state */
typedef struct {
    ParserState state;
    const char *section_name;
    const char *start_pos;
} StateStackEntry;

/* Parser context */
typedef struct {
    StateStackEntry stack[32];
    int stack_depth;
    
    /* Current tracking */
    const char *current_file_name;
    const char *current_file_start;
    bool in_code_fence;
    int code_fence_backticks;
    
    /* Results */
    FileTokenCount *files;
    SectionTokenCount *sections;
    size_t file_count;
    size_t section_count;
    size_t max_files;
    size_t max_sections;
} ParserContext;

/* Initialize parser context */
static void parser_init(ParserContext *ctx, FileTokenCount *files, size_t max_files,
                       SectionTokenCount *sections, size_t max_sections) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->files = files;
    ctx->sections = sections;
    ctx->max_files = max_files;
    ctx->max_sections = max_sections;
    
    /* Start at top level */
    ctx->stack[0].state = STATE_TOP_LEVEL;
    ctx->stack[0].section_name = NULL;
    ctx->stack[0].start_pos = NULL;
    ctx->stack_depth = 1;
}

/* Push a new state onto the stack */
static void parser_push_state(ParserContext *ctx, ParserState state, 
                             const char *section_name, const char *start_pos) {
    if (ctx->stack_depth < 32) {
        ctx->stack[ctx->stack_depth].state = state;
        ctx->stack[ctx->stack_depth].section_name = section_name;
        ctx->stack[ctx->stack_depth].start_pos = start_pos;
        ctx->stack_depth++;
    }
}

/* Pop state from stack */
static void parser_pop_state(ParserContext *ctx) {
    if (ctx->stack_depth > 1) {
        ctx->stack_depth--;
    }
}

/* Get current state */
static ParserState parser_current_state(ParserContext *ctx) {
    return ctx->stack[ctx->stack_depth - 1].state;
}

/* Save a section with its token count */
static void save_section(ParserContext *ctx, const char *name, const char *start, 
                        const char *end, const char *model, Arena *arena) {
    if (ctx->section_count >= ctx->max_sections) return;
    
    size_t len = end - start;
    char *content = arena_push_array_safe(arena, char, len + 1);
    if (!content) return;
    
    memcpy(content, start, len);
    content[len] = '\0';
    
    size_t tokens = llm_count_tokens(content, model);
    if (tokens != SIZE_MAX) {
        char *name_copy = arena_strdup_safe(arena, name);
        if (name_copy) {
            ctx->sections[ctx->section_count].name = name_copy;
            ctx->sections[ctx->section_count].tokens = tokens;
            ctx->section_count++;
        }
    }
}

/* Save a file with its token count */
static void save_file(ParserContext *ctx, const char *filename, const char *start,
                     const char *end, const char *model, Arena *arena) {
    if (ctx->file_count >= ctx->max_files) return;
    
    size_t len = end - start;
    char *content = arena_push_array_safe(arena, char, len + 1);
    if (!content) return;
    
    memcpy(content, start, len);
    content[len] = '\0';
    
    size_t tokens = llm_count_tokens(content, model);
    if (tokens != SIZE_MAX) {
        char *name_copy = arena_strdup_safe(arena, filename);
        if (name_copy) {
            ctx->files[ctx->file_count].filename = name_copy;
            ctx->files[ctx->file_count].tokens = tokens;
            ctx->file_count++;
        }
    }
}

/* Check if a line starts with a tag */
static bool line_starts_with_tag(const char *line, const char *tag) {
    /* Skip leading whitespace */
    while (*line && isspace(*line)) line++;
    return strncmp(line, tag, strlen(tag)) == 0;
}

/* Extract filename from "File: filename" line */
static char *extract_filename(const char *line, Arena *arena) {
    /* Skip leading whitespace */
    while (*line && isspace(*line)) line++;
    
    if (strncmp(line, "File:", 5) != 0) return NULL;
    
    line += 5;
    while (*line && isspace(*line)) line++;
    
    /* Find end of line */
    const char *end = line;
    while (*end && *end != '\n' && *end != '\r') end++;
    
    size_t len = end - line;
    if (len == 0) return NULL;
    
    char *filename = arena_push_array_safe(arena, char, len + 1);
    if (!filename) return NULL;
    
    memcpy(filename, line, len);
    filename[len] = '\0';
    
    return filename;
}

/* Count backticks at start of line */
static int count_backticks(const char *line) {
    int count = 0;
    while (*line && isspace(*line)) line++;
    while (*line == '`') {
        count++;
        line++;
    }
    return count;
}

/* Generate token diagnostics from content, counting tokens per file and section */
void generate_token_diagnostics(const char *content, const char *model, FILE *out, Arena *arena) {
    if (!content || !model || !out) return;
    
    /* First count total tokens */
    size_t total_tokens = llm_count_tokens(content, model);
    if (total_tokens == SIZE_MAX) {
        fprintf(out, "Error: Token counting unavailable\n");
        return;
    }
    
    /* Allocate storage for results */
    FileTokenCount *files = arena_push_array_safe(arena, FileTokenCount, 1000);
    SectionTokenCount *sections = arena_push_array_safe(arena, SectionTokenCount, 20);
    if (!files || !sections) return;
    
    /* Initialize parser context */
    ParserContext ctx;
    parser_init(&ctx, files, 1000, sections, 20);
    
    /* Parse content line by line */
    const char *pos = content;
    const char *line_start = content;
    
    while (*pos) {
        /* Find end of line */
        const char *line_end = pos;
        while (*line_end && *line_end != '\n') line_end++;
        
        /* Create null-terminated line for processing */
        size_t line_len = line_end - line_start;
        char *line = arena_push_array_safe(arena, char, line_len + 1);
        if (!line) break;
        
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';
        
        /* Get current state */
        ParserState state = parser_current_state(&ctx);
        
        /* Process line based on current state */
        switch (state) {
            case STATE_TOP_LEVEL:
                /* Look for section starts */
                if (line_starts_with_tag(line, "<file_tree>")) {
                    parser_push_state(&ctx, STATE_IN_FILE_TREE, "file_tree", line_end + 1);
                }
                else if (line_starts_with_tag(line, "<file_context>")) {
                    parser_push_state(&ctx, STATE_IN_FILE_CONTEXT, "file_context", line_end + 1);
                }
                else if (line_starts_with_tag(line, "<user_instructions>")) {
                    parser_push_state(&ctx, STATE_IN_USER_INSTRUCTIONS, "user_instructions", line_end + 1);
                }
                else if (line_starts_with_tag(line, "<system_instructions>")) {
                    parser_push_state(&ctx, STATE_IN_SYSTEM_INSTRUCTIONS, "system_instructions", line_end + 1);
                }
                else if (line_starts_with_tag(line, "<response_guide>")) {
                    parser_push_state(&ctx, STATE_IN_RESPONSE_GUIDE, "response_guide", line_end + 1);
                }
                break;
                
            case STATE_IN_FILE_TREE:
                /* Look for end tag */
                if (line_starts_with_tag(line, "</file_tree>")) {
                    /* Save section */
                    StateStackEntry *entry = &ctx.stack[ctx.stack_depth - 1];
                    save_section(&ctx, entry->section_name, entry->start_pos, line_start, model, arena);
                    parser_pop_state(&ctx);
                }
                /* Everything else is part of file tree content */
                break;
                
            case STATE_IN_FILE_CONTEXT:
                /* Look for file headers or end tag */
                if (line_starts_with_tag(line, "</file_context>")) {
                    /* Save any pending file */
                    if (ctx.current_file_name && ctx.current_file_start) {
                        save_file(&ctx, ctx.current_file_name, ctx.current_file_start, 
                                 line_start, model, arena);
                        ctx.current_file_name = NULL;
                        ctx.current_file_start = NULL;
                    }
                    parser_pop_state(&ctx);
                }
                else if (line_starts_with_tag(line, "File:")) {
                    /* Save previous file if any */
                    if (ctx.current_file_name && ctx.current_file_start) {
                        save_file(&ctx, ctx.current_file_name, ctx.current_file_start, 
                                 line_start, model, arena);
                    }
                    
                    /* Start new file */
                    ctx.current_file_name = extract_filename(line, arena);
                    ctx.current_file_start = line_end + 1;
                    
                    /* Check if next line starts a code block */
                    const char *next_line = line_end + 1;
                    if (*next_line) {
                        int backticks = count_backticks(next_line);
                        if (backticks >= 3) {
                            ctx.in_code_fence = true;
                            ctx.code_fence_backticks = backticks;
                        }
                    }
                }
                else if (ctx.in_code_fence) {
                    /* Check for code fence end */
                    int backticks = count_backticks(line);
                    if (backticks >= ctx.code_fence_backticks) {
                        ctx.in_code_fence = false;
                        ctx.code_fence_backticks = 0;
                    }
                }
                break;
                
            case STATE_IN_USER_INSTRUCTIONS:
                /* Look for end tag */
                if (line_starts_with_tag(line, "</user_instructions>")) {
                    StateStackEntry *entry = &ctx.stack[ctx.stack_depth - 1];
                    save_section(&ctx, entry->section_name, entry->start_pos, line_start, model, arena);
                    parser_pop_state(&ctx);
                }
                break;
                
            case STATE_IN_SYSTEM_INSTRUCTIONS:
                /* Look for end tag */
                if (line_starts_with_tag(line, "</system_instructions>")) {
                    StateStackEntry *entry = &ctx.stack[ctx.stack_depth - 1];
                    save_section(&ctx, entry->section_name, entry->start_pos, line_start, model, arena);
                    parser_pop_state(&ctx);
                }
                break;
                
            case STATE_IN_RESPONSE_GUIDE:
                /* Look for end tag */
                if (line_starts_with_tag(line, "</response_guide>")) {
                    StateStackEntry *entry = &ctx.stack[ctx.stack_depth - 1];
                    save_section(&ctx, entry->section_name, entry->start_pos, line_start, model, arena);
                    parser_pop_state(&ctx);
                }
                break;
                
            default:
                break;
        }
        
        /* Move to next line */
        pos = (*line_end) ? line_end + 1 : line_end;
        line_start = pos;
    }
    
    /* Handle any unclosed sections */
    while (ctx.stack_depth > 1) {
        ParserState state = parser_current_state(&ctx);
        StateStackEntry *entry = &ctx.stack[ctx.stack_depth - 1];
        
        if (state == STATE_IN_FILE_CONTEXT && ctx.current_file_name && ctx.current_file_start) {
            /* Save last file */
            save_file(&ctx, ctx.current_file_name, ctx.current_file_start, pos, model, arena);
        } else if (entry->start_pos) {
            /* Save section */
            save_section(&ctx, entry->section_name, entry->start_pos, pos, model, arena);
        }
        
        parser_pop_state(&ctx);
    }
    
    /* Print diagnostics table */
    fprintf(out, "  Tokens   Category\n");
    fprintf(out, "  -------  ------------------------\n");
    
    /* Print section token counts (excluding file_context since files are shown separately) */
    size_t accounted_tokens = 0;
    for (size_t i = 0; i < ctx.section_count; i++) {
        fprintf(out, "  %7zu  <%s>\n", ctx.sections[i].tokens, ctx.sections[i].name);
        accounted_tokens += ctx.sections[i].tokens;
    }
    
    /* Print file token counts */
    for (size_t i = 0; i < ctx.file_count; i++) {
        fprintf(out, "  %7zu  %s\n", ctx.files[i].tokens, ctx.files[i].filename);
        accounted_tokens += ctx.files[i].tokens;
    }
    
    /* Calculate and show any unaccounted tokens */
    size_t unaccounted = total_tokens - accounted_tokens;
    if (unaccounted > 0) {
        fprintf(out, "  %7zu  <other>\n", unaccounted);
    }
    
    /* Print total */
    fprintf(out, "  -------  ------------------------\n");
    fprintf(out, "  %7zu  Total\n", total_tokens);
}