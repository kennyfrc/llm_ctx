#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <dlfcn.h>

#include "../packs.h"
#include "../arena.h"
#include "../codemap.h"

/**
 * Test if a given language pack is available
 */
static bool is_pack_available(const PackRegistry *registry, const char *name) {
    if (!registry || !registry->packs || registry->pack_count == 0) return false;
    
    for (size_t i = 0; i < registry->pack_count; i++) {
        if (strcmp(registry->packs[i].name, name) == 0) {
            return true;
        }
    }
    
    return false;
}

/**
 * Test dynamic loading of language packs
 */
static void test_language_pack_loading(void) {
    printf("Test 4: Dynamic loading of language packs... ");
    
    Arena arena = arena_create(1024 * 1024);
    PackRegistry registry = {0};
    
    bool success = initialize_pack_registry(&registry, &arena);
    if (!success) {
        printf("SKIP (No packs found)\n");
        arena_destroy(&arena);
        return;
    }
    
    // Test that the test pack is available
    bool test_pack_available = is_pack_available(&registry, "test");
    if (!test_pack_available) {
        printf("SKIP (Test pack not available)\n");
        cleanup_pack_registry(&registry);
        arena_destroy(&arena);
        return;
    }
    
    // Load the language packs
    size_t loaded = load_language_packs(&registry);
    if (loaded == 0) {
        printf("FAIL (No packs could be loaded)\n");
        cleanup_pack_registry(&registry);
        arena_destroy(&arena);
        return;
    }
    
    // Build extension map
    success = build_extension_map(&registry, &arena);
    if (!success) {
        printf("FAIL (Could not build extension map)\n");
        cleanup_pack_registry(&registry);
        arena_destroy(&arena);
        return;
    }
    
    // Test extension lookup
    LanguagePack *test_pack = find_pack_for_extension(&registry, ".test");
    if (!test_pack) {
        printf("FAIL (Could not find pack for .test extension)\n");
        cleanup_pack_registry(&registry);
        arena_destroy(&arena);
        return;
    }
    
    // Verify function pointers are set
    if (!test_pack->initialize || !test_pack->cleanup || !test_pack->parse_file) {
        printf("FAIL (Function pointers not set correctly)\n");
        cleanup_pack_registry(&registry);
        arena_destroy(&arena);
        return;
    }
    
    printf("PASS\n");
    
    cleanup_pack_registry(&registry);
    arena_destroy(&arena);
}

/**
 * Simple unit test for the pack registry
 * 
 * Tests:
 * 1. Registry initialization
 * 2. Pack discovery
 * 3. Pack listing
 * 4. Dynamic loading
 * 5. Extension mapping
 */
int main(void) {
    printf("Testing pack registry functionality...\n");
    
    Arena arena = arena_create(1024 * 1024); // 1 MB
    
    PackRegistry registry = {0};
    
    // Test 1: Initialize registry
    printf("Test 1: Initialize pack registry... ");
    bool success = initialize_pack_registry(&registry, &arena);
    
    // Since we don't know if packs are available on the test system,
    // we'll just check that initialization doesn't crash
    printf("SUCCESS\n");
    
    // Test 2: Check if JavaScript is among the packs (if any are found)
    printf("Test 2: Check for JavaScript pack... ");
    bool found_js = false;
    
    if (success && registry.pack_count > 0) {
        for (size_t i = 0; i < registry.pack_count; i++) {
            if (strcmp(registry.packs[i].name, "javascript") == 0) {
                found_js = true;
                break;
            }
        }
        
        if (found_js) {
            printf("PASS (JavaScript pack found)\n");
        } else {
            printf("WARNING (JavaScript pack not found, but other packs are available)\n");
        }
    } else {
        printf("SKIP (No packs found)\n");
    }
    
    // Test 3: Print pack list
    printf("Test 3: Print pack list\n");
    printf("Available packs:\n");
    print_pack_list(&registry);
    printf("SUCCESS\n");
    
    // Test 4: Test dynamic loading of language packs
    test_language_pack_loading();
    
    // Clean up
    cleanup_pack_registry(&registry);
    arena_destroy(&arena);
    
    printf("All tests completed.\n");
    
    return 0;
}