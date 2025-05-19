#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

/**
 * JavaScript language pack for LLM_CTX using tree-sitter statically linked
 */

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

/**
 * Add an entry to the codemap file
 */
static CodemapEntry* add_entry(CodemapFile *file, const char *name, const char *signature, 
                              const char *return_type, const char *container, CMKind kind, 
                              Arena *arena) {
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
    
    // Set entry fields safely
    strncpy(new_entry->name, name ? name : "<anonymous>", sizeof(new_entry->name) - 1);
    strncpy(new_entry->signature, signature ? signature : "()", sizeof(new_entry->signature) - 1);
    strncpy(new_entry->return_type, return_type ? return_type : "void", sizeof(new_entry->return_type) - 1);
    strncpy(new_entry->container, container ? container : "", sizeof(new_entry->container) - 1);
    new_entry->kind = kind;
    
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
 * Get node's name if it's an identifier
 */
static char* get_identifier_name(TSNode node, const char *source, Arena *arena) {
    const char *node_type = ts_node_type(node);
    
    if (strcmp(node_type, "identifier") == 0) {
        // Direct identifier node - extract name
        uint32_t start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        return copy_substring(source, start, end, arena);
    }
    
    // Check for identifier in child nodes
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);
        
        if (strcmp(child_type, "identifier") == 0) {
            uint32_t start = ts_node_start_byte(child);
            uint32_t end = ts_node_end_byte(child);
            return copy_substring(source, start, end, arena);
        }
    }
    
    return "<anonymous>";
}

/**
 * Extract parameters from a node
 */
static char* get_parameters(TSNode node, const char *source, Arena *arena) {
    // Look for formal_parameters node
    TSNode params_node = {0};
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        const char *child_type = ts_node_type(child);
        
        if (strcmp(child_type, "formal_parameters") == 0) {
            params_node = child;
            break;
        }
    }
    
    if (ts_node_is_null(params_node)) {
        return "()";
    }
    
    uint32_t start = ts_node_start_byte(params_node);
    uint32_t end = ts_node_end_byte(params_node);
    char *params = copy_substring(source, start, end, arena);
    
    return params ? params : "()";
}

/**
 * Process tree-sitter nodes and extract code entities
 */
static void process_node(TSNode node, const char *source, CodemapFile *file, Arena *arena, const char *current_class) {
    if (ts_node_is_null(node)) return;
    
    const char *node_type = ts_node_type(node);
    
    // Extract function declarations
    if (strcmp(node_type, "function_declaration") == 0) {
        char *name = get_identifier_name(node, source, arena);
        char *params = get_parameters(node, source, arena);
        add_entry(file, name, params, "void", "", CM_FUNCTION, arena);
    }
    
    // Extract class declarations
    else if (strcmp(node_type, "class_declaration") == 0) {
        char *class_name = get_identifier_name(node, source, arena);
        add_entry(file, class_name, "", "", "", CM_CLASS, arena);
        
        // Find class body to extract methods
        TSNode class_body = {0};
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            TSNode child = ts_node_child(node, i);
            if (strcmp(ts_node_type(child), "class_body") == 0) {
                class_body = child;
                break;
            }
        }
        
        if (!ts_node_is_null(class_body)) {
            // Process methods in the class body
            for (uint32_t i = 0; i < ts_node_child_count(class_body); i++) {
                TSNode child = ts_node_child(class_body, i);
                const char *child_type = ts_node_type(child);
                
                if (strcmp(child_type, "method_definition") == 0) {
                    // For method_definition, need to look more carefully for the method name
                    char *method_name = NULL;
                    
                    // Check if this is a constructor
                    const char *first_child_type = ts_node_type(ts_node_child(child, 0));
                    if (strcmp(first_child_type, "constructor") == 0) {
                        method_name = "constructor";
                    } else {
                        // Look for property_identifier child
                        for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
                            TSNode prop = ts_node_child(child, j);
                            const char *prop_type = ts_node_type(prop);
                            
                            if (strcmp(prop_type, "property_identifier") == 0) {
                                uint32_t start = ts_node_start_byte(prop);
                                uint32_t end = ts_node_end_byte(prop);
                                method_name = copy_substring(source, start, end, arena);
                                break;
                            }
                        }
                    }
                    
                    if (!method_name) {
                        method_name = "<anonymous>";
                    }
                    
                    char *params = get_parameters(child, source, arena);
                    add_entry(file, method_name, params, "void", class_name, CM_METHOD, arena);
                }
            }
        }
    }
    
    // Extract TypeScript interfaces
    else if (strcmp(node_type, "interface_declaration") == 0) {
        char *name = get_identifier_name(node, source, arena);
        add_entry(file, name, "", "", "", CM_TYPE, arena);
    }
    
    // Recursively process child nodes
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        
        // Skip class_body as we handle it separately in class_declaration
        if (strcmp(ts_node_type(child), "class_body") != 0) {
            process_node(child, source, file, arena, current_class);
        }
    }
}

/**
 * Initialize the JavaScript pack
 */
bool initialize(void) {
    printf("Initializing JavaScript language pack with tree-sitter...\n");
    
    // We could do more initialization here if needed
    return true;
}

/**
 * Clean up resources
 */
void cleanup(void) {
    printf("Cleaning up JavaScript language pack resources...\n");
    
    // No resources to clean up in this implementation
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
 * Parse a JavaScript/TypeScript file using tree-sitter
 */
bool parse_file(const char *path, const char *source, size_t source_len, CodemapFile *file, Arena *arena) {
    if (!path || !source || !file || !arena) {
        fprintf(stderr, "ERROR: Invalid arguments to parse_file.\n");
        return false;
    }
    
    printf("Parsing JavaScript/TypeScript file with tree-sitter: %s\n", path);
    
    // Create a tree-sitter parser
    TSParser *parser = ts_parser_new();
    if (!parser) {
        fprintf(stderr, "ERROR: Failed to create tree-sitter parser.\n");
        return false;
    }
    
    // Set the language to JavaScript
    const TSLanguage *language = tree_sitter_javascript();
    if (!language) {
        fprintf(stderr, "ERROR: Failed to load JavaScript grammar.\n");
        ts_parser_delete(parser);
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
    
    // Get the syntax tree root node
    TSNode root_node = ts_tree_root_node(tree);
    
    // Process the syntax tree to extract code entities
    process_node(root_node, source, file, arena, NULL);
    
    // Clean up tree-sitter resources
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    
    // Check if we found any entries
    if (file->entry_count == 0) {
        fprintf(stderr, "WARNING: No code entities found in file: %s\n", path);
        // We don't consider this a failure - just return success with zero entries
    } else {
        printf("Successfully extracted %zu code entities from %s.\n", file->entry_count, path);
    }
    
    return true;
}