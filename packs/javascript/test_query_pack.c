#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

// Test the query-based pack directly
int main(int argc, char *argv[]) {
    // Load the JavaScript pack
    void *handle = dlopen("./parser.so", RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Cannot open parser.so: %s\n", dlerror());
        return 1;
    }
    
    // Get function pointers
    bool (*initialize)(void) = dlsym(handle, "initialize");
    void (*cleanup)(void) = dlsym(handle, "cleanup");
    bool (*parse_file)(const char*, const char*, size_t, void*, void*) = dlsym(handle, "parse_file");
    
    if (!initialize || !cleanup || !parse_file) {
        fprintf(stderr, "Cannot find required functions\n");
        dlclose(handle);
        return 1;
    }
    
    // Initialize the pack
    if (!initialize()) {
        fprintf(stderr, "Failed to initialize pack\n");
        dlclose(handle);
        return 1;
    }
    
    // Read test file
    const char *test_file = argc > 1 ? argv[1] : "test_query.js";
    FILE *f = fopen(test_file, "r");
    if (!f) {
        fprintf(stderr, "Cannot open %s\n", test_file);
        cleanup();
        dlclose(handle);
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *source = malloc(size + 1);
    fread(source, 1, size, f);
    source[size] = '\0';
    fclose(f);
    
    printf("Test file content:\n%s\n", source);
    printf("--- Starting parse ---\n");
    
    // Create a simple arena
    struct { 
        unsigned char *base; 
        size_t pos; 
        size_t size; 
    } arena;
    arena.size = 1024 * 1024;
    arena.base = malloc(arena.size);
    arena.pos = 0;
    
    // Create a codemap file structure
    struct {
        char path[4096];
        void *entries;
        size_t entry_count;
    } file = {0};
    strncpy(file.path, test_file, sizeof(file.path) - 1);
    
    // Parse the file
    bool result = parse_file(test_file, source, strlen(source), &file, &arena);
    
    printf("Parse result: %s\n", result ? "SUCCESS" : "FAILED");
    printf("Entries found: %zu\n", file.entry_count);
    
    // Print what was found
    if (file.entry_count > 0) {
        printf("\nEntities found:\n");
        
        // Cast to the expected structure
        struct {
            char name[128];
            char signature[256];
            char return_type[64];
            char container[128];
            int kind;
        } *entries = (void*)file.entries;
        
        for (size_t i = 0; i < file.entry_count; i++) {
            const char *kind_str = "Unknown";
            switch (entries[i].kind) {
                case 0: kind_str = "Function"; break;
                case 1: kind_str = "Class"; break;
                case 2: kind_str = "Method"; break;
                case 3: kind_str = "Type"; break;
            }
            
            printf("  %zu. %s: %s %s", i+1, kind_str, entries[i].name, entries[i].signature);
            if (entries[i].container[0]) {
                printf(" (in %s)", entries[i].container);
            }
            printf("\n");
        }
    }
    
    // Cleanup
    free(source);
    free(arena.base);
    cleanup();
    dlclose(handle);
    
    return result ? 0 : 1;
}