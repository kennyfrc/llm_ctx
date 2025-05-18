#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <errno.h>

#include "packs.h"
#include "arena.h"

/**
 * Check if a directory exists and is accessible
 */
static bool directory_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

/**
 * Check if a file exists and is accessible
 */
static bool file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

/**
 * Discover language packs in the packs directory
 * Returns true if at least one pack was found
 */
bool initialize_pack_registry(PackRegistry *registry, Arena *arena) {
    if (!registry) return false;
    
    // Initialize registry
    registry->packs = NULL;
    registry->pack_count = 0;
    
    // Try current directory first
    const char *packs_dir = "./packs";
    
    // Check if packs directory exists
    if (!directory_exists(packs_dir)) {
        fprintf(stderr, "Warning: Packs directory not found at %s\n", packs_dir);
        return false;
    }
    
    // Open the packs directory
    DIR *dir = opendir(packs_dir);
    if (!dir) {
        fprintf(stderr, "Warning: Could not open packs directory: %s\n", strerror(errno));
        return false;
    }
    
    // Count the number of potential language packs (directories in packs/)
    size_t potential_packs = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        // Skip hidden files and special entries
        if (entry->d_name[0] == '.') continue;
        
        // Check if it's a directory
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", packs_dir, entry->d_name);
        
        if (directory_exists(path)) {
            potential_packs++;
        }
    }
    
    // Reset directory stream
    rewinddir(dir);
    
    // Allocate memory for packs
    if (potential_packs > 0) {
        registry->packs = arena_push_array(arena, LanguagePack, potential_packs);
        if (!registry->packs) {
            fprintf(stderr, "Error: Failed to allocate memory for language packs\n");
            closedir(dir);
            return false;
        }
    } else {
        closedir(dir);
        return false; // No packs found
    }
    
    // Scan for language packs
    while ((entry = readdir(dir)) != NULL) {
        // Skip hidden files and special entries
        if (entry->d_name[0] == '.') continue;
        
        // Check if it's a directory
        char dir_path[4096];
        snprintf(dir_path, sizeof(dir_path), "%s/%s", packs_dir, entry->d_name);
        
        if (!directory_exists(dir_path)) continue;
        
        // Check if parser.so exists in the directory
        char parser_path[4096];
        snprintf(parser_path, sizeof(parser_path), "%s/parser.so", dir_path);
        
        bool has_parser = file_exists(parser_path);
        
        // Add to registry if it has a parser
        if (has_parser) {
            LanguagePack *pack = &registry->packs[registry->pack_count];
            
            // Set the language name
            strncpy(pack->name, entry->d_name, sizeof(pack->name) - 1);
            pack->name[sizeof(pack->name) - 1] = '\0';
            
            // Set the parser path
            strncpy(pack->path, parser_path, sizeof(pack->path) - 1);
            pack->path[sizeof(pack->path) - 1] = '\0';
            
            // Mark as available
            pack->available = true;
            
            registry->pack_count++;
        }
    }
    
    closedir(dir);
    
    return (registry->pack_count > 0);
}

/**
 * Print the list of available language packs
 */
void print_pack_list(const PackRegistry *registry) {
    if (!registry || !registry->packs || registry->pack_count == 0) {
        printf("No language packs available.\n");
        return;
    }
    
    printf("Available language packs:\n");
    
    for (size_t i = 0; i < registry->pack_count; i++) {
        const LanguagePack *pack = &registry->packs[i];
        printf("  - %s\n", pack->name);
    }
}

/**
 * Clean up the pack registry resources
 */
void cleanup_pack_registry(PackRegistry *registry) {
    if (!registry) return;
    
    // We don't need to free packs as it was allocated from the arena
    registry->packs = NULL;
    registry->pack_count = 0;
}