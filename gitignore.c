#include "gitignore.h"

/* Global variables for gitignore patterns */
IgnorePattern ignore_patterns[MAX_IGNORE_PATTERNS];
int num_ignore_patterns = 0;
int respect_gitignore = 1;  /* Flag to control whether gitignore is respected */

/**
 * Reset all gitignore patterns (for testing)
 */
void reset_gitignore_patterns(void) {
    num_ignore_patterns = 0;
    respect_gitignore = 1;
}

/**
 * Checks if a path should be ignored based on gitignore patterns
 * 
 * Returns 1 if the path should be ignored, 0 otherwise
 */
int should_ignore_path(const char *path) {
    if (!respect_gitignore || num_ignore_patterns == 0) {
        return 0;  /* Nothing to ignore */
    }
    
    /* Extract just the filename part for matching against patterns like *.txt */
    const char *basename = strrchr(path, '/');
    if (basename) {
        basename++; /* Skip the slash */
    } else {
        basename = path; /* No slash in the path */
    }
    
    /* Get file status to determine if it's a directory */
    struct stat path_stat;
    int is_dir = 0;
    if (lstat(path, &path_stat) == 0) {
        is_dir = S_ISDIR(path_stat.st_mode);
    }
    
    /* Track if this path is matched by a negation pattern */
    int negated = 0;
    
    /* Start with the last pattern and work backwards
     * This gives precedence to later patterns which is the correct behavior */
    for (int i = num_ignore_patterns - 1; i >= 0; i--) {
        /* Skip directory-only patterns if this is not a directory */
        if (ignore_patterns[i].match_only_dir && !is_dir) {
            continue;
        }
        
        /* Match the pattern against the path and the basename */
        int path_match = fnmatch(ignore_patterns[i].pattern, path, FNM_PATHNAME) == 0;
        int basename_match = fnmatch(ignore_patterns[i].pattern, basename, 0) == 0;
        
        if (path_match || basename_match) {
            /* If this is a negation pattern, we don't ignore the file */
            if (ignore_patterns[i].is_negation) {
                return 0;
            }
            
            /* Otherwise, ignore the file unless a later negation pattern matches */
            if (!negated) {
                return 1;
            }
        }
    }
    
    return 0;  /* Not ignored by default */
}

/**
 * Add a pattern to the ignore list
 */
void add_ignore_pattern(char *pattern) {
    if (num_ignore_patterns >= MAX_IGNORE_PATTERNS) {
        return;  /* Ignore list is full */
    }
    
    /* Trim leading and trailing whitespace */
    while (*pattern && isspace(*pattern)) {
        pattern++;
    }
    
    char *end = pattern + strlen(pattern) - 1;
    while (end > pattern && isspace(*end)) {
        *end-- = '\0';
    }
    
    /* Skip empty lines and comment lines */
    if (*pattern == '\0' || *pattern == '#') {
        return;
    }
    
    /* Check if this is a negation pattern */
    int is_negation = 0;
    if (*pattern == '!') {
        is_negation = 1;
        pattern++;  /* Skip the negation character */
    }
    
    /* Check if this pattern matches only directories */
    int match_only_dir = 0;
    size_t len = strlen(pattern);
    if (len > 0 && pattern[len - 1] == '/') {
        match_only_dir = 1;
        pattern[len - 1] = '\0';  /* Remove the trailing slash */
    }
    
    /* Store the pattern */
    strncpy(ignore_patterns[num_ignore_patterns].pattern, pattern, MAX_PATH - 1);
    ignore_patterns[num_ignore_patterns].pattern[MAX_PATH - 1] = '\0';
    ignore_patterns[num_ignore_patterns].is_negation = is_negation;
    ignore_patterns[num_ignore_patterns].match_only_dir = match_only_dir;
    
    num_ignore_patterns++;
}

/**
 * Load patterns from a .gitignore file
 */
void load_gitignore_file(const char *filepath) {
    FILE *file = fopen(filepath, "r");
    if (!file) {
        return;
    }
    
    char line[MAX_PATH];
    while (fgets(line, sizeof(line), file)) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        
        add_ignore_pattern(line);
    }
    
    fclose(file);
}

/**
 * Load all .gitignore files from the current directory and parent directories
 */
void load_all_gitignore_files(void) {
    char current_dir[MAX_PATH];
    char gitignore_path[MAX_PATH];
    
    /* Get the current working directory */
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        return;
    }
    
    /* First, try to load .gitignore from the current directory */
    snprintf(gitignore_path, sizeof(gitignore_path), "%s/.gitignore", current_dir);
    load_gitignore_file(gitignore_path);
    
    /* Then, try to load from parent directories */
    char *dir = current_dir;
    char *last_slash;
    
    while ((last_slash = strrchr(dir, '/')) != NULL) {
        /* Truncate path at the last slash to go up one directory */
        *last_slash = '\0';
        
        /* Try to load .gitignore from this directory */
        snprintf(gitignore_path, sizeof(gitignore_path), "%s/.gitignore", dir);
        load_gitignore_file(gitignore_path);
        
        /* Stop if we've reached the root directory */
        if (last_slash == dir) {
            break;
        }
    }
}