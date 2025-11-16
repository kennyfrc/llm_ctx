#ifndef GITIGNORE_H
#define GITIGNORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fnmatch.h>
#include <ctype.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

/* WHY: Git's own pattern matching logic - excludes and negations handled */
#define STATIC_ASSERT(cond, msg) typedef char static_assertion_##msg[(cond) ? 1 : -1]
STATIC_ASSERT(sizeof(size_t) >= 4, size_t_at_least_32_bits);

#define MAX_PATH 4096
#define MAX_IGNORE_PATTERNS 1024

typedef struct
{
    char pattern[MAX_PATH];
    bool is_negation;
    bool match_only_dir;
} IgnorePattern;

extern IgnorePattern ignore_patterns[MAX_IGNORE_PATTERNS];
extern int num_ignore_patterns;
extern bool respect_gitignore;

int should_ignore_path(const char* path);
void add_ignore_pattern(char* pattern);
void load_gitignore_file(const char* filepath);
void load_all_gitignore_files(void);
void reset_gitignore_patterns(void);

#endif /* GITIGNORE_H */
