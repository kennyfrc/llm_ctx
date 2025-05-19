#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Import codemap structure and arena
typedef enum { 
    CM_FUNCTION = 0, 
    CM_CLASS = 1, 
    CM_METHOD = 2, 
    CM_TYPE = 3
} CMKind;

typedef struct {
    char  name[128];          /* Identifier */
    char  signature[256];     /* Params incl. "(...)" */
    char  return_type[64];    /* "void" default for unknown */
    char  container[128];     /* Class name for methods, empty otherwise */
    CMKind kind;
} CodemapEntry;

typedef struct {
    char           path[4096];
    CodemapEntry  *entries;    /* arena array */
    size_t         entry_count;
} CodemapFile;

typedef struct Arena {
    unsigned char *base;
    size_t pos;
    size_t size;
} Arena;

// Import JS pack functions
extern bool initialize(void);
extern void cleanup(void);
extern const char **get_extensions(size_t *count);
extern bool parse_file(const char*, const char*, size_t, CodemapFile*, Arena*);

// Test sample code
const char *JS_TEST_CODE = 
"// Simple JavaScript test file\n"
"function hello() {\n"
"  return 'Hello';\n"
"}\n\n"
"class Person {\n"
"  constructor(name) {\n"
"    this.name = name;\n"
"  }\n"
"  \n"
"  greet() {\n"
"    return 'Hello, ' + this.name;\n"
"  }\n"
"}\n";

const char *TS_TEST_CODE = 
"// Simple TypeScript test file\n"
"interface Person {\n"
"  name: string;\n"
"  age: number;\n"
"}\n\n"
"function greet(person: Person): string {\n"
"  return `Hello, ${person.name}`;\n"
"}\n\n"
"class Employee implements Person {\n"
"  name: string;\n"
"  age: number;\n"
"  department: string;\n\n"
"  constructor(name: string, age: number, department: string) {\n"
"    this.name = name;\n"
"    this.age = age;\n"
"    this.department = department;\n"
"  }\n\n"
"  getInfo(): string {\n"
"    return `${this.name}, ${this.age}, ${this.department}`;\n"
"  }\n"
"}\n";

// Test function
bool test_js_pack(void) {
    bool success = true;
    
    printf("Testing JavaScript language pack...\n");
    
    // Initialize the pack
    if (!initialize()) {
        printf("ERROR: Failed to initialize JavaScript pack\n");
        return false;
    }
    
    // Test extensions
    size_t count = 0;
    const char **extensions = get_extensions(&count);
    printf("Supported extensions (%zu): ", count);
    for (size_t i = 0; i < count; i++) {
        printf("%s ", extensions[i]);
    }
    printf("\n");
    
    // Create arena
    Arena arena = {0};
    arena.base = malloc(1024 * 1024); // 1MB arena
    arena.size = 1024 * 1024;
    arena.pos = 0;
    
    if (!arena.base) {
        printf("ERROR: Failed to allocate arena\n");
        return false;
    }
    
    // Test JavaScript parsing
    printf("\nTesting JavaScript parsing...\n");
    CodemapFile js_file = {0};
    strcpy(js_file.path, "test.js");
    
    if (!parse_file(js_file.path, JS_TEST_CODE, strlen(JS_TEST_CODE), &js_file, &arena)) {
        printf("ERROR: Failed to parse JavaScript code\n");
        success = false;
    } else {
        printf("Found %zu entries in JavaScript code\n", js_file.entry_count);
        for (size_t i = 0; i < js_file.entry_count; i++) {
            CodemapEntry *entry = &js_file.entries[i];
            const char *kind_str = "";
            
            switch (entry->kind) {
                case CM_FUNCTION: kind_str = "Function"; break;
                case CM_CLASS: kind_str = "Class"; break;
                case CM_METHOD: kind_str = "Method"; break;
                case CM_TYPE: kind_str = "Type"; break;
            }
            
            printf("  %zu. %s: %s%s", i+1, kind_str, entry->name, entry->signature);
            if (entry->container[0]) {
                printf(" (in %s)", entry->container);
            }
            printf("\n");
        }
    }
    
    // Test TypeScript parsing
    printf("\nTesting TypeScript parsing...\n");
    CodemapFile ts_file = {0};
    strcpy(ts_file.path, "test.ts");
    
    // Reset arena position for fresh allocation
    arena.pos = 0;
    
    if (!parse_file(ts_file.path, TS_TEST_CODE, strlen(TS_TEST_CODE), &ts_file, &arena)) {
        printf("ERROR: Failed to parse TypeScript code\n");
        success = false;
    } else {
        printf("Found %zu entries in TypeScript code\n", ts_file.entry_count);
        for (size_t i = 0; i < ts_file.entry_count; i++) {
            CodemapEntry *entry = &ts_file.entries[i];
            const char *kind_str = "";
            
            switch (entry->kind) {
                case CM_FUNCTION: kind_str = "Function"; break;
                case CM_CLASS: kind_str = "Class"; break;
                case CM_METHOD: kind_str = "Method"; break;
                case CM_TYPE: kind_str = "Type"; break;
            }
            
            printf("  %zu. %s: %s%s", i+1, kind_str, entry->name, entry->signature);
            if (entry->container[0]) {
                printf(" (in %s)", entry->container);
            }
            printf("\n");
        }
    }
    
    // Cleanup
    free(arena.base);
    cleanup();
    
    return success;
}

int main(void) {
    if (test_js_pack()) {
        printf("\nAll tests passed!\n");
        return 0;
    } else {
        printf("\nSome tests failed!\n");
        return 1;
    }
}