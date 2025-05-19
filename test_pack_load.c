#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <dlfcn.h>

int main(int argc, char *argv[]) {
    printf("Testing JavaScript pack loading from main project\n");
    
    const char *pack_path = "./packs/javascript/parser.so";
    printf("Attempting to load: %s\n", pack_path);
    
    void *handle = dlopen(pack_path, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Error: Failed to load pack: %s\n", dlerror());
        return 1;
    }
    
    printf("Successfully loaded pack\n");
    
    // Get extensions function
    typedef const char** (*get_extensions_fn)(size_t*);
    get_extensions_fn get_extensions = (get_extensions_fn)dlsym(handle, "get_extensions");
    
    if (!get_extensions) {
        fprintf(stderr, "Error: Could not find get_extensions function: %s\n", dlerror());
        dlclose(handle);
        return 1;
    }
    
    printf("Successfully found get_extensions function\n");
    
    // Call the function
    size_t count = 0;
    const char **extensions = get_extensions(&count);
    
    printf("Supported extensions (%zu):\n", count);
    for (size_t i = 0; i < count && extensions[i]; i++) {
        printf("  %s\n", extensions[i]);
    }
    
    // Try to get initialize function
    typedef bool (*initialize_fn)(void);
    initialize_fn initialize = (initialize_fn)dlsym(handle, "initialize");
    
    if (!initialize) {
        fprintf(stderr, "Error: Could not find initialize function: %s\n", dlerror());
        dlclose(handle);
        return 1;
    }
    
    printf("Successfully found initialize function\n");
    
    // Try to initialize
    printf("Attempting to initialize pack...\n");
    bool init_result = initialize();
    printf("Initialize result: %s\n", init_result ? "SUCCESS" : "FAILED");
    
    // Clean up
    printf("Cleaning up\n");
    dlclose(handle);
    printf("Test completed\n");
    
    return 0;
}