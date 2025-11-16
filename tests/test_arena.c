#define ARENA_IMPLEMENTATION
#include "../arena.h"
#include "test_framework.h"

#include <stdio.h>
#include <string.h>

TEST(test_arena_create_destroy) {
    Arena a = arena_create(MiB(1));
    
    ASSERT("Arena base allocated", a.base != NULL);
    ASSERT("Arena size correct", a.size == MiB(1));
    ASSERT("Arena pos initialized", a.pos == 0);
    
    arena_destroy(&a);
    ASSERT("Arena base freed", a.base == NULL);
    ASSERT("Arena size reset", a.size == 0);
    ASSERT("Arena pos reset", a.pos == 0);
}

TEST(test_arena_allocations) {
    Arena a = arena_create(KiB(4));
    ASSERT("Arena created", a.base != NULL);
    
    int *i = arena_push(&a, int);
    ASSERT("Single int allocated", i != NULL);
    *i = 42;
    ASSERT("Int value set correctly", *i == 42);
    ASSERT("Arena pos advanced", a.pos > 0);
    
    // Allocate an array
    int *arr = arena_push_array(&a, int, 10);
    ASSERT("Array allocated", arr != NULL);
    for (int j = 0; j < 10; j++) {
        arr[j] = j;
    }
    
    // Verify array contents
    for (int j = 0; j < 10; j++) {
        ASSERT_EQUALS(j, arr[j]);
    }
    
    // Test mark/reset
    size_t mark = arena_get_mark(&a);
    int *more_data = arena_push_array(&a, int, 5);
    ASSERT("More data allocated", more_data != NULL);
    for (int j = 0; j < 5; j++) {
        more_data[j] = j + 100;
    }
    
    // Reset to mark
    arena_set_mark(&a, mark);
    ASSERT("Arena position reset to mark", a.pos == mark);
    
    // Test clearing
    arena_clear(&a);
    ASSERT("Arena cleared", a.pos == 0);
    
    arena_destroy(&a);
}

TEST(test_alignment) {
    Arena a = arena_create(KiB(4));
    
    // Allocate a byte
    char *c = arena_push(&a, char);
    ASSERT("Char allocated", c != NULL);
    *c = 'A'; // Use c to avoid warnings
    
    // Now allocate something that requires alignment
    double *d = arena_push(&a, double);
    ASSERT("Double allocated", d != NULL);
    *d = 3.14159; // Use d to avoid warnings
    
    // Verify the pointer is properly aligned
    ASSERT("Double properly aligned", ((uintptr_t)d % _Alignof(double)) == 0);
    
    // Test with manual alignment
    size_t mark = arena_get_mark(&a);
    void *aligned = arena_push_size(&a, 128, 64);
    ASSERT("Aligned allocation succeeded", aligned != NULL);
    ASSERT("Pointer aligned to 64 bytes", ((uintptr_t)aligned % 64) == 0);
    
    // Use mark and aligned to avoid warnings
    (void)mark;
    *(char*)aligned = 'B';
    
    arena_destroy(&a);
}

TEST(test_oom) {
    // Create a small arena to test out-of-memory condition
    Arena a = arena_create(16);
    ASSERT("Small arena created", a.base != NULL);
    
    // This should succeed
    int *i = arena_push(&a, int);
    ASSERT("Small allocation succeeded", i != NULL);
    *i = 123; // Use i to avoid warnings
    
    // This should fail (not enough space)
    void *too_big = arena_push_size(&a, 100, 1);
    ASSERT("Large allocation failed as expected", too_big == NULL);
    (void)too_big; // Use too_big to avoid warnings
    
    arena_destroy(&a);
}

int main(void) {
    printf("Running arena tests\n");
    printf("==================\n");
    
    RUN_TEST(test_arena_create_destroy);
    RUN_TEST(test_arena_allocations);
    RUN_TEST(test_alignment);
    RUN_TEST(test_oom);
    
    printf("\n");
    PRINT_TEST_SUMMARY();
}