#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "arena.h"
#include "codemap.h"
#include "packs.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <js_file_path>\n", argv[0]);
        return 1;
    }

    // Initialize arena
    Arena arena = arena_create(1024 * 1024 * 10); // 10MB arena
    
    // Initialize pack registry
    bool registry_initialized = initialize_pack_registry(&g_pack_registry, &arena);
    if (registry_initialized) {
        size_t loaded = load_language_packs(&g_pack_registry);
        fprintf(stderr, "Loaded %zu language pack(s).\n", loaded);
        
        // Build extension map
        build_extension_map(&g_pack_registry, &arena);
    }
    
    // Initialize codemap
    Codemap cm = codemap_init(&arena);
    
    // Create file list
    const char *files[1] = { argv[1] };
    
    // Process JS/TS files
    fprintf(stderr, "Processing file: %s\n", argv[1]);
    bool result = process_js_ts_files(&cm, files, 1, &arena);
    
    if (result) {
        // Generate codemap
        char *buffer = arena_push_array(&arena, char, 1024 * 1024); // 1MB buffer
        if (buffer) {
            size_t pos = 0;
            codemap_generate(&cm, buffer, &pos, 1024 * 1024);
            
            // Print the codemap
            printf("%.*s\n", (int)pos, buffer);
        } else {
            fprintf(stderr, "Failed to allocate memory for codemap output\n");
        }
    } else {
        fprintf(stderr, "Failed to process file\n");
    }
    
    // Cleanup
    cleanup_pack_registry(&g_pack_registry);
    arena_destroy(&arena);
    
    return 0;
}