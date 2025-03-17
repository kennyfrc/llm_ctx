/**
 * llm_ctx - A utility for extracting file content with fenced blocks for LLM context
 * 
 * This utility allows developers to quickly extract contents from files
 * and format them with appropriate tags and fenced code blocks.
 * The output is designed for LLM interfaces for code analysis.
 * 
 * Follows Unix philosophy of small, focused tools that can be composed
 * with other programs using pipes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glob.h>
#include <libgen.h>
#include <errno.h>
#include <fnmatch.h>  /* For pattern matching */
#include <ctype.h>    /* For isspace() */
#include "gitignore.h"

#define MAX_PATH 4096
#define TEMP_FILE_TEMPLATE "/tmp/llm_ctx_XXXXXX"
#define MAX_PATTERNS 64   /* Maximum number of patterns to support */
#define MAX_FILES 1024    /* Maximum number of files to process */

/* Structure to hold file information for the tree */
typedef struct {
    char path[MAX_PATH];
    char *relative_path;
    int is_dir;
} FileInfo;

/* Function declarations */
void show_help(void);
int collect_file(const char *filepath);
int output_file_content(const char *filepath, FILE *output);
void add_user_instructions(const char *instructions);
void find_recursive(const char *base_dir, const char *pattern);
int process_pattern(const char *pattern);
void generate_file_tree(void);
void add_to_file_tree(const char *filepath);
int compare_file_paths(const void *a, const void *b);
char *find_common_prefix(void);
void print_tree_node(const char *path, int level, int is_last, const char *prefix);
void build_tree_recursive(char **paths, int count, int level, char *prefix, const char *path_prefix);
int process_stdin(void);

/* Global variables - used to track state across functions */
char temp_file_path[MAX_PATH];
FILE *temp_file = NULL;
int files_found = 0;
char *processed_files[MAX_FILES]; /* Track processed files to avoid duplicates */
int num_processed_files = 0;
FileInfo file_tree[MAX_FILES];
int file_tree_count = 0;
FILE *tree_file = NULL;        /* For the file tree output */
char tree_file_path[MAX_PATH]; /* Path to the tree file */

/**
 * Check if a file has already been processed to avoid duplicates
 * 
 * Returns 1 if the file has been processed, 0 otherwise
 */
int file_already_processed(const char *filepath) {
    for (int i = 0; i < num_processed_files; i++) {
        if (strcmp(processed_files[i], filepath) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * Add a file to the processed files list
 */
void add_to_processed_files(const char *filepath) {
    if (num_processed_files < MAX_FILES) {
        processed_files[num_processed_files] = strdup(filepath);
        num_processed_files++;
        
        /* Also add to the file tree structure */
        add_to_file_tree(filepath);
    }
}

/**
 * Add a file to the file tree structure
 */
void add_to_file_tree(const char *filepath) {
    if (file_tree_count < MAX_FILES) {
        struct stat statbuf;
        
        /* Get file information */
        if (lstat(filepath, &statbuf) == 0) {
            /* Store file path */
            strncpy(file_tree[file_tree_count].path, filepath, MAX_PATH - 1);
            file_tree[file_tree_count].path[MAX_PATH - 1] = '\0';
            
            /* Determine if it's a directory */
            file_tree[file_tree_count].is_dir = S_ISDIR(statbuf.st_mode);
            
            /* Initially set relative_path to NULL - will be set after finding common prefix */
            file_tree[file_tree_count].relative_path = NULL;
            
            file_tree_count++;
        }
    }
}

/**
 * Compare function for sorting file paths
 */
int compare_file_paths(const void *a, const void *b) {
    const FileInfo *file_a = (const FileInfo *)a;
    const FileInfo *file_b = (const FileInfo *)b;
    return strcmp(file_a->path, file_b->path);
}

/**
 * Find the common prefix of all files in the tree
 * Returns a pointer to the common prefix string
 */
char *find_common_prefix(void) {
    if (file_tree_count == 0) {
        return strdup(".");
    }
    
    /* Start with the first file's directory */
    char *prefix = strdup(file_tree[0].path);
    char *last_slash = NULL;
    
    /* Find directory part */
    last_slash = strrchr(prefix, '/');
    if (last_slash != NULL) {
        *last_slash = '\0';
    } else {
        /* If no slash, use current directory */
        free(prefix);
        return strdup(".");
    }
    
    /* Check each file to find common prefix */
    for (int i = 1; i < file_tree_count; i++) {
        char *path = file_tree[i].path;
        int j;
        
        /* Find common prefix */
        for (j = 0; prefix[j] && path[j]; j++) {
            if (prefix[j] != path[j]) {
                break;
            }
        }
        
        /* Truncate to common portion */
        prefix[j] = '\0';
        
        /* Find last directory separator */
        last_slash = strrchr(prefix, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
        } else {
            /* If no common directory, use current directory */
            free(prefix);
            return strdup(".");
        }
    }
    
    /* If prefix ends up empty, use current directory */
    if (prefix[0] == '\0') {
        free(prefix);
        return strdup(".");
    }
    
    return prefix;
}

/**
 * Print a tree node with appropriate indentation
 */
void print_tree_node(const char *path, int level, int is_last, const char *prefix) {
    /* Print indentation */
    fprintf(tree_file, "%s", prefix);
    
    /* Print node connector */
    if (level > 0) {
        fprintf(tree_file, is_last ? "└── " : "├── ");
    }
    
    /* Print the filename part (after the last slash) */
    const char *filename = strrchr(path, '/');
    if (filename) {
        fprintf(tree_file, "%s\n", filename + 1);
    } else {
        fprintf(tree_file, "%s\n", path);
    }
}

/**
 * Helper function to build directory structure recursively
 */
void build_tree_recursive(char **paths, int count, int level, char *prefix, const char *path_prefix __attribute__((unused))) {
    if (count <= 0) return;
    
    char current_dir[MAX_PATH] = "";
    int i, j;
    
    /* Find current directory for this level */
    for (i = 0; i < count; i++) {
        char *path = paths[i];
        char *slash = strchr(path, '/');
        
        if (slash) {
            int dir_len = slash - path;
            if (current_dir[0] == '\0') {
                strncpy(current_dir, path, dir_len);
                current_dir[dir_len] = '\0';
            }
        }
    }
    
    /* First print files at current level */
    for (i = 0; i < count; i++) {
        if (strchr(paths[i], '/') == NULL) {
            fprintf(tree_file, "%s%s%s\n", 
                   prefix, 
                   (level > 0) ? "├── " : "", 
                   paths[i]);
        }
    }
    
    /* Then process subdirectories */
    for (i = 0; i < count;) {
        char *slash = strchr(paths[i], '/');
        if (!slash) {
            i++; /* Skip files, already processed */
            continue;
        }
        
        int dir_len = slash - paths[i];
        char dirname[MAX_PATH];
        strncpy(dirname, paths[i], dir_len);
        dirname[dir_len] = '\0';
        
        /* Count how many entries are in this directory */
        int subdir_count = 0;
        for (j = i; j < count; j++) {
            if (strncmp(paths[j], dirname, dir_len) == 0 && paths[j][dir_len] == '/') {
                subdir_count++;
            } else if (j > i) {
                break; /* No more entries in this directory */
            }
        }
        
        if (subdir_count > 0) {
            /* Print directory entry */
            fprintf(tree_file, "%s%s%s\n", 
                   prefix, 
                   (level > 0) ? "├── " : "", 
                   dirname);
            
            /* Create new prefix for subdirectory items */
            char new_prefix[MAX_PATH];
            sprintf(new_prefix, "%s%s", prefix, (level > 0) ? "│   " : "");
            
            /* Create path array for subdirectory items */
            char *subdirs[MAX_FILES];
            int subdir_idx = 0;
            
            /* Extract paths relative to this subdirectory */
            for (j = i; j < i + subdir_count; j++) {
                subdirs[subdir_idx++] = paths[j] + dir_len + 1; /* Skip dirname and slash */
            }
            
            /* Process subdirectory */
            build_tree_recursive(subdirs, subdir_count, level + 1, new_prefix, dirname);
            
            i += subdir_count;
        } else {
            i++; /* Move to next entry */
        }
    }
}

/**
 * Generate and add a file tree to the output
 */
void generate_file_tree(void) {
    if (file_tree_count == 0) {
        return;
    }
    
    /* Create a temporary file for the tree */
    strcpy(tree_file_path, "/tmp/llm_ctx_tree_XXXXXX");
    int fd = mkstemp(tree_file_path);
    if (fd == -1) {
        return;
    }
    
    tree_file = fdopen(fd, "w");
    if (!tree_file) {
        close(fd);
        return;
    }
    
    /* Sort files for easier tree generation */
    qsort(file_tree, file_tree_count, sizeof(FileInfo), compare_file_paths);
    
    /* Find common prefix */
    char *common_prefix = find_common_prefix();
    int prefix_len = strlen(common_prefix);
    
    /* Set relative paths and collect non-directory paths */
    char *paths[MAX_FILES];
    int path_count = 0;
    
    for (int i = 0; i < file_tree_count; i++) {
        const char *path = file_tree[i].path;
        
        /* Skip directories, we only want files */
        if (file_tree[i].is_dir) {
            continue;
        }
        
        /* If path starts with common prefix, skip it for relative path */
        if (strncmp(path, common_prefix, prefix_len) == 0) {
            if (path[prefix_len] == '/') {
                file_tree[i].relative_path = strdup(path + prefix_len + 1);
            } else {
                file_tree[i].relative_path = strdup(path + prefix_len);
            }
        } else {
            file_tree[i].relative_path = strdup(path);
        }
        
        /* Add to paths array for tree building */
        if (file_tree[i].relative_path && path_count < MAX_FILES) {
            paths[path_count++] = file_tree[i].relative_path;
        }
    }
    
    /* Print the root directory */
    fprintf(tree_file, "%s\n", common_prefix);
    
    /* Build the tree recursively */
    build_tree_recursive(paths, path_count, 0, "", common_prefix);
    
    /* Free common prefix memory */
    free(common_prefix);
    
    /* Close tree file */
    fclose(tree_file);
    
    /* Add file tree to the main output */
    fprintf(temp_file, "<file_tree>\n");
    
    /* Copy tree file content */
    FILE *f = fopen(tree_file_path, "r");
    if (f) {
        char buffer[4096];
        size_t bytes_read;
        
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
            fwrite(buffer, 1, bytes_read, temp_file);
        }
        
        fclose(f);
    }
    
    fprintf(temp_file, "</file_tree>\n\n");
    
    /* Remove temporary tree file */
    unlink(tree_file_path);
    
    /* Free allocated memory */
    for (int i = 0; i < file_tree_count; i++) {
        free(file_tree[i].relative_path);
        file_tree[i].relative_path = NULL;
    }
}

/**
 * Display help message with usage instructions and examples
 */
void show_help(void) {
    printf("Usage: llm_ctx [OPTIONS] [FILE...]\n");
    printf("Format files for LLM code analysis with appropriate tags.\n\n");
    printf("Options:\n");
    printf("  -c TEXT        Add user instructions wrapped in <user_instructions> tags\n");
    printf("  -h             Show this help message\n");
    printf("  --no-gitignore Ignore .gitignore files when collecting files\n\n");
    printf("If no FILE arguments are given, read from standard input.\n");
    printf("This allows piping from commands like find, ls, etc.\n\n");
    printf("Examples:\n");
    printf("  # Pass files directly\n");
    printf("  llm_ctx src/main.c include/header.h\n\n");
    printf("  # Use with find to process files\n");
    printf("  find src -name \"*.c\" | llm_ctx\n\n");
    printf("  # Add instructions for the LLM\n");
    printf("  llm_ctx -c \"Please explain this code\" src/*.c\n\n");
    printf("  # Pipe to clipboard\n");
    printf("  llm_ctx src/*.c | pbcopy\n");
    exit(0);
}

/**
 * Process lines from stdin
 * Each line is treated as a filename to process
 * 
 * Returns 0 if files were processed, 1 otherwise
 */
int process_stdin(void) {
    char line[MAX_PATH];
    int initial_files_found = files_found;
    
    while (fgets(line, sizeof(line), stdin)) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        
        /* Skip empty lines */
        if (strlen(line) == 0) {
            continue;
        }
        
        /* Process the file */
        collect_file(line);
    }
    
    return (files_found > initial_files_found) ? 0 : 1;
}

/**
 * Collect a file to be processed but don't output its content yet
 * 
 * Only adds the file to our tracking lists if it hasn't been processed yet
 * 
 * Returns 0 on success, 1 on failure
 */
int collect_file(const char *filepath) {
    /* Check if we've already processed this file to avoid duplicates */
    if (file_already_processed(filepath)) {
        return 0;
    }

    /* Check if the file should be ignored based on gitignore patterns */
    if (should_ignore_path(filepath)) {
        return 0;  /* Silently skip ignored files */
    }
    
    /* Check if file exists and is readable before adding it */
    if (access(filepath, R_OK) != 0) {
        return 1;
    }
    
    struct stat statbuf;
    if (lstat(filepath, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
        /* This is a directory, so we need to add it and then process its contents */
        add_to_processed_files(filepath);
        files_found++;
        
        /* Process all files in the directory */
        DIR *dir = opendir(filepath);
        if (dir) {
            struct dirent *entry;
            char path[MAX_PATH];
            
            while ((entry = readdir(dir)) != NULL) {
                /* Skip the special directory entries */
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                    continue;
                
                /* Construct the full path */
                snprintf(path, sizeof(path), "%s/%s", filepath, entry->d_name);
                
                /* Recursively collect this file/directory */
                collect_file(path);
            }
            closedir(dir);
        }
    } else {
        /* Regular file */
        add_to_processed_files(filepath);
        files_found++;
    }
    
    return 0;
}

/**
 * Output a file's content to the specified output file
 * 
 * Reads the file content and formats it with fenced code blocks
 * and file headers for better LLM understanding
 * 
 * Returns 0 on success, 1 on failure
 */
int output_file_content(const char *filepath, FILE *output) {
    /* Check if path is a directory - skip it if so */
    struct stat statbuf;
    if (lstat(filepath, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
        return 0; /* Skip directories silently */
    }

    /* Check if file exists and is readable before attempting to open */
    if (access(filepath, R_OK) != 0) {
        return 1;
    }

    FILE *file = fopen(filepath, "r");
    if (!file) {
        return 1;
    }

    /* Format with a header showing the filename for context */
    fprintf(output, "File: %s\n", filepath);
    fprintf(output, "```\n");

    /* Read and write the file contents in chunks for memory efficiency */
    char buffer[4096];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        fwrite(buffer, 1, bytes_read, output);
    }

    /* Close the code fence and add a separator for visual clarity */
    fprintf(output, "```\n");
    fprintf(output, "----------------------------------------\n");

    fclose(file);
    return 0;
}

/**
 * Add user instructions to the output if provided
 */
void add_user_instructions(const char *instructions) {
    if (!instructions || !instructions[0]) {
        return;
    }

    fprintf(temp_file, "<user_instructions>\n");
    fprintf(temp_file, "%s\n", instructions);
    fprintf(temp_file, "</user_instructions>\n\n");
}

/**
 * Recursively search directories for files matching a pattern
 */
void find_recursive(const char *base_dir, const char *pattern) {
    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;
    char path[MAX_PATH];
    
    /* Try to open directory - silently return if can't access */
    if (!(dir = opendir(base_dir)))
        return;
    
    /* Process each entry in the directory */
    while ((entry = readdir(dir)) != NULL) {
        /* Skip the special directory entries */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        
        /* Construct the full path of the current entry */
        snprintf(path, sizeof(path), "%s/%s", base_dir, entry->d_name);
        
        /* Get entry information - skip if can't stat */
        if (lstat(path, &statbuf) == -1)
            continue;
        
        /* If entry is a directory, recurse into it */
        if (S_ISDIR(statbuf.st_mode)) {
            find_recursive(path, pattern);
        } 
        /* If entry is a regular file, check if it matches the pattern */
        else if (S_ISREG(statbuf.st_mode)) {
            if (fnmatch(pattern, entry->d_name, 0) == 0) {
                collect_file(path);
            }
        }
    }
    
    closedir(dir);
}

/**
 * Process a glob pattern to find matching files
 */
int process_pattern(const char *pattern) {
    int initial_files_found = files_found;
    
    /* Check if this is a recursive pattern */
    if (strstr(pattern, "**/") != NULL || strstr(pattern, "**") != NULL) {
        char base_dir[MAX_PATH] = ".";
        char file_pattern[MAX_PATH] = "";
        
        /* Extract base directory and file pattern */
        const char *recursive_marker = strstr(pattern, "**");
        if (recursive_marker) {
            /* Copy base directory (everything before **) */
            size_t base_len = recursive_marker - pattern;
            if (base_len > 0) {
                strncpy(base_dir, pattern, base_len);
                base_dir[base_len] = '\0';
                
                /* Remove trailing slash if present */
                if (base_len > 0 && base_dir[base_len - 1] == '/') {
                    base_dir[base_len - 1] = '\0';
                }
            }
            
            const char *pattern_start = recursive_marker + 2;
            if (*pattern_start == '/') {
                pattern_start++;
            }
            strcpy(file_pattern, pattern_start);
        }
        
        /* Set defaults for empty values */
        if (strlen(base_dir) == 0) {
            strcpy(base_dir, ".");
        }
        
        if (strlen(file_pattern) == 0) {
            strcpy(file_pattern, "*");
        }
        
        /* Use custom recursive directory traversal */
        find_recursive(base_dir, file_pattern);
    } else {
        /* For standard patterns, use the system glob() function */
        glob_t glob_result;
        int glob_flags = GLOB_TILDE;  /* Support ~ expansion for home directory */
        
        /* Add brace expansion support if available on this platform */
        #ifdef GLOB_BRACE
        glob_flags |= GLOB_BRACE;  /* For patterns like *.{js,ts} */
        #endif
        
        /* Expand the pattern to match files */
        int glob_status = glob(pattern, glob_flags, NULL, &glob_result);
        if (glob_status != 0 && glob_status != GLOB_NOMATCH) {
            return 1;
        }
        
        /* Process each matched file */
        for (size_t i = 0; i < glob_result.gl_pathc; i++) {
            collect_file(glob_result.gl_pathv[i]);
        }
        
        globfree(&glob_result);
    }
    
    /* Return 0 if we found at least one new file, 1 otherwise */
    return (files_found > initial_files_found) ? 0 : 1;
}


/**
 * Cleanup function to free memory before exit
 */
void cleanup(void) {
    /* Free the processed files list */
    for (int i = 0; i < num_processed_files; i++) {
        free(processed_files[i]);
    }
    
    /* Remove temporary file */
    if (strlen(temp_file_path) > 0) {
        unlink(temp_file_path);
    }
    
    /* Remove tree file if it exists */
    if (strlen(tree_file_path) > 0) {
        unlink(tree_file_path);
    }
}

/**
 * Main function - program entry point
 */
int main(int argc, char *argv[]) {
    /* Parse command line arguments */
    char *user_instructions = NULL;
    int i;
    
    /* Register cleanup handler */
    atexit(cleanup);
    
    /* Create temporary file for output assembly */
    strcpy(temp_file_path, TEMP_FILE_TEMPLATE);
    int fd = mkstemp(temp_file_path);
    if (fd == -1) {
        perror("Failed to create temporary file");
        return 1;
    }
    
    temp_file = fdopen(fd, "w");
    if (!temp_file) {
        perror("Failed to open temporary file");
        close(fd);
        return 1;
    }
    
    /* Process command line options */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_help();
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--command") == 0) {
            if (i + 1 < argc) {
                user_instructions = argv[i + 1];
                i++;  /* Skip the next argument */
            } else {
                fprintf(stderr, "Error: -c requires an argument\n");
                fclose(temp_file);
                unlink(temp_file_path);
                return 1;
            }
        } else if (strcmp(argv[i], "--no-gitignore") == 0) {
            respect_gitignore = 0;
        } else {
            /* Not an option, break out to process files */
            break;
        }
    }
    
    /* Add user instructions if provided */
    add_user_instructions(user_instructions);
    
    /* Load gitignore files if enabled */
    if (respect_gitignore) {
        load_all_gitignore_files();
    }
    
    /* Check if we need to read from stdin */
    if (i >= argc) {
        /* No file arguments, check if stdin is a terminal */
        if (isatty(STDIN_FILENO)) {
            /* If stdin is a terminal, show help */
            show_help();
        } else {
            process_stdin();
        }
    } else {
        /* Process each file argument */
        for (; i < argc; i++) {
            collect_file(argv[i]);
        }
    }
    
    /* Check if any files were found */
    if (files_found == 0) {
        fprintf(stderr, "No files to process\n");
        fclose(temp_file);
        unlink(temp_file_path);
        return 1;
    }
    
    /* Generate and add file tree */
    generate_file_tree();
    
    /* Add file context header and output content of each file */
    fprintf(temp_file, "<file_context>\n\n");
    
    /* Output content of each processed file */
    for (int i = 0; i < num_processed_files; i++) {
        output_file_content(processed_files[i], temp_file);
    }
    
    /* Add closing file_context tag */
    fprintf(temp_file, "\n</file_context>\n");
    
    /* Flush and close the temp file */
    fclose(temp_file);
    
    /* Display the output directly to stdout */
    char cmd[MAX_PATH + 10];
    sprintf(cmd, "cat %s", temp_file_path);
    system(cmd);
    
    /* Cleanup is handled by atexit handler */
    return 0;
}
