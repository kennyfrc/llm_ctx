#define ARENA_IMPLEMENTATION
#include "../arena.h"
#include "test_framework.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

static void test_arena_create_destroy() {
    Arena a = arena_create(MiB(1));
    
    // Check if arena was created successfully
    assert(a.base != NULL);
    assert(a.size == MiB(1));
    assert(a.pos == 0);
    
    arena_destroy(&a);
    assert(a.base == NULL);
    assert(a.size == 0);
    assert(a.pos == 0);
    
    printf("create/destroy test passed\n");
}

static void test_arena_allocations() {
    Arena a = arena_create(KiB(4));
    assert(a.base != NULL);
    
    // Allocate a single int
    int *i = arena_push(&a, int);
    assert(i != NULL);
    *i = 42;
    assert(*i == 42);
    assert(a.pos > 0);
    
    // Allocate an array
    int *arr = arena_push_array(&a, int, 10);
    assert(arr != NULL);
    for (int j = 0; j < 10; j++) {
        arr[j] = j;
    }
    
    // Verify array contents
    for (int j = 0; j < 10; j++) {
        assert(arr[j] == j);
    }
    
    // Test mark/reset
    size_t mark = arena_get_mark(&a);
    int *more_data = arena_push_array(&a, int, 5);
    assert(more_data != NULL);
    for (int j = 0; j < 5; j++) {
        more_data[j] = j + 100;
    }
    
    // Reset to mark
    arena_set_mark(&a, mark);
    assert(a.pos == mark);
    
    // Test clearing
    arena_clear(&a);
    assert(a.pos == 0);
    
    arena_destroy(&a);
    printf("allocation test passed\n");
}

static void test_alignment() {
    Arena a = arena_create(KiB(4));
    
    // Allocate a byte
    char *c = arena_push(&a, char);
    assert(c != NULL);
    *c = 'A'; // Use c to avoid warnings
    
    // Now allocate something that requires alignment
    double *d = arena_push(&a, double);
    assert(d != NULL);
    *d = 3.14159; // Use d to avoid warnings
    
    // Verify the pointer is properly aligned
    assert(((uintptr_t)d % _Alignof(double)) == 0);
    
    // Test with manual alignment
    size_t mark = arena_get_mark(&a);
    void *aligned = arena_push_size(&a, 128, 64);
    assert(aligned != NULL);
    assert(((uintptr_t)aligned % 64) == 0);
    
    // Use mark and aligned to avoid warnings
    (void)mark;
    *(char*)aligned = 'B';
    
    arena_destroy(&a);
    printf("alignment test passed\n");
}

static void test_oom() {
    // Create a small arena to test out-of-memory condition
    Arena a = arena_create(16);
    assert(a.base != NULL);
    
    // This should succeed
    int *i = arena_push(&a, int);
    assert(i != NULL);
    *i = 123; // Use i to avoid warnings
    
    // This should fail (not enough space)
    void *too_big = arena_push_size(&a, 100, 1);
    assert(too_big == NULL);
    (void)too_big; // Use too_big to avoid warnings
    
    arena_destroy(&a);
    printf("out-of-memory test passed\n");
}

int main(void) {
    printf("Running arena tests...\n");
    
    test_arena_create_destroy();
    test_arena_allocations();
    test_alignment();
    test_oom();
    
    printf("All arena tests passed!\n");
    return 0;
}