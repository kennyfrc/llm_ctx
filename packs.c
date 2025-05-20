#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <errno.h>
#include <dlfcn.h>  /* For dynamic library loading */
#include <unistd.h> /* For dup, dup2, STDOUT_FILENO */

#include "packs.h"
#include "arena.h"
#include "codemap.h"
#include "debug.h"

/* Global pack registry */
PackRegistry g_pack_registry = {0};

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
    registry->extension_map = NULL;
    registry->extension_map_size = 0;
    
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
            
            // Initialize new fields
            pack->handle = NULL;
            pack->extensions = NULL;
            pack->extension_count = 0;
            pack->initialize = NULL;
            pack->cleanup = NULL;
            pack->parse_file = NULL;
            
            registry->pack_count++;
        }
    }
    
    closedir(dir);
    
    return (registry->pack_count > 0);
}

/**
 * Build the extension map to quickly map file extensions to language packs
 * Returns true if successful
 */
bool build_extension_map(PackRegistry *registry, Arena *arena) {
    if (!registry || !registry->packs || registry->pack_count == 0) {
        return false;
    }
    
    // Count total extensions
    size_t total_extensions = 0;
    for (size_t i = 0; i < registry->pack_count; i++) {
        LanguagePack *pack = &registry->packs[i];
        if (pack->available && pack->extension_count > 0) {
            total_extensions += pack->extension_count;
        }
    }
    
    if (total_extensions == 0) {
        return false;
    }
    
    // Allocate extension map
    registry->extension_map = arena_push_array(arena, char*, total_extensions * 2);
    if (!registry->extension_map) {
        fprintf(stderr, "Error: Failed to allocate memory for extension map\n");
        return false;
    }
    
    // Build extension map
    size_t map_index = 0;
    for (size_t i = 0; i < registry->pack_count; i++) {
        LanguagePack *pack = &registry->packs[i];
        if (!pack->available || !pack->extensions) continue;
        
        for (size_t j = 0; j < pack->extension_count; j++) {
            registry->extension_map[map_index++] = (char*)pack->extensions[j];
            registry->extension_map[map_index++] = (char*)i;  // Store pack index
        }
    }
    
    registry->extension_map_size = map_index / 2;  // Pairs of entries
    return true;
}

/**
 * Find a language pack for a given file extension
 * Returns NULL if no suitable pack is found
 */
LanguagePack *find_pack_for_extension(PackRegistry *registry, const char *extension) {
    if (!registry || !registry->extension_map || !extension) {
        return NULL;
    }
    
    // Look up extension in the map
    for (size_t i = 0; i < registry->extension_map_size * 2; i += 2) {
        if (strcmp(registry->extension_map[i], extension) == 0) {
            size_t pack_index = (size_t)registry->extension_map[i + 1];
            if (pack_index < registry->pack_count) {
                return &registry->packs[pack_index];
            }
        }
    }
    
    return NULL;
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
        printf("  - %s", pack->name);
        
        // Show status
        if (!pack->available) {
            printf(" (unavailable)");
        } else if (!pack->handle) {
            printf(" (not loaded)");
        } else {
            printf(" (loaded)");
        }
        
        // Show supported extensions if available
        if (pack->extensions && pack->extension_count > 0) {
            printf(" extensions: ");
            for (size_t j = 0; j < pack->extension_count; j++) {
                printf("%s", pack->extensions[j]);
                if (j < pack->extension_count - 1) {
                    printf(", ");
                }
            }
        }
        
        printf("\n");
    }
}

/**
 * Load dynamic libraries for all available language packs
 * Resolves function pointers and initializes extensions
 * Returns the number of successfully loaded packs
 */
size_t load_language_packs(PackRegistry *registry) {
    if (!registry || !registry->packs || registry->pack_count == 0) {
        return 0;
    }
    
    size_t loaded_count = 0;
    
    for (size_t i = 0; i < registry->pack_count; i++) {
        LanguagePack *pack = &registry->packs[i];
        
        if (!pack->available) continue;
        
        // Open the dynamic library
        pack->handle = dlopen(pack->path, RTLD_LAZY);
        if (!pack->handle) {
            fprintf(stderr, "Warning: Failed to load language pack '%s': %s\n",
                    pack->name, dlerror());
            pack->available = false;
            continue;
        }
        
        // Clear any existing errors
        dlerror();
        
        // Load function pointers
        *(void **)(&pack->initialize) = dlsym(pack->handle, "initialize");
        if (dlerror() != NULL) {
            fprintf(stderr, "Warning: No 'initialize' function in pack '%s'\n", pack->name);
            dlclose(pack->handle);
            pack->handle = NULL;
            pack->available = false;
            continue;
        }
        
        *(void **)(&pack->cleanup) = dlsym(pack->handle, "cleanup");
        if (dlerror() != NULL) {
            fprintf(stderr, "Warning: No 'cleanup' function in pack '%s'\n", pack->name);
            dlclose(pack->handle);
            pack->handle = NULL;
            pack->available = false;
            continue;
        }
        
        *(void **)(&pack->parse_file) = dlsym(pack->handle, "parse_file");
        if (dlerror() != NULL) {
            fprintf(stderr, "Warning: No 'parse_file' function in pack '%s'\n", pack->name);
            dlclose(pack->handle);
            pack->handle = NULL;
            pack->available = false;
            continue;
        }
        
        // Get extensions
        typedef const char **(*get_extensions_fn)(size_t *count);
        get_extensions_fn get_extensions = (get_extensions_fn)dlsym(pack->handle, "get_extensions");
        if (dlerror() != NULL) {
            fprintf(stderr, "Warning: No 'get_extensions' function in pack '%s'\n", pack->name);
            dlclose(pack->handle);
            pack->handle = NULL;
            pack->available = false;
            continue;
        }
        
        // Get the extensions array
        pack->extensions = get_extensions(&pack->extension_count);
        if (!pack->extensions || pack->extension_count == 0) {
            fprintf(stderr, "Warning: No file extensions defined in pack '%s'\n", pack->name);
            dlclose(pack->handle);
            pack->handle = NULL;
            pack->available = false;
            continue;
        }
        
        // Initialize the language parser
        /* In non-debug mode, we will suppress the console output */
        int saved_stdout = -1;
        int saved_stderr = -1;
        FILE *null_file = NULL;
        
        if (!debug_mode) {
            /* Redirect stdout and stderr to /dev/null to suppress output */
            fflush(stdout);
            fflush(stderr);
            saved_stdout = dup(STDOUT_FILENO);
            saved_stderr = dup(STDERR_FILENO);
            null_file = fopen("/dev/null", "w");
            if (null_file != NULL) {
                dup2(fileno(null_file), STDOUT_FILENO);
                dup2(fileno(null_file), STDERR_FILENO);
                fclose(null_file);
            }
        }
        
        /* Call initialize function */
        bool init_result = pack->initialize();
        
        /* Restore stdout and stderr if we redirected them */
        if (!debug_mode) {
            if (saved_stdout >= 0) {
                fflush(stdout);
                dup2(saved_stdout, STDOUT_FILENO);
                close(saved_stdout);
            }
            if (saved_stderr >= 0) {
                fflush(stderr);
                dup2(saved_stderr, STDERR_FILENO);
                close(saved_stderr);
            }
        }
        
        /* Check result */
        if (!init_result) {
            fprintf(stderr, "Warning: Failed to initialize language pack '%s'\n", pack->name);
            dlclose(pack->handle);
            pack->handle = NULL;
            pack->available = false;
            continue;
        }
        
        loaded_count++;
    }
    
    return loaded_count;
}

/**
 * Clean up the pack registry resources
 * Unloads all dynamic libraries and frees allocated memory
 */
void cleanup_pack_registry(PackRegistry *registry) {
    if (!registry) return;
    
    // Unload dynamic libraries if loaded
    for (size_t i = 0; i < registry->pack_count; i++) {
        LanguagePack *pack = &registry->packs[i];
        
        // Call pack cleanup function if available
        if (pack->cleanup) {
            /* In non-debug mode, suppress console output */
            int saved_stdout = -1;
            int saved_stderr = -1;
            FILE *null_file = NULL;
            
            if (!debug_mode) {
                /* Redirect stdout and stderr to /dev/null */
                fflush(stdout);
                fflush(stderr);
                saved_stdout = dup(STDOUT_FILENO);
                saved_stderr = dup(STDERR_FILENO);
                null_file = fopen("/dev/null", "w");
                if (null_file != NULL) {
                    dup2(fileno(null_file), STDOUT_FILENO);
                    dup2(fileno(null_file), STDERR_FILENO);
                    fclose(null_file);
                }
            }
            
            /* Call cleanup function */
            pack->cleanup();
            
            /* Restore stdout and stderr if redirected */
            if (!debug_mode) {
                if (saved_stdout >= 0) {
                    fflush(stdout);
                    dup2(saved_stdout, STDOUT_FILENO);
                    close(saved_stdout);
                }
                if (saved_stderr >= 0) {
                    fflush(stderr);
                    dup2(saved_stderr, STDERR_FILENO);
                    close(saved_stderr);
                }
            }
        }
        
        // Close dynamic library handle if open
        if (pack->handle) {
            dlclose(pack->handle);
            pack->handle = NULL;
        }
    }
    
    // We don't need to free packs as it was allocated from the arena
    registry->packs = NULL;
    registry->pack_count = 0;
    registry->extension_map = NULL;
    registry->extension_map_size = 0;
}