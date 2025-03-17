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

#define MAX_PATH 4096
#define MAX_IGNORE_PATTERNS 1024

/* Structure to hold gitignore patterns */
typedef struct {
    char pattern[MAX_PATH];
    int is_negation;
    int match_only_dir;
} IgnorePattern;

/* Global variables for gitignore patterns */
extern IgnorePattern ignore_patterns[MAX_IGNORE_PATTERNS];
extern int num_ignore_patterns;
extern int respect_gitignore;

/* Function declarations */
int should_ignore_path(const char *path);
void add_ignore_pattern(char *pattern);
void load_gitignore_file(const char *filepath);
void load_all_gitignore_files(void);
void reset_gitignore_patterns(void);

#endif /* GITIGNORE_H */