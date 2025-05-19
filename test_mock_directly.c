#include <stdio.h>
#include <dlfcn.h>

typedef struct {
    const char *name;
    int node_count;
    void *state;
} TSLanguage;

typedef const TSLanguage* (*language_fn)(void);

int main() {
    void *handle;
    language_fn tree_sitter_javascript;
    const TSLanguage *lang;
    
    printf("Attempting to load mock grammar...\n");
    
    // Try to load the library
    handle = dlopen("./packs/javascript/libtree-sitter-javascript.dylib", RTLD_LAZY);
    if (!handle) {
        printf("Error loading library: %s\n", dlerror());
        return 1;
    }
    
    printf("Library loaded successfully!\n");
    
    // Try to get the symbol - with and without leading underscore
    tree_sitter_javascript = (language_fn)dlsym(handle, "tree_sitter_javascript");
    if (!tree_sitter_javascript) {
        printf("Symbol 'tree_sitter_javascript' not found, trying with underscore...\n");
        
        tree_sitter_javascript = (language_fn)dlsym(handle, "_tree_sitter_javascript");
        if (!tree_sitter_javascript) {
            printf("Error: Could not find symbol: %s\n", dlerror());
            dlclose(handle);
            return 1;
        }
        printf("Found symbol with underscore '_tree_sitter_javascript'\n");
    } else {
        printf("Found symbol 'tree_sitter_javascript' without underscore\n");
    }
    
    // Try to call the function
    printf("Calling the function...\n");
    lang = tree_sitter_javascript();
    
    if (lang) {
        printf("Success! Got language: %s (node count: %d)\n", 
               lang->name, lang->node_count);
    } else {
        printf("Error: Function returned NULL\n");
    }
    
    dlclose(handle);
    return 0;
}