#define ARENA_IMPLEMENTATION
#include "arena.h"

size_t arena_align_forward(size_t p, size_t a) {
    if (a == 0) a = sizeof(void*); // Use pointer size as default alignment
    return (p + (a - 1)) & ~(a - 1);
}