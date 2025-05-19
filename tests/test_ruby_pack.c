#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include "test_framework.h"

#include "../codemap.h"
#include "../arena.h"

#define TEST_RUBY_FILE "/Users/kennyfrc/Documents/code/fun/llm_ctx/packs/ruby/test.rb"

// Import the Ruby pack functions directly
extern bool initialize(void);
extern void cleanup(void);
extern const char **get_extensions(size_t *count);
extern bool parse_file(const char*, const char*, size_t, CodemapFile*, Arena*);

// Read a file into memory
static char* read_file(const char *filename, size_t *size) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Could not open file '%s'\n", filename);
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *buffer = malloc(*size + 1);
    if (!buffer) {
        fprintf(stderr, "Error: Could not allocate memory for file\n");
        fclose(f);
        return NULL;
    }
    
    if (fread(buffer, 1, *size, f) != *size) {
        fprintf(stderr, "Error: Could not read file\n");
        free(buffer);
        fclose(f);
        return NULL;
    }
    
    buffer[*size] = '\0';
    fclose(f);
    return buffer;
}

// Test Ruby pack functions availability
TEST(test_ruby_pack_functions) {
    ASSERT("Initialize function exists", initialize != NULL);
    ASSERT("Cleanup function exists", cleanup != NULL);
    ASSERT("Get extensions function exists", get_extensions != NULL);
    ASSERT("Parse file function exists", parse_file != NULL);
}

// Test getting Ruby extensions
TEST(test_ruby_extensions) {
    size_t count = 0;
    const char **extensions = get_extensions(&count);
    
    ASSERT("Extension count is at least 1", count >= 1);
    ASSERT("Has .rb extension", extensions && extensions[0] && strcmp(extensions[0], ".rb") == 0);
}

// Test parsing a Ruby file
TEST(test_parse_ruby_file) {
    // Initialize the language pack
    bool init_result = initialize();
    ASSERT("Initialize succeeded", init_result);
    
    // Read the test file
    size_t file_size = 0;
    char *file_content = read_file(TEST_RUBY_FILE, &file_size);
    ASSERT("Read test file", file_content != NULL);
    
    if (file_content) {
        // Create arena for memory allocation
        Arena arena = {0};
        arena.base = malloc(1024 * 1024);  // 1 MB
        arena.size = 1024 * 1024;
        arena.pos = 0;
        
        // Create a CodemapFile
        CodemapFile file = {0};
        strncpy(file.path, TEST_RUBY_FILE, sizeof(file.path) - 1);
        
        // Parse the file
        bool parse_result = parse_file(TEST_RUBY_FILE, file_content, file_size, &file, &arena);
        ASSERT("Parse succeeded", parse_result);
        
        // Check results
        ASSERT("Found entries", file.entry_count > 0);
        
        // Look for specific entries
        bool found_hello = false;
        bool found_person = false;
        bool found_greet = false;
        bool found_greeter_module = false;
        
        for (size_t i = 0; i < file.entry_count; i++) {
            CodemapEntry *entry = &file.entries[i];
            
            if (entry->kind == CM_FUNCTION && strcmp(entry->name, "hello") == 0) {
                found_hello = true;
            }
            else if (entry->kind == CM_CLASS && strcmp(entry->name, "Person") == 0) {
                found_person = true;
            }
            else if (entry->kind == CM_METHOD && strcmp(entry->name, "greet") == 0) {
                found_greet = true;
            }
            else if (entry->kind == CM_TYPE && strcmp(entry->name, "Greeter") == 0) {
                found_greeter_module = true;
            }
        }
        
        ASSERT("Found hello function", found_hello);
        ASSERT("Found Person class", found_person);
        ASSERT("Found greet method", found_greet);
        ASSERT("Found Greeter module", found_greeter_module);
        
        // Print what was actually found for debugging
        printf("Entries found in Ruby file:\n");
        for (size_t i = 0; i < file.entry_count; i++) {
            CodemapEntry *entry = &file.entries[i];
            const char *kind_str = "Unknown";
            
            switch (entry->kind) {
                case CM_FUNCTION: kind_str = "Function"; break;
                case CM_CLASS: kind_str = "Class"; break;
                case CM_METHOD: kind_str = "Method"; break;
                case CM_TYPE: kind_str = "Type"; break;
            }
            
            printf("  %zu. %s: %s%s", i+1, kind_str, entry->name, entry->signature);
            if (entry->kind == CM_METHOD && entry->container[0]) {
                printf(" (in %s)", entry->container);
            }
            printf("\n");
        }
        
        // Clean up
        free(file_content);
        free(arena.base);
    }
    
    // Cleanup
    cleanup();
}

int main(void) {
    printf("Running Ruby language pack tests\n");
    printf("===================================\n");
    
    RUN_TEST(test_ruby_pack_functions);
    RUN_TEST(test_ruby_extensions);
    RUN_TEST(test_parse_ruby_file);
    
    PRINT_TEST_SUMMARY();
    return 0;
}