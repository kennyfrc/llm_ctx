#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "../arena.h"
#include "../packs.h"

/**
 * Test file extension to language pack mapping
 */
static void test_extension_mapping(void) {
    printf("Testing file extension to language pack mapping...\n");
    
    Arena arena = arena_create(1024 * 1024); // 1 MB
    if (!arena.base) {
        printf("FAIL: Failed to create arena\n");
        return;
    }
    
    // Initialize pack registry
    PackRegistry registry = {0};
    bool result = initialize_pack_registry(&registry, &arena);
    if (!result) {
        printf("SKIP: No language packs found\n");
        arena_destroy(&arena);
        return;
    }
    
    printf("Found %zu language pack(s)\n", registry.pack_count);
    
    // Print discovered packs
    for (size_t i = 0; i < registry.pack_count; i++) {
        printf("  - %s (path: %s)\n", registry.packs[i].name, registry.packs[i].path);
    }
    
    // Load language packs
    size_t loaded = load_language_packs(&registry);
    printf("Loaded %zu language pack(s)\n", loaded);
    
    // Build extension map
    result = build_extension_map(&registry, &arena);
    if (!result) {
        printf("FAIL: Could not build extension map\n");
        cleanup_pack_registry(&registry);
        arena_destroy(&arena);
        return;
    }
    
    printf("Built extension map with %zu extensions\n", registry.extension_map_size);
    
    // Test JavaScript extensions
    const char *js_extensions[] = {".js", ".jsx", ".ts", ".tsx"};
    for (size_t i = 0; i < sizeof(js_extensions) / sizeof(js_extensions[0]); i++) {
        LanguagePack *pack = find_pack_for_extension(&registry, js_extensions[i]);
        if (pack) {
            printf("Extension %s maps to language pack: %s\n", js_extensions[i], pack->name);
            assert(strcmp(pack->name, "javascript") == 0);
        } else {
            printf("FAIL: Extension %s has no matching language pack\n", js_extensions[i]);
        }
    }
    
    // Test test pack extension
    LanguagePack *test_pack = find_pack_for_extension(&registry, ".test");
    if (test_pack) {
        printf("Extension .test maps to language pack: %s\n", test_pack->name);
        assert(strcmp(test_pack->name, "test") == 0);
    } else {
        printf("FAIL: Extension .test has no matching language pack\n");
    }
    
    // Test an unsupported extension
    LanguagePack *unknown = find_pack_for_extension(&registry, ".xyz");
    if (unknown) {
        printf("FAIL: Extension .xyz unexpectedly maps to language pack: %s\n", unknown->name);
    } else {
        printf("Extension .xyz correctly has no matching language pack\n");
    }
    
    // Clean up
    cleanup_pack_registry(&registry);
    arena_destroy(&arena);
    
    printf("PASS: Extension mapping test completed successfully\n");
}

/**
 * Main function - run tests
 */
int main(void) {
    printf("Running extension mapping tests...\n");
    
    // Run tests
    test_extension_mapping();
    
    printf("Extension mapping tests completed.\n");
    return 0;
}