#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

/**
 * Template Language Pack for LLM_CTX
 *
 * This is a starting point for creating your own language pack.
 * Replace "your-language" with your target language name.
 */

/* Import Tree-sitter API definitions */
#include "tree-sitter.h"

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

/* Static file extensions array - update with your language's extensions */
static const char *lang_extensions[] = {".yourlang", NULL};
static size_t lang_extension_count = 1;  /* Update this count */

/**
 * Utility function to copy a substring from source code
 */
static char* copy_substring(const char *source, uint32_t start, uint32_t end, Arena *arena) {
    if (!source || start >= end) return NULL;
    
    uint32_t length = end - start;
    if (length >= 256) length = 255; /* Limit string length */
    
    /* Allocate from arena */
    if (arena->pos + length + 1 > arena->size) {
        fprintf(stderr, "ERROR: Arena out of memory for substring\n");
        return NULL;
    }
    
    char *str = (char*)(arena->base + arena->pos);
    memcpy(str, source + start, length);
    str[length] = '\0'; /* Null terminate */
    
    arena->pos += length + 1;
    return str;
}

/**
 * Process a Tree-sitter node and extract code entities
 */
static void process_node(TSNode node, const char *source, CodemapFile *file, Arena *arena, const char *container) {
    if (ts_node_is_null(node)) return;
    
    const char *node_type = ts_node_type(node);
    
    /* Examples for handling common node types - adjust for your language */
    
    /* Example: Function detection */
    if (strcmp(node_type, "function_definition") == 0 || 
        strcmp(node_type, "function_declaration") == 0) {
        
        /* Find the function name - this is highly language specific */
        TSNode name_node = {0};
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            TSNode child = ts_node_child(node, i);
            if (strcmp(ts_node_type(child), "identifier") == 0) {
                name_node = child;
                break;
            }
        }
        
        if (!ts_node_is_null(name_node)) {
            uint32_t start = ts_node_start_byte(name_node);
            uint32_t end = ts_node_end_byte(name_node);
            char *name = copy_substring(source, start, end, arena);
            
            if (name) {
                /* Add to codemap entries */
                if (file->entry_count < 1000) { /* Prevent overflow */
                    /* Allocate new entries array if needed */
                    size_t new_size = sizeof(CodemapEntry) * (file->entry_count + 1);
                    CodemapEntry *new_entries = (CodemapEntry*)(arena->base + arena->pos);
                    arena->pos += new_size;
                    
                    /* Copy existing entries */
                    if (file->entry_count > 0 && file->entries) {
                        memcpy(new_entries, file->entries, sizeof(CodemapEntry) * file->entry_count);
                    }
                    
                    /* Initialize new entry */
                    CodemapEntry *entry = &new_entries[file->entry_count];
                    memset(entry, 0, sizeof(CodemapEntry));
                    
                    strncpy(entry->name, name, sizeof(entry->name) - 1);
                    strcpy(entry->signature, "()"); /* Default empty signature */
                    strcpy(entry->return_type, "void"); /* Default return type */
                    
                    if (container) {
                        strncpy(entry->container, container, sizeof(entry->container) - 1);
                    }
                    
                    entry->kind = CM_FUNCTION;
                    
                    /* Update file with new entries */
                    file->entries = new_entries;
                    file->entry_count++;
                }
            }
        }
    }
    
    /* Example: Class detection */
    else if (strcmp(node_type, "class_declaration") == 0) {
        /* Find the class name - this is highly language specific */
        /* Similar approach to function detection */
        /* ... */
        
        /* Process class methods - recursively visit children with class name as container */
        /* ... */
    }
    
    /* Recursively process all children */
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        process_node(child, source, file, arena, container);
    }
}

/* ============== Required Interface Functions ============== */

/**
 * Initialize the language pack
 * This function must be exported with this exact name
 */
bool initialize(void) {
    printf("Initializing template language pack\n");
    return true;
}

/**
 * Clean up resources when the language pack is unloaded
 * This function must be exported with this exact name
 */
void cleanup(void) {
    printf("Cleaning up template language pack resources\n");
}

/**
 * Return the list of file extensions supported by this language pack
 * This function must be exported with this exact name
 */
const char **get_extensions(size_t *count) {
    if (count) {
        *count = lang_extension_count;
    }
    return lang_extensions;
}

/**
 * Parse a source file and extract code structure information
 * This function must be exported with this exact name
 */
bool parse_file(const char *path, const char *source, size_t source_len, 
               CodemapFile *file, Arena *arena) {
    if (!path || !source || !file || !arena) {
        fprintf(stderr, "ERROR: Invalid arguments to parse_file\n");
        return false;
    }
    
    printf("Parsing file with tree-sitter: %s\n", path);
    
    /* Create a tree-sitter parser */
    TSParser *parser = ts_parser_new();
    if (!parser) {
        fprintf(stderr, "ERROR: Failed to create tree-sitter parser\n");
        return false;
    }
    
    /* Set language to your language */
    const TSLanguage *language = tree_sitter_your_language();
    if (!language) {
        fprintf(stderr, "ERROR: Failed to load language grammar\n");
        ts_parser_delete(parser);
        return false;
    }
    
    if (!ts_parser_set_language(parser, language)) {
        fprintf(stderr, "ERROR: Failed to set parser language\n");
        ts_parser_delete(parser);
        return false;
    }
    
    /* Parse the source code */
    TSTree *tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)source_len);
    if (!tree) {
        fprintf(stderr, "ERROR: Failed to parse source code\n");
        ts_parser_delete(parser);
        return false;
    }
    
    /* Get the syntax tree root node */
    TSNode root_node = ts_tree_root_node(tree);
    
    /* Process the AST to extract code entities */
    process_node(root_node, source, file, arena, NULL);
    
    /* Clean up resources */
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    
    printf("Successfully extracted %zu code entities from %s\n", file->entry_count, path);
    return true;
}