#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include "../debug.h"

/**
 * Ruby language pack for LLM_CTX using tree-sitter
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
    CodemapEntry  *entries;   /* arena array */
    size_t         entry_count;
} CodemapFile;

// Import the actual Arena type definition
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
static const char *ruby_extensions[] = {".rb", ".rake", ".gemspec", NULL};
static size_t ruby_extension_count = 3;  // Excluding NULL terminator

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
 * Extract parameters from a method_parameters node
 */
static char* get_parameters(TSNode node, const char *source, Arena *arena) {
    // If node is null, return empty parameters
    if (ts_node_is_null(node)) {
        return "()";
    }
    
    // Get the text of the entire parameters node
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    char *params = copy_substring(source, start, end, arena);
    
    return params ? params : "()";
}

/**
 * Process tree-sitter nodes and extract Ruby code entities
 */
static void process_node(TSNode node, const char *source, CodemapFile *file, Arena *arena, const char *current_container) {
    if (ts_node_is_null(node)) return;
    
    const char *node_type = ts_node_type(node);
    
    // Extract Ruby method definitions (def)
    if (strcmp(node_type, "method") == 0) {
        // Find method name
        TSNode name_node = {0};
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            TSNode child = ts_node_child(node, i);
            const char *child_type = ts_node_type(child);
            
            if (strcmp(child_type, "identifier") == 0) {
                name_node = child;
                break;
            }
        }
        
        if (!ts_node_is_null(name_node)) {
            uint32_t start = ts_node_start_byte(name_node);
            uint32_t end = ts_node_end_byte(name_node);
            char *name = copy_substring(source, start, end, arena);
            
            // Find parameters node
            TSNode params_node = {0};
            for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
                TSNode child = ts_node_child(node, i);
                const char *child_type = ts_node_type(child);
                
                if (strcmp(child_type, "method_parameters") == 0) {
                    params_node = child;
                    break;
                }
            }
            
            char *params = "()";
            if (!ts_node_is_null(params_node)) {
                params = get_parameters(params_node, source, arena);
            }
            
            // Check if this is a class/module method or an instance method
            bool is_singleton_method = false;
            for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
                TSNode child = ts_node_child(node, i);
                if (strcmp(ts_node_type(child), "self") == 0) {
                    is_singleton_method = true;
                    break;
                }
            }
            
            // If we're inside a class and it's not a singleton method, it's an instance method
            if (current_container && !is_singleton_method) {
                add_entry(file, name, params, "void", current_container, CM_METHOD, arena);
            } else {
                // It's either a singleton method or a standalone function
                add_entry(file, name, params, "void", "", CM_FUNCTION, arena);
            }
        }
    }
    
    // Extract Ruby classes
    else if (strcmp(node_type, "class") == 0) {
        // Find class name
        TSNode name_node = {0};
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            TSNode child = ts_node_child(node, i);
            const char *child_type = ts_node_type(child);
            
            if (strcmp(child_type, "constant") == 0) {
                name_node = child;
                break;
            }
        }
        
        if (!ts_node_is_null(name_node)) {
            uint32_t start = ts_node_start_byte(name_node);
            uint32_t end = ts_node_end_byte(name_node);
            char *class_name = copy_substring(source, start, end, arena);
            
            // Add class entry
            add_entry(file, class_name, "", "", "", CM_CLASS, arena);
            
            // Process class body with this class name as container
            for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
                TSNode child = ts_node_child(node, i);
                process_node(child, source, file, arena, class_name);
            }
        }
        
        // We've already processed all children with the class name as container
        return;
    }
    
    // Extract Ruby modules
    else if (strcmp(node_type, "module") == 0) {
        // Find module name
        TSNode name_node = {0};
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            TSNode child = ts_node_child(node, i);
            const char *child_type = ts_node_type(child);
            
            if (strcmp(child_type, "constant") == 0) {
                name_node = child;
                break;
            }
        }
        
        if (!ts_node_is_null(name_node)) {
            uint32_t start = ts_node_start_byte(name_node);
            uint32_t end = ts_node_end_byte(name_node);
            char *module_name = copy_substring(source, start, end, arena);
            
            // Add module entry as a type
            add_entry(file, module_name, "", "", "", CM_TYPE, arena);
            
            // Process module body with this module name as container
            for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
                TSNode child = ts_node_child(node, i);
                process_node(child, source, file, arena, module_name);
            }
        }
        
        // We've already processed all children with the module name as container
        return;
    }
    
    // Process all children recursively
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        process_node(child, source, file, arena, current_container);
    }
}

/**
 * Initialize the Ruby pack
 */
bool initialize(void) {
    debug_printf("[DEBUG] Initializing language pack: ruby");
    
    // We could do more initialization here if needed
    return true;
}

/**
 * Clean up resources
 */
void cleanup(void) {
    debug_printf("[DEBUG] Cleaning up language pack: ruby");
    
    // No resources to clean up in this implementation
}

/**
 * Get the supported file extensions
 */
const char **get_extensions(size_t *count) {
    if (count) {
        *count = ruby_extension_count;
    }
    return ruby_extensions;
}

/**
 * Parse a Ruby file using tree-sitter
 */
bool parse_file(const char *path, const char *source, size_t source_len, CodemapFile *file, Arena *arena) {
    if (!path || !source || !file || !arena) {
        fprintf(stderr, "ERROR: Invalid arguments to parse_file.\n");
        return false;
    }
    
    debug_printf("[DEBUG] Parsing file with language pack: ruby, path: %s", path);
    
    // Create a tree-sitter parser
    TSParser *parser = ts_parser_new();
    if (!parser) {
        fprintf(stderr, "ERROR: Failed to create tree-sitter parser.\n");
        return false;
    }
    
    // Set the language to Ruby
    const TSLanguage *language = tree_sitter_ruby();
    if (!language) {
        fprintf(stderr, "ERROR: Failed to load Ruby grammar.\n");
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
        debug_printf("Successfully extracted %zu code entities from %s.", file->entry_count, path);
    }
    
    return true;
}