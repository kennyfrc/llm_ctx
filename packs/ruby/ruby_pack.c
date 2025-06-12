#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "../debug.h"

/**
 * JavaScript language pack for LLM_CTX using tree-sitter queries
 */

/* Debug mode flag - define locally for shared library, extern for tests */
#ifdef TEST_BUILD
extern bool debug_mode;
#else
bool debug_mode = false;
#endif

// Import exact struct definitions from the main codebase
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

// Import the actual Arena type definition from arena.h
typedef struct Arena {
    unsigned char *base;
    size_t pos;
    size_t size;
#ifdef ARENA_ENABLE_COMMIT
    size_t commit;
#endif
} Arena;

// Include tree-sitter API
#include "tree-sitter.h"

/* Static extensions array */
static const char *js_extensions[] = {".js", ".jsx", ".ts", ".tsx", NULL};
static size_t js_extension_count = 4;  // Excluding NULL terminator

/* Query source - loaded once and cached */
static char *query_source = NULL;
static TSQuery *compiled_query = NULL;
static const TSLanguage *current_language = NULL;

/**
 * Load query file content
 */
static char* load_query_file(const char *pack_dir) {
    char query_path[4096];
    FILE *f = NULL;
    
    // Try several paths to find the query file
    const char *paths[] = {
        "packs/javascript/codemap.scm",
        "./packs/javascript/codemap.scm",
        "../javascript/codemap.scm",
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
            snprintf(query_path, sizeof(query_path), "%s/packs/javascript/codemap.scm", cwd);
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
    // Determine the entity type from pattern index
    // Pattern indices based on order in codemap.scm:
    // 0: function declaration
    // 1-2: function expressions  
    // 3: class declaration
    // 4: method definition
    // 5-6: object methods
    // 7: export function
    // 8: default export
    
    CMKind kind = CM_FUNCTION;
    bool is_anonymous = false;
    
    if (match->pattern_index == 3) {
        kind = CM_CLASS;
    } else if (match->pattern_index == 4 || match->pattern_index == 5 || match->pattern_index == 6) {
        kind = CM_METHOD;
    }
    
    // Extract captures
    char *name = NULL;
    char *params = NULL;
    char *container = NULL;
    
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
        } else if (strncmp(capture_name, "class.container", capture_name_len) == 0) {
            container = copy_substring(source, start, end, arena);
        }
        
        // For methods, try to find the parent class
        if (kind == CM_METHOD && !container && capture->node.id) {
            // Look for a class.name capture in the same match
            for (uint16_t j = 0; j < match->capture_count; j++) {
                if (i != j) {
                    const TSQueryCapture *other_capture = &match->captures[j];
                    uint32_t other_name_len;
                    const char *other_capture_name = ts_query_capture_name_for_id(
                        query, other_capture->index, &other_name_len);
                    
                    if (strncmp(other_capture_name, "class.name", other_name_len) == 0) {
                        uint32_t class_start = ts_node_start_byte(other_capture->node);
                        uint32_t class_end = ts_node_end_byte(other_capture->node);
                        container = copy_substring(source, class_start, class_end, arena);
                        break;
                    }
                }
            }
        }
    }
    
    // Create codemap entry if we have a name
    if (name || is_anonymous) {
        CodemapEntry *entry = add_entry(file, arena);
        if (!entry) return;
        
        strncpy(entry->name, name ? name : "<anonymous>", sizeof(entry->name) - 1);
        strncpy(entry->signature, params ? params : "()", sizeof(entry->signature) - 1);
        strncpy(entry->return_type, "void", sizeof(entry->return_type) - 1);
        strncpy(entry->container, container ? container : "", sizeof(entry->container) - 1);
        entry->kind = kind;
    }
}

/**
 * Get the appropriate Tree-sitter language for a file
 */
static const TSLanguage* get_language_for_file(const char *path) {
    // For now, only support JavaScript
    // TODO: Add TypeScript support when grammars are linked
    (void)path; // Suppress unused parameter warning
    return tree_sitter_javascript();
}

/**
 * Initialize the JavaScript pack
 */
bool initialize(void) {
    debug_printf("[DEBUG] Initializing language pack: javascript");
    
    // Load the query file
    if (!query_source) {
        query_source = load_query_file("packs/javascript");
        if (!query_source) {
            fprintf(stderr, "ERROR: Failed to load codemap.scm\n");
            return false;
        }
    }
    
    return true;
}

/**
 * Clean up resources
 */
void cleanup(void) {
    debug_printf("[DEBUG] Cleaning up language pack: javascript");
    
    if (query_source) {
        free(query_source);
        query_source = NULL;
    }
    
    if (compiled_query) {
        ts_query_delete(compiled_query);
        compiled_query = NULL;
    }
    
    current_language = NULL;
}

/**
 * Get the supported file extensions
 */
const char **get_extensions(size_t *count) {
    if (count) {
        *count = js_extension_count;
    }
    return js_extensions;
}

/**
 * Parse a JavaScript/TypeScript file using tree-sitter queries
 */
bool parse_file(const char *path, const char *source, size_t source_len, CodemapFile *file, Arena *arena) {
    if (!path || !source || !file || !arena) {
        fprintf(stderr, "ERROR: Invalid arguments to parse_file.\n");
        return false;
    }
    
    debug_printf("[DEBUG] Parsing file with language pack: javascript, path: %s", path);
    
    // Get the appropriate language
    const TSLanguage *language = get_language_for_file(path);
    
    // Recompile query if language changed
    if (language != current_language || !compiled_query) {
        if (compiled_query) {
            ts_query_delete(compiled_query);
        }
        
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
        
        current_language = language;
    }
    
    // Create a tree-sitter parser
    TSParser *parser = ts_parser_new();
    if (!parser) {
        fprintf(stderr, "ERROR: Failed to create tree-sitter parser.\n");
        return false;
    }
    
    if (!ts_parser_set_language(parser, language)) {
        fprintf(stderr, "ERROR: Failed to set parser language.\n");
        ts_parser_delete(parser);
        return false;
    }
    
    // Parse the source code
    TSTree *tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)source_len);
    if (!tree) {
        fprintf(stderr, "ERROR: Failed to parse source code.\n");
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