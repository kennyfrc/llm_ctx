#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

int main() {
    // Try to load the tree-sitter library
    void *tree_sitter_lib = dlopen("/opt/homebrew/lib/libtree-sitter.dylib", RTLD_LAZY | RTLD_GLOBAL);
    if (!tree_sitter_lib) {
        printf("Error: Failed to load tree-sitter library: %s\n", dlerror());
        return 1;
    }
    printf("Successfully loaded tree-sitter library\n");
    
    // Try to load the JavaScript grammar library
    void *js_grammar_lib = dlopen("/Users/kennyfrc/Documents/code/fun/llm_ctx/packs/javascript/libtree-sitter-javascript.dylib", RTLD_LAZY);
    if (!js_grammar_lib) {
        printf("Error: Failed to load JavaScript grammar library: %s\n", dlerror());
        dlclose(tree_sitter_lib);
        return 1;
    }
    printf("Successfully loaded JavaScript grammar library\n");
    
    // Try to get the tree_sitter_javascript symbol
    typedef void* (*language_fn)(void);
    language_fn tree_sitter_javascript = (language_fn)dlsym(js_grammar_lib, "tree_sitter_javascript");
    if (!tree_sitter_javascript) {
        printf("Warning: Could not find tree_sitter_javascript symbol: %s\n", dlerror());
        
        // Try with underscore
        tree_sitter_javascript = (language_fn)dlsym(js_grammar_lib, "_tree_sitter_javascript");
        if (!tree_sitter_javascript) {
            printf("Error: Could not find _tree_sitter_javascript symbol either: %s\n", dlerror());
            dlclose(js_grammar_lib);
            dlclose(tree_sitter_lib);
            return 1;
        }
        printf("Found _tree_sitter_javascript symbol with underscore\n");
    } else {
        printf("Found tree_sitter_javascript symbol without underscore\n");
    }
    
    // Try calling the function
    printf("Calling tree_sitter_javascript function...\n");
    void *language = tree_sitter_javascript();
    if (!language) {
        printf("Error: tree_sitter_javascript returned NULL\n");
    } else {
        printf("Success: tree_sitter_javascript returned a language\n");
    }
    
    // Clean up
    dlclose(js_grammar_lib);
    dlclose(tree_sitter_lib);
    
    return 0;
}