
#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>
#include <string.h>
#include <stdint.h>

// Custom max_align_t only if system one is not available
#if !defined(__STDC_VERSION_STDDEF_H__) && !defined(_MSC_VER) && !defined(_MAX_ALIGN_T_DEFINED) && !defined(__APPLE__)
typedef union { long long i; long double d; void *p; } max_align_t;
#define _MAX_ALIGN_T_DEFINED
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ARENA_API
#define ARENA_API extern
#endif

typedef struct Arena {
    unsigned char *base;
    size_t pos;
    size_t size;
#ifdef ARENA_ENABLE_COMMIT
    size_t commit;
#endif
} Arena;

#define KiB(x) ((size_t)(x) << 10)
#define MiB(x) ((size_t)(x) << 20)
#define GiB(x) ((size_t)(x) << 30)

ARENA_API size_t arena_align_forward(size_t p, size_t a);

ARENA_API void arena_clear(Arena *a);
ARENA_API void arena_destroy(Arena *a);
ARENA_API Arena arena_create(size_t reserve);
ARENA_API void *arena_push_size(Arena *a, size_t size, size_t align);
ARENA_API size_t arena_get_mark(Arena *a);
ARENA_API void arena_set_mark(Arena *a, size_t mark);

#define arena_push(arena,T) ((T*)arena_push_size((arena),sizeof(T),__alignof__(T)))
#define arena_push_array(arena,T,count) ((T*)arena_push_size((arena),sizeof(T)*(count),__alignof__(T)))

#ifdef ARENA_IMPLEMENTATION

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <stdlib.h>
#include <assert.h>

ARENA_API Arena arena_create(size_t reserve) {
    Arena a = {0};
#ifdef _WIN32
    a.base = VirtualAlloc(NULL, reserve, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!a.base) return a;
    a.size = reserve;
#ifdef ARENA_ENABLE_COMMIT
    a.commit = reserve;
#endif
#else
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    void *ptr = mmap(NULL, reserve, prot, flags, -1, 0);
    if (ptr == MAP_FAILED) {
        ptr = malloc(reserve);
        if (!ptr) return a;
        a.base = ptr;
        a.size = reserve;
#ifdef ARENA_ENABLE_COMMIT
        a.commit = reserve;
#endif
    } else {
        a.base = ptr;
        a.size = reserve;
#ifdef ARENA_ENABLE_COMMIT
        a.commit = reserve;
#endif
    }
#endif
    return a;
}

ARENA_API void arena_destroy(Arena *a) {
    if (!a || !a->base) return;
#ifdef _WIN32
    VirtualFree(a->base, 0, MEM_RELEASE);
#else
    munmap(a->base, a->size);
#endif
    a->base = NULL;
    a->pos = 0;
    a->size = 0;
#ifdef ARENA_ENABLE_COMMIT
    a->commit = 0;
#endif
}

ARENA_API void arena_clear(Arena *a) {
    if (!a) return;
    a->pos = 0;
#ifdef ARENA_ENABLE_COMMIT
    // no commit tracking for now; assume fully committed
#endif
}

ARENA_API void *arena_push_size(Arena *a, size_t size, size_t align) {
    if (!a || size == 0) return NULL;
    size_t p = arena_align_forward(a->pos, align);
    size_t new_pos = p + size;
    if (new_pos > a->size) return NULL;
    void *ptr = a->base + p;
    memset(ptr, 0, size);
    a->pos = new_pos;
    return ptr;
}

ARENA_API size_t arena_get_mark(Arena *a) { return a ? a->pos : 0; }
ARENA_API void arena_set_mark(Arena *a, size_t mark) { if (a) a->pos = mark; }

#endif /* ARENA_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* ARENA_H */
