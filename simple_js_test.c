#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

// Function pointers for language pack functions
typedef bool (*initialize_fn)(void);
typedef void (*cleanup_fn)(void);
typedef bool (*parse_file_fn)(const char*, const char*, size_t, void*, void*);
typedef const char** (*get_extensions_fn)(size_t*);

int main() {
    printf("=== Simple JavaScript Language Pack Test ===\n\n");
    
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
    
    if (!initialize || !cleanup || !get_extensions) {
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
    
    // Cleanup
    printf("Cleaning up...\n");
    cleanup();
    dlclose(pack_lib);
    dlclose(js_grammar_lib);
    dlclose(tree_sitter_lib);
    
    printf("\nTest completed successfully!\n");
    return 0;
}