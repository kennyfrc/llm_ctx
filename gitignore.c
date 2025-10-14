#include "gitignore.h"

IgnorePattern ignore_patterns[MAX_IGNORE_PATTERNS];
int num_ignore_patterns = 0;
bool respect_gitignore = true;

// Reset patterns for testing
void reset_gitignore_patterns(void) {
    num_ignore_patterns = 0;
    respect_gitignore = true;
    assert(num_ignore_patterns == 0);
    assert(respect_gitignore == true);
}

// Check if path matches gitignore patterns (later patterns override earlier ones)
int should_ignore_path(const char *path) {
    assert(path != NULL);
    
    if (!respect_gitignore || num_ignore_patterns == 0) {
        return 0;
    }
    
    const char *basename = strrchr(path, '/');
    if (basename) {
        basename++;
        assert(*(basename-1) == '/');
    } else {
        basename = path;
    }
    
    assert(basename != NULL);
    
    struct stat path_stat;
    bool is_dir = false;
    if (lstat(path, &path_stat) == 0) {
        is_dir = S_ISDIR(path_stat.st_mode);
    }
    
    bool negated = false;
    
    for (int i = num_ignore_patterns - 1; i >= 0; i--) {
        if (ignore_patterns[i].match_only_dir && !is_dir) {
            continue;
        }
        
        int path_match = fnmatch(ignore_patterns[i].pattern, path, FNM_PATHNAME) == 0;
        int basename_match = fnmatch(ignore_patterns[i].pattern, basename, 0) == 0;
        
        assert(path_match == (fnmatch(ignore_patterns[i].pattern, path, FNM_PATHNAME) == 0));
        assert(basename_match == (fnmatch(ignore_patterns[i].pattern, basename, 0) == 0));
        
        if (path_match || basename_match) {
            if (ignore_patterns[i].is_negation) {
                return 0;
            }
            
            if (!negated) {
                return 1;
            }
        }
    }
    
    return 0;
}

// Add pattern to ignore list with whitespace trimming and negation support
void add_ignore_pattern(char *pattern) {
    assert(pattern != NULL);
    assert(num_ignore_patterns < MAX_IGNORE_PATTERNS);
    
    if (num_ignore_patterns >= MAX_IGNORE_PATTERNS) {
        return;
    }
    
    while (*pattern && isspace(*pattern)) {
        pattern++;
    }
    
    char *end = pattern + strlen(pattern) - 1;
    while (end > pattern && isspace(*end)) {
        *end-- = '\0';
    }
    
    if (*pattern == '\0' || *pattern == '#') {
        return;
    }
    
    bool is_negation = false;
    if (*pattern == '!') {
        is_negation = true;
        pattern++;
    }
    
    bool match_only_dir = false;
    size_t len = strlen(pattern);
    if (len > 0 && pattern[len - 1] == '/') {
        match_only_dir = true;
        pattern[len - 1] = '\0';
    }
    
    IgnorePattern new_pattern = {
        .pattern = "",
        .is_negation = is_negation,
        .match_only_dir = match_only_dir
    };
    strncpy(new_pattern.pattern, pattern, MAX_PATH - 1);
    new_pattern.pattern[MAX_PATH - 1] = '\0';
    ignore_patterns[num_ignore_patterns] = new_pattern;
    
    assert(num_ignore_patterns < MAX_IGNORE_PATTERNS);
    num_ignore_patterns++;
    
    assert(strcmp(ignore_patterns[num_ignore_patterns-1].pattern, pattern) == 0);
    assert(ignore_patterns[num_ignore_patterns-1].is_negation == is_negation);
    assert(ignore_patterns[num_ignore_patterns-1].match_only_dir == match_only_dir);
}

/**
 * Load patterns from a .gitignore file
 */
void load_gitignore_file(const char *filepath) {
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
    
    (void)fclose(file);
}

/**
 * Load all .gitignore files from the current directory and parent directories
 */
void load_all_gitignore_files(void) {
    char current_dir[MAX_PATH];
    char gitignore_path[MAX_PATH * 2];
    
    assert(num_ignore_patterns < MAX_IGNORE_PATTERNS);
    
    /* Get the current working directory */
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        return;
    }
    
    assert(current_dir[0] != '\0');
    
    /* First, try to load .gitignore from the current directory */
    int written = snprintf(gitignore_path, sizeof(gitignore_path), "%s/.gitignore", current_dir);
    if (written >= 0 && (size_t)written < sizeof(gitignore_path)) {
        load_gitignore_file(gitignore_path);
    }
    
    /* Then, try to load from parent directories */
    char *dir = current_dir;
    char *last_slash;
    
    while ((last_slash = strrchr(dir, '/')) != NULL) {
        /* Truncate path at the last slash to go up one directory */
        *last_slash = '\0';
        
        /* Try to load .gitignore from this directory */
        written = snprintf(gitignore_path, sizeof(gitignore_path), "%s/.gitignore", dir);
        if (written >= 0 && (size_t)written < sizeof(gitignore_path)) {
            load_gitignore_file(gitignore_path);
        }
        
        /* Stop if we've reached the root directory */
        if (last_slash == dir) {
            break;
        }
    }
}
