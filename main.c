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
#include <stdarg.h>   /* For va_list, va_start, va_end */
#include <limits.h>   /* For PATH_MAX */
#include <getopt.h>   /* For getopt_long */
#include "gitignore.h"

/* Forward declaration for cleanup function called by fatal() */
void cleanup(void);

/* Structure to hold settings parsed directly from the config file */
typedef struct {
    char *system_prompt_source; /* Allocated string: NULL, inline text, or "@path" */
    bool editor_comments;
    bool copy_to_clipboard;

    /* Flags to track if a setting was explicitly set in the config file */
    bool system_prompt_set;
    bool editor_comments_set;
    bool copy_to_clipboard_set;
} ConfigSettings;

/* Global flag for effective copy_to_clipboard setting */
static bool g_effective_copy_to_clipboard = false;

/* ========================= NEW HELPERS ========================= */

/* Fatal error helper: prints message to stderr, performs cleanup, and exits. */
/* Marked __attribute__((noreturn)) to inform the compiler this function never returns, */
/* potentially enabling optimizations and improving static analysis. */
__attribute__((noreturn))
static void fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    /* Use vfprintf directly to stderr for error messages. */
    vfprintf(stderr, fmt, ap);
    /* Ensure a newline terminates the error message. */
    fputc('\n', stderr);
    va_end(ap);
    /* Explicitly call cleanup before exiting to ensure resources are released, */
    /* normalizing the failure path as per the pragmatic principles. */
    cleanup();
    /* Use _Exit to terminate immediately without invoking further atexit handlers */
    /* or flushing stdio buffers, which can be important for robustness, */
    /* especially if the error occurred within complex signal handlers or */
    /* if stdio state is corrupted. */
    _Exit(EXIT_FAILURE);
}

/* Read entire FILE* into a NUL-terminated buffer.
 * Caller must free().  Returns NULL on OOM or read error. */
static char *slurp_stream(FILE *fp) {
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    /* Check for allocation failure immediately. */
    if (!buf) {
        errno = ENOMEM; /* Set errno for caller */
        return NULL;
    }

    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (len + 1 >= cap) { /* +1 for the potential NUL terminator */
            /* Check for potential size_t overflow before doubling. */
            if (cap > SIZE_MAX / 2) {
                free(buf);
                errno = ENOMEM; /* Indicate memory exhaustion due to overflow */
                return NULL;
            }
            size_t new_cap = cap * 2;
            /* Reallocate buffer */
            char *tmp = realloc(buf, new_cap);
            if (!tmp) {
                free(buf);
                errno = ENOMEM; /* Indicate realloc failure */
                return NULL;
            }
            buf = tmp;
            cap = new_cap;
        }
        buf[len++] = (char)c;
    }
    /* Ensure buffer is null-terminated *before* checking ferror. */
    buf[len] = '\0';

    /* Check for read errors *after* attempting to read the whole stream. */
    if (ferror(fp)) {
        int saved_errno = errno; /* Preserve errno from the failed I/O operation. */
        free(buf);
        errno = saved_errno; /* Restore errno. */
        return NULL;
    }
    return buf;
}


/* Convenience: slurp a *file*.  Returns NULL (and sets errno) on error. */
static char *slurp_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        /* errno is set by fopen */
        return NULL;
    }
    char *txt = slurp_stream(fp);
    int slurp_errno = errno; /* Capture errno *after* slurp_stream */

    if (fclose(fp) != 0) {
        /* If fclose fails, prioritize its errno, unless slurp_stream already failed. */
        if (txt != NULL) {
            /* If slurp succeeded but fclose failed, free buffer and set fclose errno */
            free(txt);
            /* errno is now set by fclose */
            return NULL;
        }
        /* If slurp failed AND fclose failed, keep the slurp_errno */
        errno = slurp_errno;
        /* txt is already NULL, buffer already freed by slurp_stream */
        return NULL;
    }

    /* If fclose succeeded, return the result of slurp_stream, restoring its errno */
    errno = slurp_errno;
    return txt;
}

/* Helper macro to grow buffer and append one line + '\n' */
/* Returns true on success, false on OOM */
#define APPEND_TO_BUFFER(src, len, buffer_ptr, buf_len_ptr, buf_cap_ptr) \
    ({                                                                   \
        bool success = true;                                             \
        /* Ensure space for line + newline + null terminator */          \
        while (*(buf_len_ptr) + (len) + 2 > *(buf_cap_ptr)) {             \
            size_t new_cap = (*(buf_cap_ptr) == 0) ? 256 : *(buf_cap_ptr) * 2; \
            /* Add overflow check? For now, assume it won't exceed SIZE_MAX */ \
            char *new_buffer = realloc(*(buffer_ptr), new_cap);          \
            if (!new_buffer) {                                           \
                /* OOM during buffer growth */                           \
                free(*(buffer_ptr)); /* Free old buffer */               \
                *(buffer_ptr) = NULL;                                    \
                *(buf_cap_ptr) = 0;                                      \
                *(buf_len_ptr) = 0;                                      \
                errno = ENOMEM;                                          \
                success = false;                                         \
                break; /* Exit while loop */                             \
            }                                                            \
            *(buffer_ptr) = new_buffer;                                  \
            *(buf_cap_ptr) = new_cap;                                    \
        }                                                                \
        if (success) {                                                   \
            /* Append the line content and a newline */                  \
            memcpy(*(buffer_ptr) + *(buf_len_ptr), (src), (len));        \
            *(buf_len_ptr) += (len);                                     \
            (*(buffer_ptr))[(*(buf_len_ptr))++] = '\n';                  \
            (*(buffer_ptr))[*(buf_len_ptr)] = '\0'; /* Keep null-terminated */ \
        }                                                                \
        success; /* Return status */                                     \
    })

/* Helper function to store a key-value pair */
/* Returns true on success, false on OOM */
static bool store_kv(ConfigSettings *s, const char *k, const char *v) {
    if (strcmp(k, "copy_to_clipboard") == 0) {
        s->copy_to_clipboard = (strcasecmp(v, "true") == 0 || strcmp(v, "1") == 0 || strcasecmp(v, "yes") == 0);
        s->copy_to_clipboard_set = true;
    } else if (strcmp(k, "editor_comments") == 0) {
        s->editor_comments = (strcasecmp(v, "true") == 0 || strcmp(v, "1") == 0 || strcasecmp(v, "yes") == 0);
        s->editor_comments_set = true;
    } else if (strcmp(k, "system_prompt") == 0) {
        free(s->system_prompt_source); /* Free previous if any */
        s->system_prompt_source = strdup(v);
        if (!s->system_prompt_source) {
            errno = ENOMEM;
            return false; /* Allocation failed */
        }
        s->system_prompt_set = true;
    }
    /* Add other keys here if needed */
    return true;
}

/* Forward declaration for finalize_multiline_block needed by parse_config_file */
static bool finalize_multiline_block(ConfigSettings *s, char *key, char **buf_ptr, size_t *len_ptr, size_t min_indent);

/* Forward declaration for parse_config_file needed by main */
bool parse_config_file(const char *config_path, ConfigSettings *settings);

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
char *find_config_file(void);
bool parse_config_file(const char *config_path, ConfigSettings *settings);
void copy_to_clipboard(const char *buffer);
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
static int file_mode = 0;         /* 0 = stdin mode, 1 = file mode (-f or @-) */
char *user_instructions = NULL;   /* malloc'd / strdup'd – free in cleanup() */
static char *system_instructions = NULL;   /* malloc'd, NULL if not set */
static bool want_editor_comments = false;   /* -e flag */

/**
 * Open the file context block if it hasn't been opened yet.
 * Add system instructions to the output if provided
 */
static void add_system_instructions(const char *msg) {
    if (!msg || !*msg) return;
    fprintf(temp_file, "<system_instructions>\n%s\n</system_instructions>\n\n", msg);
}

/* Global flag to track if any file content has been written */
static bool wrote_file_context = false;

/**
 * Open the file context block lazily, only if needed.
 */
static void open_file_context_if_needed(void) {
    if (!wrote_file_context) {
        fprintf(temp_file, "<file_context>\n\n");
        wrote_file_context = true;
    }
}

/**
 * Add the response guide block to the output
 */
static void add_response_guide(const char *problem) {
    // Add the guide if editor comments are requested OR if user instructions were provided.
    // This ensures -e always adds the guide, even without -c.
    if (want_editor_comments || (problem && *problem)) {
        fprintf(temp_file, "<response_guide>\n");
        fprintf(temp_file, "LLM: Please respond using the markdown format below.\n");

        // Only include Problem Statement section if user instructions were provided
        if (problem && *problem) {
            fprintf(temp_file, "## Problem Statement\n");
            fprintf(temp_file, "Summarize the user's request or problem based on the overall context provided.\n");
        }

        fprintf(temp_file, "## Response\n");
        fprintf(temp_file, "    1. Provide a clear, step-by-step solution or explanation.\n");
        fprintf(temp_file, "    2. %s\n",
                want_editor_comments ?
                  "Return **PR-style code review comments**: use GitHub inline-diff syntax, group notes per file, justify each change, and suggest concrete refactors."
                  : "No code-review block is required."); // If !want_editor_comments, this is only reached if 'problem' exists.

        fprintf(temp_file, "</response_guide>\n\n");
    }
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
        /* Check allocation immediately and handle failure gracefully. */
        /* Using fatal() normalizes the error path for OOM conditions. */
        if (!processed_files[num_processed_files]) {
            fatal("Out of memory duplicating file path: %s", filepath);
        }
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

        /* Store file path using snprintf for guaranteed null termination. */
        /* This avoids potential truncation issues with strncpy if filepath */
        /* has length >= MAX_PATH - 1. */
        int written = snprintf(new_file.path, sizeof(new_file.path), "%s", filepath);

        /* Check for truncation or encoding errors from snprintf. */
        if (written < 0 || (size_t)written >= sizeof(new_file.path)) {
             fprintf(stderr, "Warning: File path truncated or encoding error for '%s'\n", filepath);
             /* Decide how to handle: skip this file or fatal? Skipping for now. */
             return; /* Skip adding this file to the tree */
        }

        /* Add to file tree array */
            file_tree[file_tree_count] = new_file;
            file_tree_count++;
            /* Post-condition: file tree was updated */
            assert(file_tree_count > 0);
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
    long original_pos = ftell(file);
    /* Check if ftell failed (e.g., on a pipe/socket) */
    if (original_pos == -1) {
        /* Cannot reliably check or rewind; assume not binary or handle error. */
        /* For now, let's assume it's not binary if we can't check. */
        /* Alternatively, could return an error or a specific status. */
        return false;
    }
    bool likely_binary = false;

    /* Read a chunk from the beginning */
    /* No need to rewind here, ftell already gives position. Seek back later. */
    bytes_read = fread(buffer, 1, BINARY_CHECK_SIZE, file);
    /* Check for read error */
    if (ferror(file)) {
        fseek(file, original_pos, SEEK_SET); /* Attempt to restore position */
        return false; /* Treat read error as non-binary for safety */
    }

    if (bytes_read > 0) {
        /* Check for Null Bytes (strongest indicator) and specific non-whitespace */
        /* C0 control codes (0x01-0x08, 0x0B, 0x0C, 0x0E-0x1F). */
        /* This combined check avoids locale issues with isprint() and handles */
        /* common binary file characteristics efficiently. */
        for (size_t i = 0; i < bytes_read; i++) {
            unsigned char c = (unsigned char)buffer[i];
            if (c == '\0') {
                likely_binary = true;
                break; /* Found null byte, definitely binary */
            }
            /* Check for control characters excluding common whitespace (TAB, LF, CR) */
            if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
                 likely_binary = true;
                 break; /* Found suspicious control code */
            }
        }
        /* No need for goto, loop completion handles the non-binary case. */
    }

    /* Restore original file position. Check for fseek error. */
    if (fseek(file, original_pos, SEEK_SET) != 0) {
        /* Failed to restore position, file stream might be compromised. */
        /* Handle appropriately, e.g., log a warning or return an error state. */
        /* For now, proceed but be aware the file position is wrong. */
        fprintf(stderr, "Warning: Failed to restore file position for binary check.\n");
    }

    /* Post-condition: file pointer is restored (best effort) */
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
    printf("  -c TEXT        Add user instruction text wrapped in <user_instructions> tags\n");
    printf("  -c @FILE       Read instruction text from FILE (any bytes)\n");
    printf("  -c @-          Read instruction text from standard input until EOF\n");
    printf("  -C             Shortcut for -c @-. Reads user instructions from stdin\n");
    printf("  -c=\"TEXT\"     Equals form also accepted\n");
    printf("  -s             Use default system prompt (see README for content)\n"); // Keep help concise
    printf("  -s@FILE        Read system prompt from FILE (no space after -s)\n");
    printf("  -s@-           Read system prompt from standard input (no space after -s)\n");
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
        /* Check allocation immediately */
        if (!processed_files[num_processed_files]) {
            fatal("Out of memory duplicating special file name: %s", name);
        }
        num_processed_files++;

        /* Also add to the file tree structure */
// REVIEW: Replaced assert with fatal() call on allocation failure for consistency
// and robustness, normalizing the OOM failure path.
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
             processed_files[num_processed_files] = strdup(filepath);
             /* Check allocation immediately */
             if (!processed_files[num_processed_files]) {
                 /* Use fatal for consistency on OOM */
                 fatal("Out of memory duplicating file path in collect_file: %s", filepath);
                 /* fatal() does not return, but for clarity: */
                 /* return false; */
             }
             num_processed_files++;
             files_found++; // Increment count only for files we will output content for
             return true;
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
    /* Ensure the file context block is opened before writing file content */
    open_file_context_if_needed();

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
    /* FNM_PATHNAME: Makes wildcard '*' not match '/'. */
    /* FNM_PERIOD: Makes wildcard '*' not match a leading '.' in the filename part. */
    /* Include FNM_PERIOD to align with typical shell globbing and .gitignore behavior */
    /* where '*' usually doesn't match hidden files unless explicitly started with '.'. */
    int fnmatch_flags = FNM_PATHNAME | FNM_PERIOD;

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
 * Search upwards from the current directory for the config file.
 * Returns the path to the first found config file (caller must free), or NULL.
 */
char *find_config_file(void) {
    char current_dir[PATH_MAX];
    char config_path[PATH_MAX];
    char parent_dir[PATH_MAX];

    /* Get the current working directory */
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        perror("getcwd() error");
        return NULL;
    }

    /* Loop upwards until root or error */
    while (1) {
        /* Construct the path to the config file in the current directory */
        snprintf(config_path, sizeof(config_path), "%s/.llm_ctx.conf", current_dir);

        /* Check if the file exists and is readable */
        if (access(config_path, R_OK) == 0) {
            /* Found it! Duplicate the path and return */
            char *found_path = strdup(config_path);
            if (!found_path) {
                perror("strdup() error");
                return NULL; /* Allocation failed */
            }
            return found_path;
        }

        /* Check if we are at the root directory */
        if (strcmp(current_dir, "/") == 0) {
            break; /* Reached root, not found */
        }

        /* Get the parent directory path */
        /* Use dirname() on a copy because it can modify the input */
        char *dir_copy = strdup(current_dir);
        if (!dir_copy) { perror("strdup() error"); return NULL; }
        strcpy(parent_dir, dirname(dir_copy));
        free(dir_copy);

        /* Check if dirname returned the same path (e.g., at root or error) */
        if (strcmp(current_dir, parent_dir) == 0) {
            break; /* Stop if path doesn't change */
        }

        /* Update current_dir to the parent directory */
        strcpy(current_dir, parent_dir);
    }

    return NULL; /* Not found */
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

/* Trim leading/trailing whitespace from a string in-place */
static char *trim_whitespace(char *str) {
    char *end;

    /* Trim leading space */
    while (isspace((unsigned char)*str)) str++;

    if (*str == 0) /* All spaces? */
        return str;

    /* Trim trailing space */
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    /* Write new null terminator character */
    end[1] = '\0';

    return str;
}

/* Helper function to finalize a multiline block */
static bool finalize_multiline_block(ConfigSettings *s, char *key, char **buf_ptr, size_t *len_ptr, size_t min_indent __attribute__((unused))) {
    char *buf = *buf_ptr;
    size_t len = *len_ptr;

    /* Drop trailing NL */
    if (len > 0 && buf[len - 1] == '\n') {
        buf[--len] = '\0';
    }

    /* TODO: Implement left-trimming based on min_indent if needed */
    /* For now, just store the collected buffer as is */

    bool success = store_kv(s, key, buf);

    free(buf); /* Free the collected buffer */
    *buf_ptr = NULL;
    *len_ptr = 0;
    return success;
}

/* Parse the configuration file */
bool parse_config_file(const char *config_path, ConfigSettings *settings) {
    FILE *file = fopen(config_path, "r");
    if (!file) {
        /* Don't treat not found as fatal, just return false */
        if (errno == ENOENT) {
            return false;
        }
        /* Other errors (e.g., permission denied) could be warned */
        /* fprintf(stderr, "Warning: Could not open config file %s: %s\n", config_path, strerror(errno)); */
        return false;
    }

    /* State for multiline value collection */
    bool collecting_multiline = false;
    char *pending_key = NULL;
    char *pending_value_buffer = NULL;
    size_t pending_value_cap = 0;
    size_t pending_value_len = 0;
    size_t min_indent = SIZE_MAX; /* Track minimum indentation for multiline */


    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        char *trimmed_line = trim_whitespace(line);

        /* Handle multiline value collection */
        /* ---------- Already inside a multiline value ---------- */
        if (collecting_multiline) {
            /* Blank line (just '\n' or "\r\n") → keep it inside the block      */
            if (line[0] == '\n' || (line[0] == '\r' && line[1] == '\n')) {
                /* Append just a newline */
                if (!APPEND_TO_BUFFER("\n", 1, &pending_value_buffer, &pending_value_len, &pending_value_cap)) {
                    free(pending_key);
                    fclose(file);
                    return false; /* OOM */
                }
                continue; /* Stay in multiline block */
            }


            /* Still indented? keep it in the block - even if it starts with '#' */
            if (line[0] == ' ' || line[0] == '\t') {
                /* Keep original line content, just find length up to newline */
                size_t raw_len = strcspn(line, "\r\n");
                /* Append raw line content + newline */
                if (!APPEND_TO_BUFFER(line, raw_len, &pending_value_buffer, &pending_value_len, &pending_value_cap)) {
                    free(pending_key);
                    fclose(file);
                    return false; /* OOM in APPEND_TO_BUFFER */
                }
                /* Track minimum indentation */
                size_t indent = strspn(line, " \t");
                if (indent < min_indent) {
                    min_indent = indent;
                }
                continue; /* Stay in multiline block */
            } else {
                /* Not indented, multiline value ends here. Finalize and fall through. */
                collecting_multiline = false;
                if (!finalize_multiline_block(settings, pending_key, &pending_value_buffer, &pending_value_len, min_indent)) {
                    /* Handle potential error from finalize/store_kv */
                    free(pending_key);
                    fclose(file);
                    return false;
                }
                free(pending_key);
                pending_key = NULL;
                /* Reset buffer pointers after finalize frees the buffer */
                pending_value_buffer = NULL;
                pending_value_cap = 0;
                pending_value_len = 0;
                /* Fall through to process the current line as a new key/value */
            }
        }

        trimmed_line = trim_whitespace(line); /* Re-trim after potential multiline exit */

        /* Skip empty lines and comments (ONLY when not inside a block) */
        if (trimmed_line[0] == '\0' || trimmed_line[0] == '#') {
            continue;
        }

        /* ---------------- single-line parsing ---------------- */
        char *key = trimmed_line;
        char *value = strpbrk(trimmed_line, "=:"); /* Accept '=' or ':' */

        if (value) {
            *value = '\0'; /* Split key and value */
            value++;       /* Move to start of value */
            key = trim_whitespace(key);
            value = trim_whitespace(value);
            /* Check for multiline start: key = (nothing follows) */
            if (*value == '\0' && !collecting_multiline) {
                collecting_multiline = true;
                pending_key = strdup(key);
                if (!pending_key) { fclose(file); return false; /* OOM */ }

                /* Reset buffer */
                free(pending_value_buffer);
                pending_value_buffer = NULL;
                pending_value_cap = 0;
                pending_value_len = 0;
                min_indent = SIZE_MAX; /* Reset min_indent tracking */

                /* Set flag early if it's system_prompt */
                if (strcmp(key, "system_prompt") == 0) {
                    settings->system_prompt_set = true;
                }
                continue; /* Go to next line to start collecting */
            }

            /* --- Handle single-line values --- */
            /* Strip optional surrounding quotes (only for single-line values) */
            if ((*value == '"'  && value[strlen(value)-1] == '"') ||
                (*value == '\'' && value[strlen(value)-1] == '\'')) {
                value[strlen(value)-1] = '\0'; /* Remove trailing quote */
                value++;                       /* Advance past leading quote */
            }

            /* Store the single-line key-value pair */
            if (!store_kv(settings, key, value)) {
                fclose(file);
                return false; /* OOM in strdup */
            }
        } /* Ignore lines without delimiter for now */
    }

    /* End of file: finalize any pending multiline value */
    if (collecting_multiline) {
        /* Remove last newline if buffer is not empty */
        if (pending_value_len > 0 && pending_value_buffer[pending_value_len - 1] == '\n') {
            pending_value_len--;
            pending_value_buffer[pending_value_len] = '\0';
        }

        if (!finalize_multiline_block(settings, pending_key, &pending_value_buffer, &pending_value_len, min_indent)) {
            /* Handle potential error */
            free(pending_key);
            fclose(file);
            return false;
        }
        free(pending_key);
    }

    fclose(file);
    return true;
}

/**
 * Copy the given buffer content to the system clipboard.
 * Uses platform-specific commands.
 */
void copy_to_clipboard(const char *buffer) {
    const char *cmd = NULL;
    #ifdef __APPLE__
        cmd = "pbcopy";
    #elif defined(__linux__)
        /* Prefer wl-copy if Wayland is likely running */
        if (getenv("WAYLAND_DISPLAY")) {
            cmd = "wl-copy";
        } else {
            /* Fallback to xclip for X11 */
            /* Check if xclip exists? For now, assume it does or popen will fail. */
            cmd = "xclip -selection clipboard";
        }
    #elif defined(_WIN32)
        cmd = "clip.exe";
    #else
        fprintf(stderr, "Warning: Clipboard copy not supported on this platform.\n");
        return;
    #endif

    if (!cmd) { /* Should only happen on Linux if both checks fail */
         fprintf(stderr, "Warning: Could not determine clipboard command on Linux.\n");
         return;
    }

    FILE *pipe = popen(cmd, "w");
    if (!pipe) {
        perror("popen failed for clipboard command");
        return;
    }

    /* Write buffer to the command's stdin */
    fwrite(buffer, 1, strlen(buffer), pipe);

    /* Close the pipe and check status */
    if (pclose(pipe) == -1) {
        perror("pclose failed for clipboard command");
    }
    /* Ignore command exit status for now, focus on pipe errors */
}

/**
 * Cleanup function to free memory before exit
 */
void cleanup(void) {
    if (system_instructions) free(system_instructions);
    /* Free dynamically allocated user instructions */
    if (user_instructions) free(user_instructions);

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


/* Define command-line options for getopt_long */
/* Moved #include <getopt.h> to the top */
static const struct option long_options[] = {
    {"help",            no_argument,       0, 'h'},
    {"command",         required_argument, 0, 'c'}, /* Takes an argument */
    {"system",          optional_argument, 0, 's'}, /* Argument is optional (@file/@-/default) */
    {"files",           no_argument,       0, 'f'}, /* Indicates file args follow */
    {"editor-comments", no_argument,       0, 'e'},
    {"no-gitignore",    no_argument,       0,  1 }, /* Use a value > 255 for long-only */
    {0, 0, 0, 0} /* Terminator */
};
static bool s_flag_used = false; /* Track if -s was used */
static bool c_flag_used = false; /* Track if -c/-C/--command was used */
static bool e_flag_used = false; /* Track if -e was used */
static bool g_stdin_consumed_for_option = false; /* Track if stdin was used for @- */
/* No specific CLI flag for copy yet, so no copy_flag_used needed */



/* Helper to handle argument for -c/--command */
static void handle_command_arg(const char *arg) {
    /* Accept three syntactic forms:
       -cTEXT      (posix short-option glued)
       -c TEXT     (separate argv element)
       -c=TEXT     (equals-form, common in our tests)
     */
    /* getopt_long ensures arg is non-NULL if the option requires an argument */
    /* unless opterr is zero and it returns '?' or ':'. We handle NULL defensively. */
    if (!arg) fatal("Error: -c/--command requires an argument");

    /* Allow and strip the leading '=' for equals-form. */
    if (arg[0] == '=') arg++;

    /* After possible stripping, the argument must be non-empty. */
    if (*arg == '\0')
        fatal("Error: -c/--command requires a non-empty argument");

    c_flag_used = true; /* Track that CLI flag was used */
    if (user_instructions) { /* Free previous if called multiple times */
         free(user_instructions);
         user_instructions = NULL;
    }

    if (arg[0] == '@') {
        /* Reject bare “-c@” (empty path). */
        if (arg[1] == '\0')
            fatal("Error: -c/--command requires a non-empty argument after @");

        if (strcmp(arg, "@-") == 0) { /* read from STDIN */
            if (isatty(STDIN_FILENO)) {
                fprintf(stderr, "Reading instructions from terminal. Enter text and press Ctrl+D when done.\n");
            }
            user_instructions = slurp_stream(stdin);
            g_stdin_consumed_for_option = true; /* Mark stdin as used */
            if (!user_instructions) fatal("Error reading instructions from stdin: %s", ferror(stdin) ? strerror(errno) : "Out of memory");
            file_mode = 1; /* Set file mode globally */
        } else { /* read from file */
            user_instructions = slurp_file(arg + 1);
            if (!user_instructions)
                fatal("Cannot open or read instruction file '%s': %s", arg + 1, strerror(errno));
        }
    } else {
        user_instructions = strdup(arg);
        if (!user_instructions) fatal("Out of memory duplicating -c argument");
    }
}

/* Helper to handle argument for -s/--system */
static void handle_system_arg(const char *arg) {
    if (system_instructions) free(system_instructions); /* Free previous if called multiple times */
    system_instructions = NULL; /* Reset before handling */

    /* Case 1: -s without argument (optarg is NULL) -> Mark flag used, prompt loaded later */
    if (arg == NULL) {
        s_flag_used = true; /* Track that CLI flag was used */
        return; /* Nothing more to do */
    }

    /* Case 2: -s with argument starting with '@' */
    if (arg[0] == '@') {
        if (strcmp(arg, "@-") == 0) { /* Read from stdin */
            if (isatty(STDIN_FILENO)) {
                fprintf(stderr, "Reading system instructions from terminal. Enter text and press Ctrl+D when done.\n");
            }
            system_instructions = slurp_stream(stdin);
            g_stdin_consumed_for_option = true; /* Mark stdin as used */
            if (!system_instructions) fatal("Error reading system instructions from stdin: %s", ferror(stdin) ? strerror(errno) : "Out of memory");
            s_flag_used = true; /* Track that CLI flag was used */
            file_mode = 1; /* Set file mode globally */
        } else { /* Read from file */
            system_instructions = slurp_file(arg + 1); /* skip '@' */
            if (!system_instructions)
                fatal("Cannot open or read system prompt file '%s': %s", arg + 1, strerror(errno));
            s_flag_used = true; /* Track that CLI flag was used */
        }
    } else {
        /* Case 3: -s with an argument not starting with '@' -> Treat as inline text */
        system_instructions = strdup(arg);
        if (!system_instructions) fatal("Out of memory duplicating -s argument");
        s_flag_used = true; /* Track that CLI flag was used */
    }
}


/**
 * Main function - program entry point
 */
int main(int argc, char *argv[]) {
    bool allow_empty_context = false; /* Can we finish with no file content? */
    ConfigSettings loaded_settings; /* Declare here for broader scope */
    /* Register cleanup handler */
    atexit(cleanup); /* Register cleanup handler early */

    /* Create temporary file for output assembly */
    strcpy(temp_file_path, TEMP_FILE_TEMPLATE);
    int fd = mkstemp(temp_file_path);
    if (fd == -1) {
        fatal("Failed to create temporary file: %s", strerror(errno));
    }
    temp_file = fdopen(fd, "w");
    if (!temp_file) {
        /* fd is still open here, cleanup() will handle unlinking */
        fatal("Failed to open temporary file stream: %s", strerror(errno));
    }

    /* Silence getopt_long(); we'll print the expected diagnostic ourselves. */
    opterr = 0;

    /* Use getopt_long for robust and extensible argument parsing. */
    /* This replaces the manual strcmp/strncmp chain, reducing complexity */
    /* and adhering to the "minimize execution paths" principle. */
    int opt;
    /* Add 'C' to the short options string. It takes no argument. */
    while ((opt = getopt_long(argc, argv, "hc:s::feC", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h': /* -h or --help */


                show_help(); /* Exits */
                break;
            case 'c': /* -c or --command */
                handle_command_arg(optarg);
                /* Flag set inside handle_command_arg */
                break;
            case 's': /* -s or --system */
                /*
                 * With "s::" the short‑option optional argument is only captured
                 * when it is glued to the flag. Support the space‑separated
                 * form (-s "foo") by stealing the next argv element when it is
                 * not another option.
                 */
                if (optarg == NULL              /* no glued arg */
                    && optind < argc            /* something left */
                    && argv[optind][0] != '-') {/* not next flag  */
                    handle_system_arg(argv[optind++]); /* treat as arg, consume */
                } else {
                    handle_system_arg(optarg);  /* glued/NULL */
                }
                break;
            case 'f': /* -f or --files */
                file_mode = 1;
                break;
            case 'e': /* -e or --editor-comments */
                want_editor_comments = true;
                e_flag_used = true; /* Track that CLI flag was used */
                /* allow_empty_context = true; // Set later based on final state */
                break;
            case 'C': /* -C (equivalent to -c @-) */
                /* Reuse the existing handler by simulating the @- argument */
                handle_command_arg("@-"); /* Sets c_flag_used */
                break;
            case 1: /* --no-gitignore (long option without short equiv) */
                respect_gitignore = false;
                break;
            case '?': /* Unknown option OR missing required argument */
                /* optopt contains the failing option character */
                if (optopt == 'c') {
                    /* Replicate the standard missing-argument banner */
                    fprintf(stderr, "%s: option requires an argument -- '%c'\n",
                            argv[0], optopt);
                } else if (isprint(optopt)) {
                    /* Handle other unknown options */
                     fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                } else {
                     /* Handle unknown options with non-printable characters */
                     fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                }
                /* Match canonical GNU wording and unit-test expectation:
                 * normalise to "./<basename>" regardless of invocation path. */
                {
                    /* basename() might modify its argument, so pass a copy if needed, */
                    /* but POSIX standard says it *returns* a pointer, possibly static. */
                    /* Use argv[0] directly, providing a default if it's NULL. */
                    char *prog_base = basename(argv[0] ? argv[0] : "llm_ctx");
                    fprintf(stderr, "Try './%s --help' for more information.\n", prog_base);
                }
                return 1; /* Exit with error */
            default:
                /* Should not happen with the defined options */
                fatal("Unexpected option character: %c (ASCII: %d)", opt, opt);
        }
    }
    /* After getopt_long, optind is the index of the first non-option argument. */
    /* These are the file paths/patterns if file_mode is set. */
    int file_args_start = optind;

    /* --- Configuration File Loading --- */
    /* Find config file by searching upwards */
    char *config_path = find_config_file();
    bool config_set_system = false;
    bool config_set_editor = false;

    if (config_path) {
        memset(&loaded_settings, 0, sizeof(ConfigSettings)); /* Initialize before parsing */
        if (parse_config_file(config_path, &loaded_settings)) { /* Parse into the main scope variable */
            /* Merge copy_to_clipboard (no CLI override yet) */
            if (loaded_settings.copy_to_clipboard_set) {
                g_effective_copy_to_clipboard = loaded_settings.copy_to_clipboard;
            }
            /* Merge editor_comments (respect CLI override) */
            if (loaded_settings.editor_comments_set && !e_flag_used) {
                /* Apply config setting only if CLI flag wasn't used */
                want_editor_comments = loaded_settings.editor_comments;
                config_set_editor = true;
                /* Note: e_flag_used remains false here */
            }
            /* Merge system_prompt (respect CLI override) */
            if (loaded_settings.system_prompt_set && !s_flag_used) {
                config_set_system = true; /* Mark that config tried to set it */
                char *source = loaded_settings.system_prompt_source;
                char *new_prompt = NULL;
                bool load_attempted = false; /* Track if we tried loading from config */

                if (source && source[0] == '@') {
                    if (strcmp(source, "@-") == 0) {
                        fprintf(stderr, "Warning: system_prompt=@- in config file is not supported. Ignoring.\n");
                    } else {
                        /* Resolve path relative to config file */
                        char config_dir_buf[PATH_MAX];
                        strncpy(config_dir_buf, config_path, PATH_MAX - 1);
                        config_dir_buf[PATH_MAX - 1] = '\0';
                        char *config_dir = dirname(config_dir_buf);

                        char abs_path[PATH_MAX];
                        snprintf(abs_path, sizeof(abs_path), "%s/%s", config_dir, source + 1);

                        new_prompt = slurp_file(abs_path);
                        load_attempted = true; /* We attempted to load from a file */
                        if (!new_prompt) {
                            fprintf(stderr, "Warning: Cannot read system prompt file '%s' (resolved from '%s' in config): %s. Ignoring.\n", abs_path, source, strerror(errno));
                            /* new_prompt remains NULL, indicating failure */
                        }
                    }
                } else if (source) { /* Inline string */
                    new_prompt = strdup(source);
                    load_attempted = true; /* We attempted to load from inline text */
                    if (!new_prompt) {
                        fprintf(stderr, "Warning: Out of memory duplicating system prompt from config. Ignoring.\n");
                        /* new_prompt is NULL, indicating failure */
                    }
                }

                if (new_prompt) {
                    /* Successfully loaded/duplicated prompt from config */
                    if (system_instructions) free(system_instructions);
                    system_instructions = new_prompt;
                } else if (load_attempted) {
                    /* Attempted to load from config (file or inline) but failed (file not found, OOM) */
                    /* Warning: Config specified a prompt, but loading failed or resulted in empty. */
                    fprintf(stderr, "Warning: system_prompt specified in config file '%s' but could not be loaded (file missing or empty). Ignoring.\n", config_path);
                } /* else: no system_prompt in config, system_instructions remains as set by CLI or NULL */
            }
        }
        free(config_path); /* Free the path returned by find_config_file */
    }

    /* Determine if prompt-only output is allowed based on final settings */
    /* Allow if user instructions were given (-c/-C/--command) */
    /* OR if system instructions were explicitly set (via -s or config) */
    /* OR if editor comments were requested (via -e or config) */
    /* OR if stdin was consumed by an option like -c @- or -s @- */
    allow_empty_context = c_flag_used || s_flag_used || config_set_system || e_flag_used || config_set_editor || g_stdin_consumed_for_option;
    if (config_path && loaded_settings.system_prompt_source) free(loaded_settings.system_prompt_source); /* Free the source string after merging if config was loaded */


    /* Add user instructions first, if provided */
    add_user_instructions(user_instructions);
    /* Add system instructions if provided */
    add_system_instructions(system_instructions);
    /* Add response guide (depends on user instructions and -e flag) */
    add_response_guide(user_instructions);

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
        /* Process files listed after options */
        if (file_args_start >= argc) {
            /* Check if stdin was already consumed by an @- option */
            /* If stdin wasn't used for @- and no files given, it's an error/warning */
            bool used_stdin_for_args = g_stdin_consumed_for_option;
            if (!used_stdin_for_args) {
                 /* -f flag likely provided but no files specified, or only options given */
                 fprintf(stderr, "Warning: File mode specified (-f or via @-) but no file arguments provided.\n");
                 /* Allow proceeding if stdin might have been intended but wasn't used for @- */
                 /* If stdin is not a tty, process it. Otherwise, exit if prompt-only not allowed. */
                 if (isatty(STDIN_FILENO)) {
                     fprintf(stderr, "No input provided.\n");
                     return 1; /* Exit if terminal and no files */
                 } else {
                     /* Fall through to process stdin if data is piped */
                     if (!process_stdin_content()) {
                         return 1; /* Exit on stdin processing error */
                     }
                 }
            }
             /* If stdin *was* used for @-, proceed without file args is okay */

        } else {
            /* Process each remaining argument as a file/pattern */
            for (int i = file_args_start; i < argc; i++) {
                process_pattern(argv[i]);
            }
        }
    } else {
        /* Stdin mode (no -f, no @- used) */
        if (isatty(STDIN_FILENO)) {
            /* If stdin is a terminal and we are not in file mode (which would be set by -f, -c @-, -s @-, -C),
             * and prompt-only output isn't allowed (no -c/-s/-e flags used),
             * it means the user likely forgot to pipe input or provide file arguments. Show help. */
            if (!allow_empty_context) {
                show_help(); /* Exits */
            } /* Otherwise, allow proceeding to generate prompt-only output */

        } else {
            /* Stdin is not a terminal (piped data), process it */
            if (!process_stdin_content()) {
                 return 1; /* Exit on stdin processing error */
            }
        }
    }
    
    /* Check if any files were found or if prompt-only output is allowed */
    if (files_found == 0 && !allow_empty_context) {
        fprintf(stderr, "No files to process\n");
        fclose(temp_file);
        unlink(temp_file_path);
        return 1;
    }

    /* Inform user if producing prompt-only output in interactive mode */
    if (files_found == 0 && allow_empty_context && isatty(STDERR_FILENO)) {
        fprintf(stderr, "llm_ctx: No files or stdin provided; producing prompt-only output.\n");
    }
    
    /* Generate and add file tree */
    generate_file_tree();
    
    /* Add file context header and output content of each file */
    
    /* Output content of each processed file */
    for (int i = 0; i < num_processed_files; i++) {
        output_file_content(processed_files[i], temp_file);
    }
    
    /* Add closing file_context tag */
    if (wrote_file_context) fprintf(temp_file, "</file_context>\n");
    
    /* Flush and close the temp file */
    fclose(temp_file);
    

    /* --- Output Handling --- */
    if (g_effective_copy_to_clipboard) {
        /* Read temp file content */
        char *final_content = slurp_file(temp_file_path);
        if (final_content) {
            copy_to_clipboard(final_content);
            free(final_content);
            /* Print confirmation message to stderr */
            fprintf(stderr, "Content copied to clipboard.\n");
        } else {
            perror("Failed to read temporary file for clipboard");
            /* atexit handler will still attempt cleanup */
            return 1; /* Indicate failure */
        }
        /* Do NOT print to stdout when copying */
    } else {
        /* Default: Display the output directly to stdout */
        char *final_content = slurp_file(temp_file_path);
        if (final_content) {
            printf("%s", final_content); /* Print directly to stdout */
            free(final_content);
        } else {
            perror("Failed to read temporary file for stdout");
            return 1; /* Indicate failure */
        }
    }

    /* Cleanup is handled by atexit handler */
    return 0;
}
