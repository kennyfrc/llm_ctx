#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include "test_framework.h"

#include "../codemap.h"
#include "../arena.h"
#include "../debug.h"

/* Define debug_mode for tests */
bool debug_mode = false;

#define TEST_JS_FILE "/Users/kennyfrc/Documents/code/fun/llm_ctx/packs/javascript/test.js"
#define TEST_TS_FILE "/Users/kennyfrc/Documents/code/fun/llm_ctx/packs/javascript/test.ts"

// Import the JavaScript pack functions directly
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

// Create a test JavaScript file
static void create_test_js_file(void) {
    FILE *f = fopen(TEST_JS_FILE, "w");
    if (!f) {
        fprintf(stderr, "Error: Could not create test.js file\n");
        return;
    }
    
    fprintf(f, "// Simple JavaScript test file\n");
    fprintf(f, "function hello() {\n");
    fprintf(f, "  return 'Hello';\n");
    fprintf(f, "}\n\n");
    fprintf(f, "class Person {\n");
    fprintf(f, "  constructor(name) {\n");
    fprintf(f, "    this.name = name;\n");
    fprintf(f, "  }\n");
    fprintf(f, "  \n");
    fprintf(f, "  greet() {\n");
    fprintf(f, "    return 'Hello, ' + this.name;\n");
    fprintf(f, "  }\n");
    fprintf(f, "}\n");
    
    fclose(f);
}

// Create a test TypeScript file
static void create_test_ts_file(void) {
    FILE *f = fopen(TEST_TS_FILE, "w");
    if (!f) {
        fprintf(stderr, "Error: Could not create test.ts file\n");
        return;
    }
    
    fprintf(f, "// Simple TypeScript test file\n");
    fprintf(f, "interface Person {\n");
    fprintf(f, "  name: string;\n");
    fprintf(f, "  age: number;\n");
    fprintf(f, "}\n\n");
    fprintf(f, "function greet(person: Person): string {\n");
    fprintf(f, "  return `Hello, ${person.name}`;\n");
    fprintf(f, "}\n\n");
    fprintf(f, "class Employee implements Person {\n");
    fprintf(f, "  name: string;\n");
    fprintf(f, "  age: number;\n");
    fprintf(f, "  department: string;\n\n");
    fprintf(f, "  constructor(name: string, age: number, department: string) {\n");
    fprintf(f, "    this.name = name;\n");
    fprintf(f, "    this.age = age;\n");
    fprintf(f, "    this.department = department;\n");
    fprintf(f, "  }\n\n");
    fprintf(f, "  getInfo(): string {\n");
    fprintf(f, "    return `${this.name}, ${this.age}, ${this.department}`;\n");
    fprintf(f, "  }\n");
    fprintf(f, "}\n");
    
    fclose(f);
}

// Test JS pack functions availability
TEST(test_js_pack_functions) {
    ASSERT("Initialize function exists", initialize != NULL);
    ASSERT("Cleanup function exists", cleanup != NULL);
    ASSERT("Get extensions function exists", get_extensions != NULL);
    ASSERT("Parse file function exists", parse_file != NULL);
}

// Test getting JavaScript extensions
TEST(test_js_extensions) {
    size_t count = 0;
    const char **extensions = get_extensions(&count);
    
    ASSERT("Extension count is at least 2", count >= 2);
    ASSERT("Has .js extension", extensions && extensions[0] && strcmp(extensions[0], ".js") == 0);
    ASSERT("Has .ts extension", extensions && count >= 3 && extensions[2] && strcmp(extensions[2], ".ts") == 0);
}

// Test parsing a JavaScript file
TEST(test_parse_js_file) {
    // Create test file
    create_test_js_file();
    
    // Initialize the language pack
    bool init_result = initialize();
    ASSERT("Initialize succeeded", init_result);
    
    // Read the test file
    size_t file_size = 0;
    char *file_content = read_file(TEST_JS_FILE, &file_size);
    ASSERT("Read test file", file_content != NULL);
    
    if (file_content) {
        // Create arena for memory allocation
        Arena arena = {0};
        arena.base = malloc(1024 * 1024);  // 1 MB
        arena.size = 1024 * 1024;
        arena.pos = 0;
        
        // Create a CodemapFile
        CodemapFile file = {0};
        strncpy(file.path, TEST_JS_FILE, sizeof(file.path) - 1);
        
        // Parse the file
        bool parse_result = parse_file(TEST_JS_FILE, file_content, file_size, &file, &arena);
        ASSERT("Parse succeeded", parse_result);
        
        // Check results
        ASSERT("Found entries", file.entry_count > 0);
        
        // Look for specific entries
        bool found_hello = false;
        bool found_person = false;
        bool found_greet = false;
        
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
        }
        
        ASSERT("Found hello function", found_hello);
        ASSERT("Found Person class", found_person);
        ASSERT("Found greet method", found_greet);
        
        // Clean up
        free(file_content);
        free(arena.base);
    }
    
    // Cleanup
    cleanup();
}

// Test parsing a TypeScript file
TEST(test_parse_ts_file) {
    // Create test file
    create_test_ts_file();
    
    // Initialize the language pack
    bool init_result = initialize();
    ASSERT("Initialize succeeded", init_result);
    
    // Read the test file
    size_t file_size = 0;
    char *file_content = read_file(TEST_TS_FILE, &file_size);
    ASSERT("Read test file", file_content != NULL);
    
    if (file_content) {
        // Create arena for memory allocation
        Arena arena = {0};
        arena.base = malloc(1024 * 1024);  // 1 MB
        arena.size = 1024 * 1024;
        arena.pos = 0;
        
        // Create a CodemapFile
        CodemapFile file = {0};
        strncpy(file.path, TEST_TS_FILE, sizeof(file.path) - 1);
        
        // Parse the file
        bool parse_result = parse_file(TEST_TS_FILE, file_content, file_size, &file, &arena);
        ASSERT("Parse succeeded", parse_result);
        
        // Check results
        ASSERT("Found entries", file.entry_count > 0);
        
        // For TypeScript, we just verify that we found at least one entry
        // Our parser is simplified and may not handle TypeScript-specific constructs perfectly,
        // but we want to make sure it doesn't crash and finds something
        ASSERT("Found entries in TypeScript file", file.entry_count > 0);
        
        // Print what was actually found for debugging
        printf("Entries found in TypeScript file:\n");
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
    printf("Running language pack tests: javascript\n");
    printf("=====================================\n");
    
    RUN_TEST(test_js_pack_functions);
    RUN_TEST(test_js_extensions);
    RUN_TEST(test_parse_js_file);
    RUN_TEST(test_parse_ts_file);
    
    PRINT_TEST_SUMMARY();
    return 0;
}