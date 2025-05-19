#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdbool.h>

// Simplified codemap structures
typedef enum { 
    CM_FUNCTION = 0, 
    CM_CLASS = 1, 
    CM_METHOD = 2, 
    CM_TYPE = 3
} CMKind;

typedef struct {
    char name[128];
    char signature[256];
    char return_type[64];
    char container[128];
    CMKind kind;
} CodemapEntry;

typedef struct {
    char path[4096];
    CodemapEntry *entries;
    size_t entry_count;
} CodemapFile;

typedef struct {
    void *start;
    void *end;
    size_t size;
    size_t allocated;
} Arena;

// Function pointers for language pack functions
typedef bool (*initialize_fn)(void);
typedef void (*cleanup_fn)(void);
typedef bool (*parse_file_fn)(const char*, const char*, size_t, CodemapFile*, Arena*);
typedef const char** (*get_extensions_fn)(size_t*);

// Initialize an arena
void arena_init(Arena *arena, size_t size) {
    arena->start = malloc(size);
    arena->size = size;
    arena->allocated = 0;
    arena->end = (char*)arena->start + size;
}

void arena_destroy(Arena *arena) {
    if (arena->start) {
        free(arena->start);
        arena->start = NULL;
        arena->size = 0;
        arena->allocated = 0;
        arena->end = NULL;
    }
}

// Print the codemap entries
void print_codemap_entries(CodemapFile *file) {
    printf("Entries for file: %s\n", file->path);
    
    for (size_t i = 0; i < file->entry_count; i++) {
        CodemapEntry *entry = &file->entries[i];
        
        switch(entry->kind) {
            case CM_FUNCTION:
                printf("  Function: %s%s", entry->name, entry->signature);
                if (strcmp(entry->return_type, "void") != 0) {
                    printf(" -> %s", entry->return_type);
                }
                printf("\n");
                break;
                
            case CM_CLASS:
                printf("  Class: %s\n", entry->name);
                break;
                
            case CM_METHOD:
                printf("  Method: %s%s in %s", entry->name, entry->signature, entry->container);
                if (strcmp(entry->return_type, "void") != 0) {
                    printf(" -> %s", entry->return_type);
                }
                printf("\n");
                break;
                
            case CM_TYPE:
                printf("  Type: %s\n", entry->name);
                break;
                
            default:
                printf("  Unknown entry kind: %d\n", entry->kind);
                break;
        }
    }
    
    printf("Total entries: %zu\n", file->entry_count);
}

int main() {
    printf("=== Complete JavaScript Language Pack Test ===\n\n");
    
    // Try to load the tree-sitter library directly
    void *tree_sitter_lib = dlopen("/opt/homebrew/lib/libtree-sitter.dylib", RTLD_LAZY | RTLD_GLOBAL);
    if (!tree_sitter_lib) {
        printf("Error: Could not load Tree-sitter library: %s\n", dlerror());
        return 1;
    }
    printf("Successfully loaded Tree-sitter library\n");
    
    // Load the JavaScript grammar
    void *js_grammar_lib = dlopen("/Users/kennyfrc/Documents/code/fun/llm_ctx/packs/javascript/libtree-sitter-javascript.dylib", RTLD_LAZY | RTLD_GLOBAL);
    if (!js_grammar_lib) {
        printf("Error: Could not load JavaScript grammar library: %s\n", dlerror());
        dlclose(tree_sitter_lib);
        return 1;
    }
    printf("Successfully loaded JavaScript grammar library\n");
    
    // Check if the tree_sitter_javascript symbol exists
    void *symbol = dlsym(js_grammar_lib, "tree_sitter_javascript");
    if (!symbol) {
        symbol = dlsym(js_grammar_lib, "_tree_sitter_javascript");
        if (symbol) {
            printf("Found _tree_sitter_javascript symbol (with underscore)\n");
        } else {
            printf("Error: Could not find tree_sitter_javascript symbol: %s\n", dlerror());
            dlclose(js_grammar_lib);
            dlclose(tree_sitter_lib);
            return 1;
        }
    } else {
        printf("Found tree_sitter_javascript symbol (without underscore)\n");
    }
    
    // Load the language pack
    void *pack_lib = dlopen("/Users/kennyfrc/Documents/code/fun/llm_ctx/packs/javascript/parser.so", RTLD_LAZY);
    if (!pack_lib) {
        printf("Error: Could not load language pack: %s\n", dlerror());
        dlclose(js_grammar_lib);
        dlclose(tree_sitter_lib);
        return 1;
    }
    printf("Successfully loaded language pack\n");
    
    // Get function pointers
    initialize_fn initialize = (initialize_fn)dlsym(pack_lib, "initialize");
    cleanup_fn cleanup = (cleanup_fn)dlsym(pack_lib, "cleanup");
    get_extensions_fn get_extensions = (get_extensions_fn)dlsym(pack_lib, "get_extensions");
    parse_file_fn parse_file = (parse_file_fn)dlsym(pack_lib, "parse_file");
    
    if (!initialize || !cleanup || !get_extensions || !parse_file) {
        printf("Error: Could not find required functions in language pack: %s\n", dlerror());
        dlclose(pack_lib);
        dlclose(js_grammar_lib);
        dlclose(tree_sitter_lib);
        return 1;
    }
    printf("Successfully got function pointers from language pack\n");
    
    // Initialize the language pack
    printf("Initializing language pack...\n");
    if (!initialize()) {
        printf("Error: Failed to initialize language pack\n");
        dlclose(pack_lib);
        dlclose(js_grammar_lib);
        dlclose(tree_sitter_lib);
        return 1;
    }
    printf("Language pack initialized successfully\n");
    
    // Get supported extensions
    size_t ext_count;
    const char **extensions = get_extensions(&ext_count);
    printf("Supported extensions: ");
    for (size_t i = 0; i < ext_count; i++) {
        printf("%s ", extensions[i]);
    }
    printf("\n");
    
    // Create a sample JavaScript file
    const char *test_source = "// Sample JavaScript file for testing\n"
                              "function hello(name) {\n"
                              "  return \"Hello, \" + name + \"!\";\n"
                              "}\n"
                              "\n"
                              "class Person {\n"
                              "  constructor(name, age) {\n"
                              "    this.name = name;\n"
                              "    this.age = age;\n"
                              "  }\n"
                              "  \n"
                              "  greet() {\n"
                              "    return \"Hi, I'm \" + this.name;\n"
                              "  }\n"
                              "}\n"
                              "\n"
                              "// Export the functions\n"
                              "module.exports = {\n"
                              "  hello,\n"
                              "  Person\n"
                              "};\n";
    
    size_t source_len = strlen(test_source);
    printf("Created sample JavaScript source (%zu bytes)\n", source_len);
    
    // Set up arena and codemap file
    Arena arena;
    arena_init(&arena, 1024 * 1024); // 1MB arena
    
    CodemapFile file;
    memset(&file, 0, sizeof(file));
    strncpy(file.path, "test.js", sizeof(file.path) - 1);
    
    // Parse the file
    printf("Parsing sample file...\n");
    bool result = parse_file("test.js", test_source, source_len, &file, &arena);
    
    if (result) {
        printf("Successfully parsed file!\n");
        print_codemap_entries(&file);
    } else {
        printf("Failed to parse file\n");
    }
    
    // Clean up
    printf("Cleaning up...\n");
    cleanup();
    arena_destroy(&arena);
    dlclose(pack_lib);
    dlclose(js_grammar_lib);
    dlclose(tree_sitter_lib);
    
    printf("\nTest completed %s!\n", result ? "successfully" : "with errors");
    return result ? 0 : 1;
}