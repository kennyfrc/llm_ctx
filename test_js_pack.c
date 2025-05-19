#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dlfcn.h>

#include "arena.h"
#include "codemap.h"
#include "packs.h"

int main(int argc, char *argv[]) {
    printf("Testing JavaScript language pack integration\n");
    
    // Create a new arena
    Arena arena = arena_create(1024 * 1024); // 1MB
    
    // Initialize the pack registry
    PackRegistry registry = {0};
    bool initialized = initialize_pack_registry(&registry, &arena);
    
    if (!initialized) {
        fprintf(stderr, "Failed to initialize pack registry\n");
        arena_destroy(&arena);
        return 1;
    }
    
    printf("Pack registry initialized successfully.\n");
    
    // Load the packs
    size_t loaded = load_language_packs(&registry);
    printf("Loaded %zu language packs.\n", loaded);
    
    if (loaded == 0) {
        fprintf(stderr, "No language packs were loaded\n");
        cleanup_pack_registry(&registry);
        arena_destroy(&arena);
        return 1;
    }
    
    // Build the extension map
    bool built = build_extension_map(&registry, &arena);
    if (!built) {
        fprintf(stderr, "Failed to build extension map\n");
        cleanup_pack_registry(&registry);
        arena_destroy(&arena);
        return 1;
    }
    
    // Print all loaded packs
    print_pack_list(&registry);
    
    // Look up JavaScript extensions
    LanguagePack *js_pack = NULL;
    
    const char *extensions[] = {".js", ".jsx", ".ts", ".tsx"};
    for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++) {
        LanguagePack *pack = find_pack_for_extension(&registry, extensions[i]);
        if (pack) {
            printf("Found pack for %s: %s (available: %s)\n", 
                   extensions[i], pack->name, pack->available ? "yes" : "no");
            
            // Remember the JavaScript pack for later
            if (strcmp(pack->name, "javascript") == 0) {
                js_pack = pack;
            }
        } else {
            printf("No pack found for %s\n", extensions[i]);
        }
    }
    
    // Clean up
    cleanup_pack_registry(&registry);
    
    // Clean up arena
    arena_destroy(&arena);
    
    printf("Test completed successfully.\n");
    return 0;
}