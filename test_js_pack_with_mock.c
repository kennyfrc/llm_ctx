#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dlfcn.h>

// Simplified tree-sitter API
typedef struct TSLanguage {
    const char *name;
    int node_count;
    void *state;
} TSLanguage;

typedef const TSLanguage* (*language_fn)(void);

// Forward declarations for codemap types
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

// Function to create a mock JavaScript grammar
bool create_mock_grammar(void) {
    // Write the mock grammar to a file
    FILE *f = fopen("mock_grammar.c", "w");
    if (!f) {
        fprintf(stderr, "Error creating mock grammar file\n");
        return false;
    }
    
    fprintf(f, "#include <stdio.h>\n");
    fprintf(f, "typedef struct TSLanguage {\n");
    fprintf(f, "    const char *name;\n");
    fprintf(f, "    int node_count;\n");
    fprintf(f, "    void *state;\n");
    fprintf(f, "} TSLanguage;\n\n");
    fprintf(f, "const TSLanguage *tree_sitter_javascript(void) {\n");
    fprintf(f, "    static TSLanguage lang = {\"javascript\", 100, NULL};\n");
    fprintf(f, "    printf(\"Mock JavaScript grammar loaded\\n\");\n");
    fprintf(f, "    return &lang;\n");
    fprintf(f, "}\n");
    
    fclose(f);
    
    // Compile the mock grammar
    int result = system("cc -shared -fPIC -o mock_grammar.dylib mock_grammar.c");
    if (result != 0) {
        fprintf(stderr, "Error compiling mock grammar\n");
        return false;
    }
    
    printf("Mock grammar compiled successfully\n");
    return true;
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

// Initialize a simple arena
void arena_init(Arena *arena, size_t size) {
    arena->start = malloc(size);
    arena->end = (char*)arena->start + size;
    arena->size = size;
    arena->allocated = 0;
}

void arena_destroy(Arena *arena) {
    if (arena->start) {
        free(arena->start);
        arena->start = NULL;
        arena->allocated = 0;
        arena->size = 0;
    }
}

// Test with our mock implementation
bool test_with_mock(void) {
    // Try to load the Tree-sitter library
    void *tree_sitter_lib = dlopen("/opt/homebrew/lib/libtree-sitter.dylib", RTLD_LAZY);
    if (!tree_sitter_lib) {
        fprintf(stderr, "Error: Could not load Tree-sitter library\n");
        return false;
    }
    printf("Loaded Tree-sitter library\n");
    
    // Load our mock grammar
    void *js_grammar_lib = dlopen("./mock_grammar.dylib", RTLD_LAZY);
    if (!js_grammar_lib) {
        fprintf(stderr, "Error: Could not load mock grammar: %s\n", dlerror());
        dlclose(tree_sitter_lib);
        return false;
    }
    printf("Loaded mock grammar library\n");
    
    // Get the JavaScript language function
    language_fn tree_sitter_javascript = (language_fn)dlsym(js_grammar_lib, "tree_sitter_javascript");
    if (!tree_sitter_javascript) {
        fprintf(stderr, "Error: Could not find tree_sitter_javascript function: %s\n", dlerror());
        dlclose(js_grammar_lib);
        dlclose(tree_sitter_lib);
        return false;
    }
    printf("Found tree_sitter_javascript function\n");
    
    // Call the function to get the language
    const TSLanguage *language = tree_sitter_javascript();
    if (!language) {
        fprintf(stderr, "Error: Could not get JavaScript language\n");
        dlclose(js_grammar_lib);
        dlclose(tree_sitter_lib);
        return false;
    }
    printf("Successfully got JavaScript language: %s\n", language->name);
    
    // Create a simple codemap file and add entries
    Arena arena;
    arena_init(&arena, 1024 * 1024); // 1MB
    
    CodemapFile file;
    memset(&file, 0, sizeof(file));
    strcpy(file.path, "test.js");
    
    // Add some test entries
    codemap_add_entry(&file, "hello", "(name)", "string", "", CM_FUNCTION, &arena);
    codemap_add_entry(&file, "Person", "", "", "", CM_CLASS, &arena);
    codemap_add_entry(&file, "greet", "()", "string", "Person", CM_METHOD, &arena);
    
    printf("Successfully created codemap entries:\n");
    for (size_t i = 0; i < file.entry_count; i++) {
        CodemapEntry *entry = &file.entries[i];
        printf("  Entry %zu: %s %s", i, entry->name, entry->signature);
        if (entry->container[0]) {
            printf(" in %s", entry->container);
        }
        printf("\n");
    }
    
    // Clean up
    arena_destroy(&arena);
    dlclose(js_grammar_lib);
    dlclose(tree_sitter_lib);
    
    return true;
}

int main() {
    printf("====== Testing JavaScript Language Pack with Mock Grammar ======\n");
    
    // Create the mock grammar
    if (!create_mock_grammar()) {
        fprintf(stderr, "Failed to create mock grammar\n");
        return 1;
    }
    
    // Test with the mock grammar
    if (test_with_mock()) {
        printf("\n✅ TEST PASSED: Successfully used the mock JavaScript grammar!\n");
        return 0;
    } else {
        printf("\n❌ TEST FAILED: Could not use the mock JavaScript grammar\n");
        return 1;
    }
}