#include "gitignore.h"

/* Global variables for gitignore patterns */
IgnorePattern ignore_patterns[MAX_IGNORE_PATTERNS];
int num_ignore_patterns = 0;
bool respect_gitignore = true;  /* Flag to control whether gitignore is respected */

/**
 * Reset all gitignore patterns (for testing)
 */
void reset_gitignore_patterns(void) {
    num_ignore_patterns = 0;
    respect_gitignore = true;
    
    /* Post-condition: patterns have been reset */
    assert(num_ignore_patterns == 0);
    assert(respect_gitignore == true);
}

/**
 * Checks if a path should be ignored based on gitignore patterns
 * 
 * Returns 1 if the path should be ignored, 0 otherwise
 */
int should_ignore_path(const char *path) {
    /* Pre-condition: valid path pointer */
    assert(path != NULL);
    
    if (!respect_gitignore || num_ignore_patterns == 0) {
        return 0;  /* Nothing to ignore */
    }
    
    /* Extract just the filename part for matching against patterns like *.txt */
    const char *basename = strrchr(path, '/');
    if (basename) {
        basename++; /* Skip the slash */
        /* Post-condition: basename points after slash */
        assert(*(basename-1) == '/');
    } else {
        basename = path; /* No slash in the path */
    }
    
    /* Invariant: basename is always valid and points to a string */
    assert(basename != NULL);
    
    /* Get file status to determine if it's a directory */
    struct stat path_stat;
    bool is_dir = false;
    if (lstat(path, &path_stat) == 0) {
        is_dir = S_ISDIR(path_stat.st_mode);
    }
    
    /* Track if this path is matched by a negation pattern */
    bool negated = false;
    
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
        
        /* Verify that pattern matching is consistent */
        assert(path_match == (fnmatch(ignore_patterns[i].pattern, path, FNM_PATHNAME) == 0));
        assert(basename_match == (fnmatch(ignore_patterns[i].pattern, basename, 0) == 0));
        
        if (path_match || basename_match) {
            /* If this is a negation pattern, we don't ignore the file */
            if (ignore_patterns[i].is_negation) {
                /* Postcondition: negation patterns override previous matches */
                return 0;
            }
            
            /* Otherwise, ignore the file unless a later negation pattern matches */
            if (!negated) {
                /* Postcondition: path matches non-negated pattern and no negation overrides */
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
    /* Pre-condition: pattern pointer is valid */
    assert(pattern != NULL);
    /* Pre-condition: we have space for more patterns */
    assert(num_ignore_patterns < MAX_IGNORE_PATTERNS);
    
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
    bool is_negation = false;
    if (*pattern == '!') {
        is_negation = true;
        pattern++;  /* Skip the negation character */
    }
    
    /* Check if this pattern matches only directories */
    bool match_only_dir = false;
    size_t len = strlen(pattern);
    if (len > 0 && pattern[len - 1] == '/') {
        match_only_dir = true;
        pattern[len - 1] = '\0';  /* Remove the trailing slash */
    }
    
    /* Store the pattern using designated initializers (C99) */
    IgnorePattern new_pattern = {
        .pattern = "",
        .is_negation = is_negation,
        .match_only_dir = match_only_dir
    };
    strncpy(new_pattern.pattern, pattern, MAX_PATH - 1);
    new_pattern.pattern[MAX_PATH - 1] = '\0';
    ignore_patterns[num_ignore_patterns] = new_pattern;
    
    /* Pre-condition for next call: ensure we don't exceed array bounds */
    assert(num_ignore_patterns < MAX_IGNORE_PATTERNS);
    num_ignore_patterns++;
    
    /* Post-condition: pattern was properly added */
    assert(strcmp(ignore_patterns[num_ignore_patterns-1].pattern, pattern) == 0);
    assert(ignore_patterns[num_ignore_patterns-1].is_negation == is_negation);
    assert(ignore_patterns[num_ignore_patterns-1].match_only_dir == match_only_dir);
}

/**
 * Load patterns from a .gitignore file
 */
void load_gitignore_file(const char *filepath) {
    /* Pre-condition: filepath is valid */
    assert(filepath != NULL);
    
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
    char gitignore_path[MAX_PATH * 2];
    
    /* Pre-condition: ensure we have space for patterns */
    assert(num_ignore_patterns < MAX_IGNORE_PATTERNS);
    
    /* Get the current working directory */
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        return;
    }
    
    /* Invariant: current_dir is now a valid path */
    assert(current_dir[0] != '\0');
    
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
