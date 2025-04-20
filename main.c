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
#include <ctype.h>    /* For isprint */
#include <errno.h>    /* For strerror */
#include <stdarg.h>   /* For va_list, va_start, va_end */
#include "gitignore.h"

/* ========================= NEW HELPERS ========================= */

/* Simple fatal-error helper */
static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(EXIT_FAILURE);
}

/* Read entire FILE* into a NUL-terminated buffer.
 * Caller must free().  Returns NULL on OOM or read error. */
static char *slurp_stream(FILE *fp) {
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (len + 1 >= cap) {
            /* Double buffer size, check for overflow */
            size_t new_cap = cap * 2;
            if (new_cap <= cap) { /* Overflow check */
                free(buf);
                return NULL;
            }
            cap = new_cap;
            char *tmp = realloc(buf, cap);
            if (!tmp) { free(buf); return NULL; }
            buf = tmp;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';

    /* Check for read errors */
    if (ferror(fp)) {
        free(buf);
        return NULL;
    }
    return buf;
}


/* Convenience: slurp a *file*.  Returns NULL (and sets errno) on error. */
static char *slurp_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;
    char *txt = slurp_stream(fp);
    /* fclose preserves errno if slurp_stream failed */
    int saved_errno = errno;
    fclose(fp);
    errno = saved_errno; /* Restore errno from fopen or slurp_stream */
    return txt;
}

/* ========================= EXISTING CODE ======================= */

#define MAX_PATH 4096
#define BINARY_CHECK_SIZE 1024 /* Bytes to check for binary detection */
#define TEMP_FILE_TEMPLATE "/tmp/llm_ctx_XXXXXX"
#define MAX_PATTERNS 64   /* Maximum number of patterns to support */
#define MAX_FILES 1024    /* Maximum number of files to process */
/* Increased buffer size to 8MB, aligning better with typical LLM context limits (e.g., ~2M tokens) */
#define STDIN_BUFFER_SIZE (8 * 1024 * 1024) /* 8MB buffer for stdin content */

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
bool process_stdin_content(void);
void output_file_callback(const char *name, const char *type, const char *content);
bool is_binary(FILE *file);

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
char *user_instructions = NULL;   /* malloc'd / strdup'd – free in cleanup() */
static bool want_editor_comments = false;   /* -e flag */

/**
 * Add the response guide block to the output
 */
static void add_response_guide(const char *problem) {
    if (!problem || !*problem) return;
    fprintf(temp_file,
        "<response_guide>\n"
        "LLM: Please respond using the markdown format below.\n"
        "## Problem Statement\n"
        "Summarize the user's request or problem based on the overall context provided.\n"
        "## Response\n"
        "    1. Provide a clear, step-by-step solution or explanation.\n"
        "    2. %s\n"
        "</response_guide>\n\n",
        /* The only argument needed is for the %s above */
        want_editor_comments ?
          "Return **PR-style code review comments**: use GitHub inline-diff syntax, group notes per file, justify each change, and suggest concrete refactors."
          : "No code-review block is required.");
}


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
        bool is_special = (strcmp(filepath, "stdin_content") == 0);
        struct stat statbuf;
        bool is_dir = false; /* Default to not a directory */

        /* Only call lstat for actual file paths, not special names */
        if (!is_special) {
            if (lstat(filepath, &statbuf) == 0) {
                is_dir = S_ISDIR(statbuf.st_mode);
            }
            /* If lstat fails, keep is_dir as false */
        }
        /* For special files like "stdin_content", is_dir remains false */

        FileInfo new_file = {
            .path = "",
            .relative_path = NULL,
            .is_dir = is_dir /* Use determined or default value */
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
/* Removed extraneous closing brace */

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
 * Check if a file stream contains binary data.
 * Reads the first BINARY_CHECK_SIZE bytes and looks for null bytes
 * or a significant number of non-printable characters.
 * Rewinds the file stream before returning.
 *
 * Returns true if the file is likely binary, false otherwise.
 */
bool is_binary(FILE *file) {
    /* Pre-condition: file must be non-NULL and opened in binary read mode */
    assert(file != NULL);

    char buffer[BINARY_CHECK_SIZE];
    size_t bytes_read;
    long original_pos = ftell(file); /* Remember original position */
    bool likely_binary = false;

    /* Read a chunk from the beginning */
    rewind(file);
    bytes_read = fread(buffer, 1, sizeof(buffer), file);

    if (bytes_read > 0) {
        /* Pass 1: Check for Null Bytes */
        /* This is the strongest indicator and catches many binary files quickly. */
        for (size_t i = 0; i < bytes_read; i++) {
            if (buffer[i] == '\0') {
                likely_binary = true;
                goto end_check; /* Found null byte, definitely binary */
            }
        }

        /* Pass 2: Check for specific non-whitespace C0 control codes */
        /* If no null bytes were found, check for codes 0x01-0x1F, excluding */
        /* common whitespace TAB (0x09), LF (0x0A), CR (0x0D). */
        /* These are rare in text/code but common in binary data. */
        /* This avoids locale issues with isprint() and high-bit UTF-8 chars. */
        for (size_t i = 0; i < bytes_read; i++) {
            unsigned char current_byte = (unsigned char)buffer[i];
            if (current_byte > 0 && current_byte < 0x20) { /* Range 1-31 */
                if (current_byte != '\t' && current_byte != '\n' && current_byte != '\r') {
                    likely_binary = true;
                    goto end_check; /* Found suspicious control code */
                }
            }
        }
    }

end_check:
    /* Restore original file position */
    fseek(file, original_pos, SEEK_SET);

    /* Post-condition: file pointer is restored */
    assert(ftell(file) == original_pos);

    return likely_binary;
}


/**
 * Display help message with usage instructions and examples
 */
void show_help(void) {
    printf("Usage: llm_ctx [OPTIONS] [FILE...]\n");
    printf("Format files for LLM code analysis with appropriate tags.\n\n");
    printf("Options:\n");
    printf("  -c TEXT        Add instruction text wrapped in <user_instructions> tags\n");
    printf("  -c @FILE       Read instruction text from FILE (any bytes)\n");
    printf("  -c @-          Read instruction text from standard input until EOF\n");
    printf("  -c=\"TEXT\"     Equals form also accepted\n");
    printf("  -e             Instruct the LLM to append PR-style review comments\n");
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
 * Process raw content from stdin
 * Any content piped in is treated as a single file
 * This handles commands like cat, git diff, git show, etc.
 * 
 * Returns true if successful, false on failure
 */
bool process_stdin_content(void) {
    char buffer[4096];
    size_t bytes_read;
    FILE *content_file_write = NULL;
    FILE *content_file_read = NULL;
    bool found_content = false;
    char *stdin_content_buffer = NULL; /* Buffer to hold content if not binary */
    const char *content_to_register = NULL; /* Points to buffer or placeholder */
    char content_type[32] = ""; /* Detected content type */
    bool buffer_filled_completely = false; /* Flag to detect truncation */
    bool truncation_warning_issued = false; /* Ensure warning is printed only once */

    /* Create a temporary file to store the content */
    char content_path[MAX_PATH];
    strcpy(content_path, "/tmp/llm_ctx_content_XXXXXX");
    int fd = mkstemp(content_path);
    if (fd == -1) {
        perror("Failed to create temporary file for stdin");
        return false;
    }

    content_file_write = fdopen(fd, "wb"); /* Open in binary write mode */
    if (!content_file_write) {
        perror("Failed to open temporary file for writing");
        close(fd);
        unlink(content_path);
        return false;
    }

    /* Read all data from stdin and save to temp file */
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
        fwrite(buffer, 1, bytes_read, content_file_write);
        found_content = true;
    }
    fclose(content_file_write); /* Close write handle */

    /* Check if we actually got any content. If not, treat as empty input. */
    if (!found_content) {
        /* Register stdin_content with empty content */
        content_to_register = "";
        strcpy(content_type, ""); /* No type for empty */
        /* Register stdin_content with empty content */
        content_to_register = "";
        strcpy(content_type, ""); /* No type for empty */
        /* content_file_read remains NULL */
    } else {
        /* Content was found, reopen the temp file for reading */
        content_file_read = fopen(content_path, "rb");
        if (!content_file_read) {
            perror("Failed to reopen temporary file for reading");
            unlink(content_path);
            return false;
        }

        /* Check if the content is binary */
        if (is_binary(content_file_read)) {
            content_to_register = "[Binary file content skipped]";
            strcpy(content_type, ""); /* No type for binary */
        } else {
            /* Not binary, determine content type and read content */
            rewind(content_file_read); /* Rewind after binary check */
            char first_line[1024] = "";
            if (fgets(first_line, sizeof(first_line), content_file_read) != NULL) {
                /* Content type detection logic */
                if (strstr(first_line, "diff --git") == first_line ||
                    strstr(first_line, "commit ") == first_line ||
                    strstr(first_line, "index ") == first_line ||
                    strstr(first_line, "--- a/") == first_line) {
                    strcpy(content_type, "diff");
                } else if (first_line[0] == '{' || first_line[0] == '[') {
                    strcpy(content_type, "json");
                } else if (strstr(first_line, "<?xml") == first_line ||
                           strstr(first_line, "<") != NULL) {
                    strcpy(content_type, "xml");
                } else if (first_line[0] == '#' ||
                           strstr(first_line, "```") != NULL) {
                    strcpy(content_type, "markdown");
                }
            }

            /* Allocate buffer and read the full content */
            rewind(content_file_read); /* Rewind again before full read */
            stdin_content_buffer = malloc(STDIN_BUFFER_SIZE);
            if (!stdin_content_buffer) {
                /* Post-condition: Allocation failed */
                assert(stdin_content_buffer == NULL);
                perror("Failed to allocate memory for stdin content");
                fclose(content_file_read);
                unlink(content_path);
                return false;
            }

            size_t total_read = 0;
            while (total_read < STDIN_BUFFER_SIZE - 1) {
                size_t space_left = STDIN_BUFFER_SIZE - 1 - total_read;
                /* Read either a full buffer or just enough to fill stdin_content_buffer */
                size_t bytes_to_read = (space_left < sizeof(buffer)) ? space_left : sizeof(buffer);

                bytes_read = fread(buffer, 1, bytes_to_read, content_file_read);

                if (bytes_read > 0) {
                    memcpy(stdin_content_buffer + total_read, buffer, bytes_read);
                    total_read += bytes_read;

                    /* Check if we've filled the buffer exactly */
                    if (total_read == STDIN_BUFFER_SIZE - 1) {
                        buffer_filled_completely = true;
                        break; /* Exit loop to check if there's more data */
                    }
                } else {
                    /* Check for read error */
                    if (ferror(content_file_read)) {
                        perror("Error reading from temporary stdin file");
                        /* Consider this a failure, cleanup will happen below */
                        if (stdin_content_buffer) free(stdin_content_buffer);
                        fclose(content_file_read);
                        unlink(content_path);
                        return false;
                    }
                    /* EOF reached before buffer was full */
                    break;
                }
            }
            stdin_content_buffer[total_read] = '\0'; /* Null-terminate */

            /* Check for truncation if the buffer was filled */
            if (buffer_filled_completely) {
                /* Try reading one more byte to see if input was longer */
                int next_char = fgetc(content_file_read);
                if (next_char != EOF) {
                    /* More data exists - input was truncated */
                    fprintf(stderr, "Warning: Standard input exceeded buffer size (%d MB) and was truncated.\n", STDIN_BUFFER_SIZE / (1024 * 1024));
                    truncation_warning_issued = true;
                    /* Put the character back (optional, but good practice) */
                    ungetc(next_char, content_file_read);
                }
                /* Post-condition: Checked for data beyond buffer limit */
                assert(truncation_warning_issued == (next_char != EOF));
            }

            content_to_register = stdin_content_buffer;
        }
    }

    /* Common path for registration and cleanup */
    output_file_callback("stdin_content", content_type, content_to_register);

    /* Clean up resources */
    if (content_file_read) { /* Only close if it was opened */
        fclose(content_file_read);
    }
    unlink(content_path); /* Always remove temp file */
    if (stdin_content_buffer) {
        free(stdin_content_buffer); /* Free buffer if allocated */
    }

    /* Increment files found so we don't error out */
    files_found++;

    return true; /* Indicate success */
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
    
    /* Add to processed files list for content output and tree display */
    if (num_processed_files < MAX_FILES) {
        processed_files[num_processed_files] = strdup(name);
        /* Ensure strdup succeeded before proceeding */
        /* A failure here is critical, indicating severe memory issues. */
        assert(processed_files[num_processed_files] != NULL && "strdup failed in output_file_callback");
            
        num_processed_files++; /* Increment count *after* successful allocation */
            
        /* Also add to the file tree structure */
        add_to_file_tree(name); /* Handles special "stdin_content" case */
    }
}

/**
 * Collect a file to be processed but don't output its content yet
 * 
 * Only adds the file to our tracking lists if it hasn't been processed yet
 * Only adds regular, readable files to the processed_files list for content output.
 * Assumes ignore checks and path validation happened before calling.
 * Increments files_found for files added.
 * 
 * Returns true if the file was processed (added or skipped), false on error (e.g., memory).
 */
bool collect_file(const char *filepath) {
    // Avoid duplicates in content output
    if (file_already_processed(filepath)) {
        return true;
    }

    struct stat statbuf;
    // Check if it's a regular file and readable
    // lstat check is slightly redundant as caller likely did it, but safe.
    if (lstat(filepath, &statbuf) == 0 && S_ISREG(statbuf.st_mode) && access(filepath, R_OK) == 0) {
         if (num_processed_files < MAX_FILES) {
             // Add to the list of files whose content will be outputted
             processed_files[num_processed_files] = strdup(filepath);
             if (processed_files[num_processed_files] != NULL) {
                 num_processed_files++;
                 files_found++; // Increment count only for files we will output content for
                 return true;
             } else {
                 // strdup failed - memory allocation error
                 perror("Failed to allocate memory for file path");
                 return false; // Indicate error
             }
         } else {
             // Too many files - log warning?
             fprintf(stderr, "Warning: Maximum number of files (%d) exceeded. Skipping %s\n", MAX_FILES, filepath);
             return false; // Indicate that we couldn't process it
         }
    }
    // Not a regular readable file, don't add to processed_files for content output.
    // Return true because it's not an error, just skipping content for this path.
    return true;
}

/**
 * Output a file's content to the specified output file
 * 
 * Reads the file content and formats it with fenced code blocks
 * and file headers for better LLM understanding
 * 
 * Returns true on success, false on failure
 */
bool output_file_content(const char *filepath, FILE *output) {
    /* Check if this is a special file (e.g., stdin content) */
    for (int i = 0; i < num_special_files; i++) {
        if (strcmp(filepath, special_files[i].filename) == 0) {
            fprintf(output, "File: %s\n", filepath);
            /* Check if the content is the binary placeholder */
            if (strcmp(special_files[i].content, "[Binary file content skipped]") == 0) {
                fprintf(output, "%s\n", special_files[i].content);
            } else {
                /* Format with code fences for non-binary special files */
                fprintf(output, "```%s\n", special_files[i].type);
                fprintf(output, "%s", special_files[i].content);
                fprintf(output, "```\n");
            }
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
        /* Don't print error for non-readable files, just skip */
        return true;
    }

    /* Open in binary read mode for binary detection */
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        /* Should not happen due to access check, but handle anyway */
        return true;
    }

    /* Check if the file is binary */
    if (is_binary(file)) {
        fprintf(output, "File: %s\n", filepath);
        fprintf(output, "[Binary file content skipped]\n");
        fprintf(output, "----------------------------------------\n");
    } else {
        /* File is not binary, output its content with fences */
        fprintf(output, "File: %s\n", filepath);
        fprintf(output, "```\n");

        /* Read and write the file contents in chunks */
        char buffer[4096];
        size_t bytes_read;
        /* Ensure we read from the beginning after the binary check */
        rewind(file);
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
            fwrite(buffer, 1, bytes_read, output);
        }

        /* Close the code fence and add a separator */
        fprintf(output, "```\n");
        fprintf(output, "----------------------------------------\n");
    }

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
    int fnmatch_flags = FNM_PATHNAME; // Basic flag for matching path components
    // We removed FNM_PERIOD logic here. Let '*' match dotfiles.
    // Filtering based on actual .gitignore rules happens in should_ignore_path.
 
    /* Try to open directory - silently return if can't access */
    if (!(dir = opendir(base_dir)))
        return;
    
    /* Process each entry in the directory */
    while ((entry = readdir(dir)) != NULL) {
        /* Skip the special directory entries */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        /* Always skip the .git directory to mirror Git's behavior */
        /* This prevents descending into the Git internal directory structure. */
        if (strcmp(entry->d_name, ".git") == 0) {
            continue; // Skip .git entirely
        }

        /* Construct the full path of the current entry */
        snprintf(path, sizeof(path), "%s/%s", base_dir, entry->d_name);

        /* Get entry information - skip if can't stat */
        if (lstat(path, &statbuf) == -1)
            continue;
        
        /* Check if the path should be ignored *before* processing */
        if (respect_gitignore && should_ignore_path(path)) {
            continue; /* Skip ignored files/directories */
        }

        // Add any non-ignored entry to the file tree structure
        add_to_file_tree(path);

        /* If entry is a directory, recurse into it */
        if (S_ISDIR(statbuf.st_mode)) {
            find_recursive(path, pattern);
        }
        /* If entry is a regular file, check if it matches the pattern */
        else if (S_ISREG(statbuf.st_mode)) {
            /* Match filename against the pattern using appropriate flags */
            if (fnmatch(pattern, entry->d_name, fnmatch_flags) == 0) {
                // Collect the file for content output ONLY if it matches and is readable
                // collect_file now handles adding to processed_files and files_found
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
            const char *path = glob_result.gl_pathv[i];

            // Check ignore rules before adding
            if (respect_gitignore && should_ignore_path(path)) {
                continue;
            }

            // Add to tree structure
            add_to_file_tree(path);

            // Collect file content if it's a regular file
            struct stat statbuf;
            if (lstat(path, &statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
                // collect_file now handles adding to processed_files and files_found,
                // and checks for readability.
                collect_file(path);
           }
           // If it's a directory matched by glob, descend into it
           else if (S_ISDIR(statbuf.st_mode)) {
                // Recursively find all files within this directory, respecting gitignore
                // Use "*" as the pattern to match all files within.
                find_recursive(path, "*");
           }
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
    /* Free dynamically allocated user instructions */
    if (user_instructions) free(user_instructions);

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
    /* user_instructions is now a global variable */
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
            show_help(); /* Exits */
        } else if (strncmp(argv[i], "-c", 2) == 0 || strncmp(argv[i], "--command=", 10) == 0) {
            const char *arg = NULL;
            bool is_long_opt = (strncmp(argv[i], "--command=", 10) == 0);

            /* Support -cFOO | -c FOO | -c=FOO | --command=FOO */
            if (!is_long_opt && argv[i][2] == '\0') { /* "-c" form */
                if (i + 1 >= argc) die("Error: -c requires an argument");
                arg = argv[++i];
            } else if (!is_long_opt && argv[i][2] == '=') { /* "-c=FOO" */
                arg = argv[i] + 3;
            } else if (!is_long_opt && argv[i][2] != '\0') { /* "-cFOO" */
                 /* Note: This form "-cFOO" is less common but supported */
                arg = argv[i] + 2;
            } else if (is_long_opt) { /* "--command=FOO" */
                arg = argv[i] + 10; /* Skip "--command=" */
            } else {
                /* Should not happen based on outer if, but defensive */
                die("Internal error parsing option: %s", argv[i]);
            }


            if (!arg || *arg == '\0') die("Error: -c requires a non-empty argument");

            /* ----- @file / @- decoding ----- */
            if (arg[0] == '@') {
                if (strcmp(arg, "@-") == 0) {               /* read from STDIN */
                    /* Ensure stdin is not a TTY unless explicitly using @- */
                    if (isatty(STDIN_FILENO)) {
                        fprintf(stderr, "Reading instructions from terminal. Enter text and press Ctrl+D when done.\n");
                    }
                    user_instructions = slurp_stream(stdin);
                    if (!user_instructions) die("Error reading instructions from stdin: %s", ferror(stdin) ? strerror(errno) : "Out of memory");
                    /* After reading from stdin for -c @-, subsequent file processing expects stdin to be closed or irrelevant. */
                    /* If file_mode is not set, we might have issues if process_stdin_content is called later. */
                    /* Let's prevent mixing -c @- with implicit stdin reading. */
                    if (!file_mode) {
                         fprintf(stderr, "Warning: Using -c @- implies file mode (-f). Subsequent arguments will be treated as files.\n");
                         file_mode = 1;
                         /* If file_args_start wasn't set by -f, assume files start after @- */
                         if (file_args_start == 0) file_args_start = i + 1;
                    }

                } else {                                    /* read from file */
                    user_instructions = slurp_file(arg + 1);
                    if (!user_instructions)
                        die("Cannot open or read instruction file '%s': %s", arg + 1, strerror(errno));
                }
            } else {
                user_instructions = strdup(arg);            /* old behaviour */
                if (!user_instructions) die("Out of memory duplicating -c argument");
            }
        } else if (strcmp(argv[i], "-f") == 0) {
            file_mode = 1;  /* Switch to file mode */
            /* Only set file_args_start if it hasn't been set by -c @- logic */
            if (file_args_start == 0) {
                file_args_start = i + 1;  /* Files start after the -f flag */
            }
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--editor-comments") == 0) {
            want_editor_comments = true;
        } else if (strcmp(argv[i], "--no-gitignore") == 0) {
            respect_gitignore = false; /* Use bool directly */
        } else if (file_mode && argv[i][0] != '-') {
            /* If in file mode and not an option, it's a file/pattern */
            /* Argument processing happens later */
            continue;
        } else if (argv[i][0] == '-') {
            /* Unknown option */
             die("Error: Unknown option %s", argv[i]);
        } else if (!file_mode) {
            /* Not in file mode but saw a non-option - error */
            /* This case should only be reachable if no -f and no -c @- was used */
             die("Error: File arguments must be specified with -f flag or use -c @-");
        }
        /* If it's a file argument in file_mode, the loop continues */
    }

    /* Add user instructions if provided (now happens *after* parsing all args) */
    add_user_instructions(user_instructions);
    add_response_guide(user_instructions); /* Add response guide based on instructions */
    
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
            /* Process each file argument (which could be a file path or a glob pattern) */
            for (i = file_args_start; i < argc; i++) {
                // process_pattern handles both individual files and glob expansion
                process_pattern(argv[i]);
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
