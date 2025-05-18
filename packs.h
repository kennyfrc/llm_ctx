#ifndef PACKS_H
#define PACKS_H

#include <stdbool.h>
#include <stddef.h>
#include "arena.h"

/* Language Pack structure */
typedef struct {
    char name[64];                /* Language name (e.g. "javascript") */
    char path[4096];              /* Full path to the parser.so */
    bool available;               /* Whether the pack is actually available */
} LanguagePack;

/* Registry of installed language packs */
typedef struct {
    LanguagePack *packs;          /* Array of language packs */
    size_t pack_count;            /* Number of loaded packs */
} PackRegistry;

/**
 * Initialize the pack registry
 * Scans the packs directory for available language packs
 * Returns true if at least one pack was found
 */
bool initialize_pack_registry(PackRegistry *registry, Arena *arena);

/**
 * Print the list of available language packs
 */
void print_pack_list(const PackRegistry *registry);

/**
 * Clean up the pack registry resources
 */
void cleanup_pack_registry(PackRegistry *registry);

#endif /* PACKS_H */