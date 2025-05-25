#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "../debug.h"

/**
 * Template Language Pack for LLM_CTX using tree-sitter queries
 *
 * This is a starting point for creating your own language pack.
 * Replace "your-language" with your target language name.
 */

/* Debug mode flag - define locally for shared library, extern for tests */
#ifdef TEST_BUILD
extern bool debug_mode;
#else
bool debug_mode = false;
#endif

/* Import required structures from LLM_CTX */
typedef enum { 
    CM_FUNCTION = 0, 
    CM_CLASS = 1, 
    CM_METHOD = 2, 
    CM_TYPE = 3
} CMKind;

typedef struct {
    char  name[128];          /* Identifier */
    char  signature[256];     /* Params incl. "(...)" */
    char  return_type[64];    /* "void" default for unknown */
    char  container[128];     /* Class name for methods, empty otherwise */
    CMKind kind;
} CodemapEntry;

typedef struct {
    char           path[4096];
    CodemapEntry  *entries;    /* arena array */
    size_t         entry_count;
} CodemapFile;

typedef struct Arena {
    unsigned char *base;
    size_t pos;
    size_t size;
#ifdef ARENA_ENABLE_COMMIT
    size_t commit;
#endif
} Arena;

/* Import Tree-sitter API definitions */
#include "tree-sitter.h"

/* Static file extensions array - update with your language's extensions */
static const char *lang_extensions[] = {".yourlang", NULL};
static size_t lang_extension_count = 1;  /* Update this count */

/* Query source - loaded once and cached */
static char *query_source = NULL;
static TSQuery *compiled_query = NULL;

/**
 * Load query file content
 */
static char* load_query_file(const char *pack_dir) {
    char query_path[4096];
    FILE *f = NULL;
    
    // Try several paths to find the query file
    const char *paths[] = {
        "packs/your-language/codemap.scm",  // Update this path
        "./packs/your-language/codemap.scm",
        "../your-language/codemap.scm",
        "codemap.scm",
        NULL
    };
    
    // First try the provided pack_dir
    if (pack_dir) {
        snprintf(query_path, sizeof(query_path), "%s/codemap.scm", pack_dir);
        f = fopen(query_path, "r");
    }
    
    // Try other paths
    for (int i = 0; paths[i] && !f; i++) {
        f = fopen(paths[i], "r");
    }
    
    if (!f) {
        // Try to find where we are and construct path
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd))) {
            snprintf(query_path, sizeof(query_path), "%s/packs/your-language/codemap.scm", cwd);
            f = fopen(query_path, "r");
        }
    }
    
    if (!f) {
        fprintf(stderr, "ERROR: Could not open codemap.scm (tried multiple paths)\n");
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *content = malloc(file_size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }
    
    size_t read_size = fread(content, 1, file_size, f);
    content[read_size] = '\0';
    fclose(f);
    
    return content;
}

/**
 * Add an entry to the codemap file
 */
static CodemapEntry* add_entry(CodemapFile *file, Arena *arena) {
    if (!file || !arena) return NULL;
    
    // Allocate a new array in the arena
    size_t new_entries_size = sizeof(CodemapEntry) * (file->entry_count + 1);
    
    // Check if we have enough space
    if (arena->pos + new_entries_size > arena->size) {
        fprintf(stderr, "ERROR: Arena out of memory for new entries\n");
        return NULL;
    }
    
    // Allocate the new array
    CodemapEntry *new_entries = (CodemapEntry*)(arena->base + arena->pos);
    arena->pos += new_entries_size;
    
    // Copy existing entries
    if (file->entry_count > 0 && file->entries) {
        memcpy(new_entries, file->entries, file->entry_count * sizeof(CodemapEntry));
    }
    
    // Initialize the new entry
    CodemapEntry *new_entry = &new_entries[file->entry_count];
    memset(new_entry, 0, sizeof(CodemapEntry));
    
    // Update the file structure
    file->entries = new_entries;
    file->entry_count++;
    
    return new_entry;
}

/**
 * Copy a substring from source code
 */
static char* copy_substring(const char *source, uint32_t start, uint32_t end, Arena *arena) {
    if (!source || start >= end) return NULL;
    
    uint32_t length = end - start;
    if (length >= 256) length = 255; // Enforce size limit
    
    if (arena->pos + length + 1 > arena->size) {
        fprintf(stderr, "ERROR: Arena out of memory for substring\n");
        return NULL;
    }
    
    char *str = (char*)(arena->base + arena->pos);
    memcpy(str, source + start, length);
    str[length] = '\0'; // Null terminate
    
    arena->pos += length + 1;
    return str;
}

/**
 * Process a query match and extract code entity
 */
static void process_match(const TSQueryMatch *match, const char *source, 
                         CodemapFile *file, Arena *arena, TSQuery *query) {
    // Extract captures
    char *name = NULL;
    char *params = NULL;
    char *container = NULL;
    CMKind kind = CM_FUNCTION;
    
    for (uint16_t i = 0; i < match->capture_count; i++) {
        const TSQueryCapture *capture = &match->captures[i];
        uint32_t capture_name_len;
        const char *capture_name = ts_query_capture_name_for_id(
            query, capture->index, &capture_name_len);
        
        uint32_t start = ts_node_start_byte(capture->node);
        uint32_t end = ts_node_end_byte(capture->node);
        
        // Extract based on capture name
        if (strncmp(capture_name, "function.name", capture_name_len) == 0 ||
            strncmp(capture_name, "class.name", capture_name_len) == 0 ||
            strncmp(capture_name, "method.name", capture_name_len) == 0 ||
            strncmp(capture_name, "type.name", capture_name_len) == 0) {
            name = copy_substring(source, start, end, arena);
        } else if (strncmp(capture_name, "function.params", capture_name_len) == 0 ||
                   strncmp(capture_name, "method.params", capture_name_len) == 0) {
            params = copy_substring(source, start, end, arena);
        } else if (strncmp(capture_name, "class.container", capture_name_len) == 0 ||
                   strncmp(capture_name, "method.container", capture_name_len) == 0) {
            container = copy_substring(source, start, end, arena);
        }
    }
    
    // Determine kind based on the match pattern
    for (uint16_t i = 0; i < match->capture_count; i++) {
        const TSQueryCapture *capture = &match->captures[i];
        uint32_t capture_name_len;
        const char *capture_name = ts_query_capture_name_for_id(
            query, capture->index, &capture_name_len);
        
        if (strncmp(capture_name, "class", capture_name_len) == 0) {
            kind = CM_CLASS;
            break;
        } else if (strncmp(capture_name, "type", capture_name_len) == 0) {
            kind = CM_TYPE;
            break;
        } else if (strncmp(capture_name, "method", capture_name_len) == 0) {
            kind = CM_METHOD;
            break;
        }
    }
    
    // Create codemap entry if we have a name
    if (name) {
        CodemapEntry *entry = add_entry(file, arena);
        if (!entry) return;
        
        strncpy(entry->name, name, sizeof(entry->name) - 1);
        strncpy(entry->signature, params ? params : "()", sizeof(entry->signature) - 1);
        strncpy(entry->return_type, "void", sizeof(entry->return_type) - 1);
        strncpy(entry->container, container ? container : "", sizeof(entry->container) - 1);
        entry->kind = kind;
    }
}

/* ============== Required Interface Functions ============== */

/**
 * Initialize the language pack
 */
bool initialize(void) {
    debug_printf("[DEBUG] Initializing language pack: your-language");
    
    // Load the query file
    if (!query_source) {
        query_source = load_query_file("packs/your-language");
        if (!query_source) {
            fprintf(stderr, "ERROR: Failed to load codemap.scm\n");
            return false;
        }
    }
    
    return true;
}

/**
 * Clean up resources when the language pack is unloaded
 */
void cleanup(void) {
    debug_printf("[DEBUG] Cleaning up language pack: your-language");
    
    if (query_source) {
        free(query_source);
        query_source = NULL;
    }
    
    if (compiled_query) {
        ts_query_delete(compiled_query);
        compiled_query = NULL;
    }
}

/**
 * Return the list of file extensions supported by this language pack
 */
const char **get_extensions(size_t *count) {
    if (count) {
        *count = lang_extension_count;
    }
    return lang_extensions;
}

/**
 * Parse a source file and extract code structure information
 */
bool parse_file(const char *path, const char *source, size_t source_len, 
               CodemapFile *file, Arena *arena) {
    if (!path || !source || !file || !arena) {
        fprintf(stderr, "ERROR: Invalid arguments to parse_file\n");
        return false;
    }
    
    debug_printf("[DEBUG] Parsing file with language pack: your-language, path: %s", path);
    
    // Get your language's tree-sitter function
    const TSLanguage *language = tree_sitter_your_language();
    
    // Compile query if not already compiled
    if (!compiled_query) {
        uint32_t error_offset;
        TSQueryError error_type;
        compiled_query = ts_query_new(language, query_source, strlen(query_source),
                                     &error_offset, &error_type);
        
        if (!compiled_query) {
            fprintf(stderr, "ERROR: Failed to compile query at offset %u: ", error_offset);
            switch (error_type) {
                case TSQueryErrorSyntax:
                    fprintf(stderr, "Syntax error\n");
                    break;
                case TSQueryErrorNodeType:
                    fprintf(stderr, "Invalid node type\n");
                    break;
                case TSQueryErrorField:
                    fprintf(stderr, "Invalid field\n");
                    break;
                case TSQueryErrorCapture:
                    fprintf(stderr, "Invalid capture\n");
                    break;
                default:
                    fprintf(stderr, "Unknown error\n");
            }
            return false;
        }
    }
    
    // Create a tree-sitter parser
    TSParser *parser = ts_parser_new();
    if (!parser) {
        fprintf(stderr, "ERROR: Failed to create tree-sitter parser\n");
        return false;
    }
    
    if (!ts_parser_set_language(parser, language)) {
        fprintf(stderr, "ERROR: Failed to set parser language\n");
        ts_parser_delete(parser);
        return false;
    }
    
    // Parse the source code
    TSTree *tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)source_len);
    if (!tree) {
        fprintf(stderr, "ERROR: Failed to parse source code\n");
        ts_parser_delete(parser);
        return false;
    }
    
    // Execute query
    TSQueryCursor *cursor = ts_query_cursor_new();
    TSNode root_node = ts_tree_root_node(tree);
    ts_query_cursor_exec(cursor, compiled_query, root_node);
    
    // Process matches
    TSQueryMatch match;
    uint32_t match_count = 0;
    while (ts_query_cursor_next_match(cursor, &match)) {
        process_match(&match, source, file, arena, compiled_query);
        match_count++;
    }
    
    // Clean up tree-sitter resources
    ts_query_cursor_delete(cursor);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    
    if (file->entry_count == 0) {
        debug_printf("WARNING: No code entities found in file: %s (processed %u matches)", path, match_count);
    } else {
        debug_printf("Successfully extracted %zu code entities from %s (from %u matches).", 
                    file->entry_count, path, match_count);
    }
    
    return true;
}