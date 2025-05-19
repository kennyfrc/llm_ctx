#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dlfcn.h>

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

// Initialize a simple arena
void arena_init(Arena *arena, size_t size) {
    arena->start = malloc(size);
    arena->end = (char*)arena->start + size;
    arena->size = size;
    arena->allocated = 0;
}

void *arena_push_size(Arena *arena, size_t size, size_t align) {
    // Simple alignment
    size_t mask = align - 1;
    size_t addr = (size_t)arena->start + arena->allocated;
    size_t aligned_addr = (addr + mask) & ~mask;
    size_t offset = aligned_addr - (size_t)arena->start;
    
    if (offset + size > arena->size) {
        return NULL; // Out of memory
    }
    
    void *ptr = (char*)arena->start + offset;
    arena->allocated = offset + size;
    memset(ptr, 0, size);
    return ptr;
}

void arena_destroy(Arena *arena) {
    if (arena->start) {
        free(arena->start);
        arena->start = NULL;
        arena->allocated = 0;
        arena->size = 0;
    }
}

// Function to add codemap entries
CodemapEntry *codemap_add_entry(CodemapFile *file, const char *name, const char *signature, 
                              const char *return_type, const char *container, CMKind kind, 
                              Arena *arena) {
    // Calculate required size in arena
    size_t required = sizeof(CodemapEntry) * (file->entry_count + 1);
    
    // Check if enough space in arena
    size_t available = arena->size - arena->allocated;
    if (required > available) {
        return NULL;
    }
    
    // Allocate new array with one more element
    CodemapEntry *new_entries = (CodemapEntry*)((char*)arena->start + arena->allocated);
    arena->allocated += required;
    
    // Copy existing entries if any
    if (file->entry_count > 0 && file->entries) {
        memcpy(new_entries, file->entries, file->entry_count * sizeof(CodemapEntry));
    }
    
    // Initialize the new entry
    CodemapEntry *new_entry = &new_entries[file->entry_count];
    memset(new_entry, 0, sizeof(CodemapEntry)); // Zero out the new entry
    
    // Set the fields
    strncpy(new_entry->name, name ? name : "<anonymous>", sizeof(new_entry->name) - 1);
    strncpy(new_entry->signature, signature ? signature : "()", sizeof(new_entry->signature) - 1);
    strncpy(new_entry->return_type, return_type ? return_type : "void", sizeof(new_entry->return_type) - 1);
    strncpy(new_entry->container, container ? container : "", sizeof(new_entry->container) - 1);
    new_entry->kind = kind;
    
    // Update the file
    file->entries = new_entries;
    file->entry_count++;
    
    return new_entry;
}

// Simple implementation to count codemap entries
void print_codemap_entries(CodemapFile *file) {
    printf("Entries for file: %s\n", file->path);
    int functions = 0, classes = 0, methods = 0, types = 0;
    
    for (size_t i = 0; i < file->entry_count; i++) {
        CodemapEntry *entry = &file->entries[i];
        switch (entry->kind) {
            case CM_FUNCTION: 
                functions++; 
                printf("  Function: %s%s", entry->name, entry->signature);
                if (entry->return_type[0] && strcmp(entry->return_type, "void") != 0) {
                    printf(" -> %s", entry->return_type);
                }
                printf("\n");
                break;
            case CM_CLASS: 
                classes++; 
                printf("  Class: %s\n", entry->name);
                break;
            case CM_METHOD: 
                methods++; 
                printf("  Method: %s%s in %s", entry->name, entry->signature, entry->container);
                if (entry->return_type[0] && strcmp(entry->return_type, "void") != 0) {
                    printf(" -> %s", entry->return_type);
                }
                printf("\n");
                break;
            case CM_TYPE:
                types++;
                printf("  Type: %s\n", entry->name);
                break;
            default: break;
        }
    }
    
    printf("\nSummary: %d functions, %d classes, %d methods, %d types\n", 
           functions, classes, methods, types);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <js_file_path> [tree_sitter_path]\n", argv[0]);
        return 1;
    }
    
    const char *js_file = argv[1];
    const char *tree_sitter_path = NULL;
    
    // Check if Tree-sitter path was provided
    if (argc > 2) {
        tree_sitter_path = argv[2];
        printf("Using provided Tree-sitter path: %s\n", tree_sitter_path);
        
        // Try to preload the Tree-sitter library
        if (tree_sitter_path && dlopen(tree_sitter_path, RTLD_LAZY | RTLD_GLOBAL)) {
            printf("Preloaded Tree-sitter library from: %s\n", tree_sitter_path);
        } else if (tree_sitter_path) {
            printf("Warning: Failed to preload Tree-sitter: %s\n", dlerror());
        }
    }
    
    // Try to preload the JavaScript grammar
    void *js_grammar_lib = dlopen("./packs/javascript/libtree-sitter-javascript.dylib", RTLD_LAZY | RTLD_GLOBAL);
    if (js_grammar_lib) {
        printf("Successfully preloaded JavaScript grammar library\n");
        
        // Check if it has the right symbol
        void *js_symbol = dlsym(js_grammar_lib, "tree_sitter_javascript");
        if (js_symbol) {
            printf("Found tree_sitter_javascript symbol in the grammar library\n");
        } else {
            printf("Warning: Could not find tree_sitter_javascript symbol in grammar library: %s\n", dlerror());
            
            // Try with underscore prefix (macOS sometimes adds this)
            js_symbol = dlsym(js_grammar_lib, "_tree_sitter_javascript");
            if (js_symbol) {
                printf("Found _tree_sitter_javascript symbol (with underscore) in the grammar library\n");
            } else {
                printf("Warning: Could not find _tree_sitter_javascript symbol either: %s\n", dlerror());
            }
        }
    } else {
        printf("Warning: Could not preload JavaScript grammar: %s\n", dlerror());
    }
    
    // Try to load the JavaScript language pack
    void *pack_handle = dlopen("./packs/javascript/parser.so", RTLD_LAZY);
    
    if (!pack_handle) {
        printf("Error: Could not load language pack: %s\n", dlerror());
        if (js_grammar_lib) dlclose(js_grammar_lib);
        return 1;
    }
    
    // Get function pointers
    initialize_fn initialize = (initialize_fn)dlsym(pack_handle, "initialize");
    cleanup_fn cleanup = (cleanup_fn)dlsym(pack_handle, "cleanup");
    parse_file_fn parse_file = (parse_file_fn)dlsym(pack_handle, "parse_file");
    get_extensions_fn get_extensions = (get_extensions_fn)dlsym(pack_handle, "get_extensions");
    
    if (!initialize || !cleanup || !parse_file || !get_extensions) {
        printf("Error: Could not find required functions in language pack\n");
        dlclose(pack_handle);
        return 1;
    }
    
    // Initialize the language pack
    if (!initialize()) {
        printf("Error: Failed to initialize language pack\n");
        dlclose(pack_handle);
        return 1;
    }
    
    // Get supported extensions
    size_t ext_count;
    const char **extensions = get_extensions(&ext_count);
    printf("Supported extensions: ");
    for (size_t i = 0; i < ext_count; i++) {
        printf("%s ", extensions[i]);
    }
    printf("\n");
    
    // Set up arena and file structure
    Arena arena;
    arena_init(&arena, 1024 * 1024); // 1MB
    
    CodemapFile file;
    memset(&file, 0, sizeof(file));
    strncpy(file.path, js_file, sizeof(file.path) - 1);
    
    // Read file contents
    FILE *f = fopen(js_file, "rb");
    if (!f) {
        printf("Error: Could not open file %s\n", js_file);
        cleanup();
        dlclose(pack_handle);
        arena_destroy(&arena);
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *source = (char*)arena_push_size(&arena, file_size + 1, 1);
    if (!source) {
        printf("Error: Could not allocate memory for file contents\n");
        fclose(f);
        cleanup();
        dlclose(pack_handle);
        arena_destroy(&arena);
        return 1;
    }
    
    size_t bytes_read = fread(source, 1, file_size, f);
    fclose(f);
    
    if (bytes_read != (size_t)file_size) {
        printf("Error: Could not read file contents\n");
        cleanup();
        dlclose(pack_handle);
        arena_destroy(&arena);
        return 1;
    }
    
    source[file_size] = '\0';
    
    // Parse the file
    bool result = parse_file(js_file, source, file_size, &file, &arena);
    
    if (result) {
        printf("Successfully parsed file!\n");
        print_codemap_entries(&file);
    } else {
        printf("Failed to parse file\n");
    }
    
    // Clean up
    cleanup();
    dlclose(pack_handle);
    arena_destroy(&arena);
    
    return result ? 0 : 1;
}