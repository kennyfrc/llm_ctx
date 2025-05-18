#ifndef CODEMAP_H
#define CODEMAP_H

#include <stddef.h>
#include "arena.h"

#define MAX_PATH 4096

typedef enum { 
    CM_FUNCTION, 
    CM_CLASS, 
    CM_METHOD, 
    CM_TYPE 
} CMKind;

typedef struct {
    char  name[128];          /* Identifier */
    char  signature[256];     /* Params incl. "(...)" */
    char  return_type[64];    /* "void" default for unknown */
    char  container[128];     /* Class name for methods, empty otherwise */
    CMKind kind;
} CodemapEntry;

typedef struct {
    char           path[MAX_PATH];
    CodemapEntry  *entries;    /* arena array */
    size_t         entry_count;
} CodemapFile;

typedef struct {
    CodemapFile *files;        /* arena array, 1:1 with processed JS/TS files */
    size_t       file_count;
} Codemap;

/**
 * Initialize an empty codemap
 */
Codemap codemap_init(Arena *arena);

/**
 * Add a new file to the codemap
 * Returns a pointer to the newly created file entry
 */
CodemapFile *codemap_add_file(Codemap *cm, const char *path, Arena *arena);

/**
 * Add a new entry to a codemap file
 * Returns a pointer to the newly created entry
 */
CodemapEntry *codemap_add_entry(CodemapFile *file, const char *name, const char *signature, 
                              const char *return_type, const char *container, CMKind kind, 
                              Arena *arena);

/**
 * Generate the codemap output and append it to the given output_buffer
 * Returns the updated buffer position
 */
char *codemap_generate(Codemap *cm, char *output_buffer, size_t *buffer_pos, size_t buffer_size);

/**
 * Process JavaScript/TypeScript files and build the codemap
 * Returns true if successful, false otherwise
 */
bool process_js_ts_files(Codemap *cm, const char **processed_files, size_t processed_count, Arena *arena);

#endif /* CODEMAP_H */