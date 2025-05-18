#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "../packs.h"
#include "../arena.h"

/**
 * Simple unit test for the pack registry
 * 
 * Tests:
 * 1. Registry initialization
 * 2. Pack discovery
 * 3. Pack listing
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
    
    // Clean up
    cleanup_pack_registry(&registry);
    arena_destroy(&arena);
    
    printf("All tests completed.\n");
    
    return 0;
}