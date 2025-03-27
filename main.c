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
#include <stdbool.h>  /* For bool type */
#include <stdint.h>   /* For fixed-width integer types */
#include "gitignore.h"

#define MAX_PATH 4096
#define TEMP_FILE_TEMPLATE "/tmp/llm_ctx_XXXXXX"
#define MAX_PATTERNS 64   /* Maximum number of patterns to support */
#define MAX_FILES 1024    /* Maximum number of files to process */
#define STDIN_BUFFER_SIZE (1024 * 1024) /* 1MB buffer for stdin content */

/* Structure to hold file information for the tree */
typedef struct {
    char path[MAX_PATH];
    char *relative_path;
    bool is_dir;
} FileInfo;

/* Function declarations with proper prototypes */
void show_help(void);
bool collect_file(const char *filepath);
bool output_file_content(const char *filepath, FILE *output);
void add_user_instructions(const char *instructions);
void find_recursive(const char *base_dir, const char *pattern);
bool process_pattern(const char *pattern);
void generate_file_tree(void);
void add_to_file_tree(const char *filepath);
int compare_file_paths(const void *a, const void *b);
char *find_common_prefix(void);
void print_tree_node(const char *path, int level, bool is_last, const char *prefix);
void build_tree_recursive(char **paths, int count, int level, char *prefix, const char *path_prefix);
bool process_stdin(void);
bool process_stdin_content(void);
bool is_likely_filenames(FILE *input);
void output_file_callback(const char *name, const char *type, const char *content);

/* Special file structure for stdin content */
typedef struct {
    char filename[MAX_PATH];
    char type[32];
    char *content;
} SpecialFile;

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
SpecialFile special_files[10]; /* Support up to 10 special files */
int num_special_files = 0;

/**
 * Check if a file has already been processed to avoid duplicates
 * 
 * Returns true if the file has been processed, false otherwise
 */
bool file_already_processed(const char *filepath) {
    /* Pre-condition: valid filepath parameter */
    assert(filepath != NULL);
    assert(strlen(filepath) > 0);
    
    /* Invariant: num_processed_files is always valid */
    assert(num_processed_files >= 0);
    assert(num_processed_files <= MAX_FILES);
    
    for (int i = 0; i < num_processed_files; i++) {
        /* Invariant: each processed_files entry is valid */
        assert(processed_files[i] != NULL);
        
        if (strcmp(processed_files[i], filepath) == 0) {
            /* Post-condition: found matching file */
            return true;
        }
    }
    /* Post-condition: file not found */
    return false;
}

/**
 * Add a file to the processed files list
 */
void add_to_processed_files(const char *filepath) {
    /* Pre-condition: valid filepath */
    assert(filepath != NULL);
    assert(strlen(filepath) > 0);
    
    /* Pre-condition: we have space for more files */
    assert(num_processed_files < MAX_FILES);
    
    if (num_processed_files < MAX_FILES) {
        processed_files[num_processed_files] = strdup(filepath);
        /* Invariant: successfully allocated memory */
        assert(processed_files[num_processed_files] != NULL);
        
        num_processed_files++;
        
        /* Also add to the file tree structure */
        add_to_file_tree(filepath);
        
        /* Post-condition: file was added successfully */
        assert(num_processed_files > 0);
        assert(strcmp(processed_files[num_processed_files-1], filepath) == 0);
    }
}

/**
 * Add a file to the file tree structure
 */
void add_to_file_tree(const char *filepath) {
    /* Pre-condition: valid filepath */
    assert(filepath != NULL);
    assert(strlen(filepath) > 0);
    
    /* Pre-condition: space in file tree */
    assert(file_tree_count < MAX_FILES);
    
    if (file_tree_count < MAX_FILES) {
        struct stat statbuf;
        
        /* Get file information */
        if (lstat(filepath, &statbuf) == 0) {
            FileInfo new_file = {
                .path = "",
                .relative_path = NULL,
                .is_dir = S_ISDIR(statbuf.st_mode)
            };
            
            /* Store file path */
            strncpy(new_file.path, filepath, MAX_PATH - 1);
            new_file.path[MAX_PATH - 1] = '\0';
            
            /* Verify path was copied correctly */
            assert(strcmp(new_file.path, filepath) == 0);
            
            /* Add to file tree array */
            file_tree[file_tree_count] = new_file;
            file_tree_count++;
            
            /* Post-condition: file tree was updated */
            assert(file_tree_count > 0);
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
void print_tree_node(const char *path, int level, bool is_last, const char *prefix) {
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
    
    /* Find current directory for this level */
    for (int i = 0; i < count; i++) {
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
    for (int i = 0; i < count; i++) {
        if (strchr(paths[i], '/') == NULL) {
            fprintf(tree_file, "%s%s%s\n", 
                   prefix, 
                   (level > 0) ? "├── " : "", 
                   paths[i]);
        }
    }
    
    /* Then process subdirectories */
    for (int i = 0; i < count;) {
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
        for (int j = i; j < count; j++) {
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
            for (int j = i; j < i + subdir_count; j++) {
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
    printf("  -f [FILE...]   Process files instead of stdin content\n");
    printf("  -h             Show this help message\n");
    printf("  --no-gitignore Ignore .gitignore files when collecting files\n\n");
    printf("By default, llm_ctx reads content from stdin.\n");
    printf("Use -f flag to indicate file arguments are provided.\n\n");
    printf("Examples:\n");
    printf("  # Process content from stdin (default behavior)\n");
    printf("  git diff | llm_ctx -c \"Please explain these changes\"\n\n");
    printf("  # Process content from a file via stdin\n");
    printf("  cat complex_file.json | llm_ctx -c \"Explain this JSON structure\"\n\n");
    printf("  # Process specific files (using -f flag)\n");
    printf("  llm_ctx -f src/main.c include/header.h\n\n");
    printf("  # Use with find to process files\n");
    printf("  find src -name \"*.c\" | xargs llm_ctx -f\n\n");
    printf("  # Add instructions for the LLM\n");
    printf("  llm_ctx -c \"Please explain this code\" -f src/*.c\n\n");
    printf("  # Pipe to clipboard\n");
    printf("  git diff | llm_ctx -c \"Review these changes\" | pbcopy\n");
    exit(0);
}

/**
 * Determine if input is likely filenames or raw content
 * Checks the first few lines of input to see if they look like valid files
 * 
 * Returns true if likely filenames, false if likely content
 */
bool is_likely_filenames(FILE *input) {
    char line[MAX_PATH];
    int line_count = 0;
    int valid_file_count = 0;
    
    /* Remember current position */
    long pos = ftell(input);
    
    /* Check first few lines */
    while (fgets(line, sizeof(line), input) != NULL && line_count < 5) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        
        /* Skip empty lines */
        if (strlen(line) == 0) {
            continue;
        }
        
        /* Content type detection heuristics */
        if (line[0] == '{' || line[0] == '[' || 
            strstr(line, "<?xml") == line || 
            strstr(line, "<") != NULL ||
            line[0] == '#' || 
            strstr(line, "```") != NULL ||
            strstr(line, "diff --git") == line || 
            strstr(line, "commit ") == line ||
            strstr(line, "index ") == line ||
            strstr(line, "--- a/") == line) {
            /* If it looks like content, immediately return false */
            fseek(input, pos, SEEK_SET);
            return 0;
        }
        
        /* Check if line looks like a valid file */
        if (access(line, F_OK) == 0) {
            valid_file_count++;
        }
        
        line_count++;
    }
    
    /* Restore position */
    fseek(input, pos, SEEK_SET);
    
    /* If at least half the lines are valid files, assume filenames */
    /* If no files were found, assume this is content */
    if (line_count > 0 && valid_file_count == 0) {
        return false;  /* No valid files found, assume content */
    }
    
    /* If at least one valid file found, assume filenames */
    return (valid_file_count > 0) ? true : false;
}

/**
 * Process lines from stdin
 * Each line is treated as a filename to process
 * 
 * Returns true if files were processed, false otherwise
 */
bool process_stdin(void) {
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
    
    return (files_found > initial_files_found);
}

/**
 * Process raw content from stdin
 * Any content piped in is treated as a single file
 * This handles commands like cat, git diff, git show, etc.
 * 
 * Returns true if successful, false on failure
 */
bool process_stdin_content(void) {
    char buffer[4096];
    size_t bytes_read;
    FILE *content_file;
    bool found_content = false;
    
    /* Create a temporary file to store the content */
    char content_path[MAX_PATH];
    strcpy(content_path, "/tmp/llm_ctx_content_XXXXXX");
    int fd = mkstemp(content_path);
    if (fd == -1) {
        return false;
    }
    
    content_file = fdopen(fd, "w+");
    if (!content_file) {
        close(fd);
        return false;
    }
    
    /* Read all data from stdin and save to temp file */
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
        fwrite(buffer, 1, bytes_read, content_file);
        found_content = true;
    }
    
    /* Check if we actually got any content */
    if (!found_content) {
        fclose(content_file);
        unlink(content_path);
        return false;
    }
    
    /* Rewind the file so we can read it */
    rewind(content_file);
    
    /* Try to determine the content type for proper formatting */
    char content_type[32] = "";
    char first_line[1024] = "";
    
    /* Read the first line to help detect content type */
    if (fgets(first_line, sizeof(first_line), content_file) != NULL) {
        /* Git diff detection */
        if (strstr(first_line, "diff --git") == first_line || 
            strstr(first_line, "commit ") == first_line ||
            strstr(first_line, "index ") == first_line ||
            strstr(first_line, "--- a/") == first_line) {
            strcpy(content_type, "diff");
        }
        /* JSON detection */
        else if (first_line[0] == '{' || first_line[0] == '[') {
            strcpy(content_type, "json");
        }
        /* XML detection */
        else if (strstr(first_line, "<?xml") == first_line || 
                 strstr(first_line, "<") != NULL) {
            strcpy(content_type, "xml");
        }
        /* Markdown detection */
        else if (first_line[0] == '#' || 
                 strstr(first_line, "```") != NULL) {
            strcpy(content_type, "markdown");
        }
    }
    
    /* Rewind again to read the full content */
    rewind(content_file);
    
    /* We need to store the content for later output */
    char *stdin_content = malloc(STDIN_BUFFER_SIZE); 
    if (!stdin_content) {
        fclose(content_file);
        unlink(content_path);
        return 1;
    }
    
    size_t total_read = 0;
    stdin_content[0] = '\0'; /* Initialize as empty string */

    /* Read from content_file into stdin_content, respecting STDIN_BUFFER_SIZE */
    while (total_read < STDIN_BUFFER_SIZE - 1) { /* Leave space for null terminator */
        size_t space_left = STDIN_BUFFER_SIZE - 1 - total_read;
        /* Determine how much to read: either a full buffer chunk or just the remaining space */
        size_t bytes_to_read = (space_left < sizeof(buffer)) ? space_left : sizeof(buffer);
        
        bytes_read = fread(buffer, 1, bytes_to_read, content_file);
        
        if (bytes_read > 0) {
            /* Append read data to stdin_content */
            memcpy(stdin_content + total_read, buffer, bytes_read);
            total_read += bytes_read;
        } else {
            /* End of file reached or read error occurred */
            break;
        }
    }
    /* Ensure null termination */
    stdin_content[total_read] = '\0'; 
    
    /* Register a special file output function */
    output_file_callback("stdin_content", content_type, stdin_content);
    
    /* Clean up */
    fclose(content_file);
    unlink(content_path);
    
    /* Increment files found so we don't error out */
    files_found++;
    
    return true;
}

/* Function to register a callback for a special file */
void output_file_callback(const char *name, const char *type, const char *content) {
    /* Pre-condition: validate inputs */
    assert(name != NULL);
    assert(type != NULL);
    assert(content != NULL);
    
    /* Check if we have room for more special files */
    assert(num_special_files < 10);
    if (num_special_files >= 10) {
        return;
    }
    
    /* Store the special file information */
    strcpy(special_files[num_special_files].filename, name);
    strcpy(special_files[num_special_files].type, type);
    special_files[num_special_files].content = strdup(content);
    
    /* Invariant: memory was allocated successfully */
    assert(special_files[num_special_files].content != NULL);
    
    num_special_files++;
    
    /* Post-condition: file was added */
    assert(num_special_files > 0);
    
    /* Add to processed files list for tree display */
    if (num_processed_files < MAX_FILES) {
        processed_files[num_processed_files] = strdup(name);
        num_processed_files++;
        
        /* Also add to the file tree structure */
        add_to_file_tree(name);
    }
}

/**
 * Collect a file to be processed but don't output its content yet
 * 
 * Only adds the file to our tracking lists if it hasn't been processed yet
 * 
 * Returns true on success, false on failure
 */
bool collect_file(const char *filepath) {
    /* Check if we've already processed this file to avoid duplicates */
    if (file_already_processed(filepath)) {
        return true;
    }

    /* Check if the file should be ignored based on gitignore patterns */
    if (should_ignore_path(filepath)) {
        return true;  /* Silently skip ignored files */
    }
    
    /* Check if file exists and is readable before adding it */
    if (access(filepath, R_OK) != 0) {
        return false;
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
    
    return true;
}

/* Output file content implementation continues below */

/**
 * Output a file's content to the specified output file
 * 
 * Reads the file content and formats it with fenced code blocks
 * and file headers for better LLM understanding
 * 
 * Returns true on success, false on failure
 */
bool output_file_content(const char *filepath, FILE *output) {
    /* Check if this is a special file (stdin content) */
    for (int i = 0; i < num_special_files; i++) {
        if (strcmp(filepath, special_files[i].filename) == 0) {
            /* Format with a header showing the filename for context */
            fprintf(output, "File: %s\n", filepath);
            fprintf(output, "```%s\n", special_files[i].type);
            
            /* Write the stored content */
            fprintf(output, "%s", special_files[i].content);
            
            /* Close the code fence and add a separator for visual clarity */
            fprintf(output, "```\n");
            fprintf(output, "----------------------------------------\n");
            
            return true;
        }
    }
    
    /* Not a special file, process normally */
    
    /* Check if path is a directory - skip it if so */
    struct stat statbuf;
    if (lstat(filepath, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
        return true; /* Skip directories silently */
    }

    /* Check if file exists and is readable before attempting to open */
    if (access(filepath, R_OK) != 0) {
        return false;
    }

    FILE *file = fopen(filepath, "r");
    if (!file) {
        return false;
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
    return true;
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
bool process_pattern(const char *pattern) {
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
            return false;
        }
        
        /* Process each matched file */
        for (size_t i = 0; i < glob_result.gl_pathc; i++) {
            collect_file(glob_result.gl_pathv[i]);
        }
        
        globfree(&glob_result);
    }
    
    /* Return true if we found at least one new file, false otherwise */
    return (files_found > initial_files_found);
}


/**
 * Cleanup function to free memory before exit
 */
void cleanup(void) {
    /* Free the processed files list */
    for (int i = 0; i < num_processed_files; i++) {
        free(processed_files[i]);
    }
    
    /* Free special file content */
    for (int i = 0; i < num_special_files; i++) {
        free(special_files[i].content);
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
    int file_mode = 0;  /* Default to stdin mode */
    int file_args_start = 0;  /* Where file arguments start */
    
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
        } else if (strcmp(argv[i], "-f") == 0) {
            file_mode = 1;  /* Switch to file mode */
            file_args_start = i + 1;  /* Files start after the -f flag */
        } else if (strcmp(argv[i], "--no-gitignore") == 0) {
            respect_gitignore = 0;
        } else if (file_mode && argv[i][0] != '-') {
            /* If in file mode and not an option, it's a file */
            continue;
        } else if (argv[i][0] == '-') {
            /* Unknown option */
            fprintf(stderr, "Error: Unknown option %s\n", argv[i]);
            fclose(temp_file);
            unlink(temp_file_path);
            return 1;
        } else {
            /* Not in file mode but saw a non-option - error */
            fprintf(stderr, "Error: File arguments must be specified with -f flag\n");
            fprintf(stderr, "Example: llm_ctx -f file1.c file2.c\n");
            fclose(temp_file);
            unlink(temp_file_path);
            return 1;
        }
    }
    
    /* Add user instructions if provided */
    add_user_instructions(user_instructions);
    
    /* Load gitignore files if enabled */
    if (respect_gitignore) {
        /* Pre-condition: gitignore is enabled */
        assert(respect_gitignore == 1);
        load_all_gitignore_files();
        /* Post-condition: gitignore patterns may have been loaded */
        assert(respect_gitignore == 1);
    }
    
    /* Process input based on mode */
    if (file_mode) {
        /* Process files */
        if (file_args_start >= argc) {
            /* -f flag provided but no files specified */
            fprintf(stderr, "Warning: -f flag provided but no files specified\n");
        } else {
            /* Process each file argument */
            for (i = file_args_start; i < argc; i++) {
                collect_file(argv[i]);
            }
        }
    } else {
        /* Default to stdin mode */
        if (isatty(STDIN_FILENO)) {
            /* If stdin is a terminal, show help */
            show_help();
        } else {
            /* Process stdin content */
            process_stdin_content();
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
    FILE *final_output = fopen(temp_file_path, "r");
    if (final_output) {
        char buffer[4096];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), final_output)) > 0) {
            fwrite(buffer, 1, bytes_read, stdout);
        }
        fclose(final_output);
    } else {
        perror("Failed to open temporary file for reading");
        /* atexit handler will still attempt cleanup */
        return 1; /* Indicate failure */
    }

    /* Cleanup is handled by atexit handler */
    return 0;
}
