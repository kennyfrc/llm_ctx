#ifndef PACKS_H
#define PACKS_H

#include <stdbool.h>
#include <stddef.h>
#include "arena.h"
#include "codemap.h"

/* Forward declaration */
struct LanguagePack;
typedef struct LanguagePack LanguagePack;

/* Language Pack structure */
struct LanguagePack {
    char name[64];                /* Language name (e.g. "javascript") */
    char path[4096];              /* Full path to the parser.so */
    bool available;               /* Whether the pack is actually available */
    
    /* Extension handling */
    const char **extensions;      /* File extensions (e.g. {".js", ".jsx", NULL}) */
    size_t extension_count;       /* Number of supported extensions */
    
    /* Dynamic library handle */
    void *handle;                 /* Dynamic library handle (e.g., from dlopen) */
    
    /* Function pointers for language-specific operations */
    bool (*initialize)(void);     /* Initialize the language parser */
    void (*cleanup)(void);        /* Clean up language parser resources */
    bool (*parse_file)(const char *path, const char *source, size_t source_len, 
                       CodemapFile *file, Arena *arena);  /* Parse a file */
};

/* Registry of installed language packs */
typedef struct {
    LanguagePack *packs;          /* Array of language packs */
    size_t pack_count;            /* Number of loaded packs */
    
    /* Extension mapping */
    char **extension_map;         /* Maps extensions to pack indices */
    size_t extension_map_size;    /* Size of extension map */
} PackRegistry;

/* Global pack registry */
extern PackRegistry g_pack_registry;

/**
 * Initialize the pack registry
 * Scans the packs directory for available language packs
 * Returns true if at least one pack was found
 */
bool initialize_pack_registry(PackRegistry *registry, Arena *arena);

/**
 * Load dynamic libraries for all available language packs
 * Resolves function pointers and initializes extensions
 * Returns the number of successfully loaded packs
 */
size_t load_language_packs(PackRegistry *registry);

/**
 * Build the extension map to quickly map file extensions to language packs
 * Returns true if successful
 */
bool build_extension_map(PackRegistry *registry, Arena *arena);

/**
 * Find a language pack for a given file extension
 * Returns NULL if no suitable pack is found
 */
LanguagePack *find_pack_for_extension(PackRegistry *registry, const char *extension);

/**
 * Print the list of available language packs
 */
void print_pack_list(const PackRegistry *registry);

/**
 * Clean up the pack registry resources
 * Unloads all dynamic libraries and frees allocated memory
 */
void cleanup_pack_registry(PackRegistry *registry);

#endif /* PACKS_H */