#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>

// Tree-sitter structures
typedef struct TSLanguage TSLanguage;
typedef struct TSParser TSParser;
typedef struct TSTree TSTree;

typedef struct {
    const void *tree;
    uint32_t id;
    uint32_t version;
} TSNode;

typedef const TSLanguage* (*ts_language_fn)(void);
typedef TSParser* (*ts_parser_new_fn)(void);
typedef void (*ts_parser_delete_fn)(TSParser*);
typedef int (*ts_parser_set_language_fn)(TSParser*, const TSLanguage*);
typedef TSTree* (*ts_parser_parse_string_fn)(TSParser*, const TSTree*, const char*, uint32_t);
typedef void (*ts_tree_delete_fn)(TSTree*);
typedef TSNode (*ts_tree_root_node_fn)(const TSTree*);
typedef const char* (*ts_node_type_fn)(TSNode);

int main() {
    printf("=== Minimal Tree-sitter Test ===\n\n");
    
    // Load Tree-sitter library
    void *ts_lib = dlopen("/opt/homebrew/lib/libtree-sitter.dylib", RTLD_LAZY | RTLD_GLOBAL);
    if (!ts_lib) {
        printf("Error: Could not load tree-sitter library: %s\n", dlerror());
        return 1;
    }
    printf("Successfully loaded Tree-sitter library\n");
    
    // Find tree-sitter functions
    ts_parser_new_fn ts_parser_new = (ts_parser_new_fn)dlsym(ts_lib, "ts_parser_new");
    ts_parser_delete_fn ts_parser_delete = (ts_parser_delete_fn)dlsym(ts_lib, "ts_parser_delete");
    ts_parser_set_language_fn ts_parser_set_language = (ts_parser_set_language_fn)dlsym(ts_lib, "ts_parser_set_language");
    ts_parser_parse_string_fn ts_parser_parse_string = (ts_parser_parse_string_fn)dlsym(ts_lib, "ts_parser_parse_string");
    ts_tree_delete_fn ts_tree_delete = (ts_tree_delete_fn)dlsym(ts_lib, "ts_tree_delete");
    ts_tree_root_node_fn ts_tree_root_node = (ts_tree_root_node_fn)dlsym(ts_lib, "ts_tree_root_node");
    ts_node_type_fn ts_node_type = (ts_node_type_fn)dlsym(ts_lib, "ts_node_type");
    
    if (!ts_parser_new || !ts_parser_delete || !ts_parser_set_language || 
        !ts_parser_parse_string || !ts_tree_delete || !ts_tree_root_node || !ts_node_type) {
        printf("Error: Could not load tree-sitter functions: %s\n", dlerror());
        dlclose(ts_lib);
        return 1;
    }
    printf("Successfully loaded Tree-sitter functions\n");
    
    // Load JavaScript grammar
    void *js_lib = dlopen("/Users/kennyfrc/Documents/code/fun/llm_ctx/packs/javascript/libtree-sitter-javascript.dylib", RTLD_LAZY);
    if (!js_lib) {
        printf("Error: Could not load JavaScript grammar: %s\n", dlerror());
        dlclose(ts_lib);
        return 1;
    }
    printf("Successfully loaded JavaScript grammar library\n");
    
    // Get tree_sitter_javascript function
    ts_language_fn tree_sitter_javascript = (ts_language_fn)dlsym(js_lib, "tree_sitter_javascript");
    if (!tree_sitter_javascript) {
        // Try with underscore (macOS symbol mangling)
        tree_sitter_javascript = (ts_language_fn)dlsym(js_lib, "_tree_sitter_javascript");
        if (!tree_sitter_javascript) {
            printf("Error: Could not find tree_sitter_javascript function: %s\n", dlerror());
            dlclose(js_lib);
            dlclose(ts_lib);
            return 1;
        }
        printf("Found tree_sitter_javascript function with underscore prefix\n");
    } else {
        printf("Found tree_sitter_javascript function without underscore\n");
    }
    
    // Get the language object
    printf("Calling tree_sitter_javascript()...\n");
    const TSLanguage *js_lang = tree_sitter_javascript();
    if (!js_lang) {
        printf("Error: tree_sitter_javascript() returned NULL\n");
        dlclose(js_lib);
        dlclose(ts_lib);
        return 1;
    }
    printf("Successfully got JavaScript language object\n");
    
    // Create a parser
    printf("Creating a parser...\n");
    TSParser *parser = ts_parser_new();
    if (!parser) {
        printf("Error: ts_parser_new() returned NULL\n");
        dlclose(js_lib);
        dlclose(ts_lib);
        return 1;
    }
    printf("Successfully created a parser\n");
    
    // Set the language
    printf("Setting the language...\n");
    if (!ts_parser_set_language(parser, js_lang)) {
        printf("Error: Failed to set language on parser\n");
        ts_parser_delete(parser);
        dlclose(js_lib);
        dlclose(ts_lib);
        return 1;
    }
    printf("Successfully set language on parser\n");
    
    // Parse a simple JavaScript string
    const char *js_code = "function hello() { return 'world'; }";
    printf("Parsing JavaScript code: %s\n", js_code);
    TSTree *tree = ts_parser_parse_string(parser, NULL, js_code, strlen(js_code));
    if (!tree) {
        printf("Error: Failed to parse JavaScript code\n");
        ts_parser_delete(parser);
        dlclose(js_lib);
        dlclose(ts_lib);
        return 1;
    }
    printf("Successfully parsed JavaScript code\n");
    
    // Get root node
    TSNode root = ts_tree_root_node(tree);
    printf("Root node type: %s\n", ts_node_type(root));
    
    // Clean up
    printf("Cleaning up...\n");
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    dlclose(js_lib);
    dlclose(ts_lib);
    
    printf("\nTest completed successfully!\n");
    return 0;
}