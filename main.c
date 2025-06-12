
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
#ifdef __APPLE__
#  include <mach-o/dyld.h> /* _NSGetExecutablePath */
#endif
#include "gitignore.h"
#include "arena.h"
#include "packs.h"
#include "codemap.h"
#include "debug.h"
#include "tokenizer.h"

static Arena g_arena;

/* Token counting globals */
static size_t g_token_budget = 96000; /* Default budget: 96k tokens */
static const char *g_token_model = "gpt-4o"; /* Default model */
static char *g_token_diagnostics_file = NULL;
static bool g_token_diagnostics_requested = false; /* Track if -D was used */

void cleanup(void);

/* Structure to hold settings parsed directly from the config file */
/* Global flag for effective copy_to_clipboard setting - default is true (clipboard) */
static bool g_effective_copy_to_clipboard = true;
/* Store argv[0] for fallback executable path resolution */
static const char *g_argv0 = NULL;
/* Output file path when using -o with argument */
static char *g_output_file = NULL;

/* Global arena allocator */
/* Reserve 64 MiB for all allocations used by the application */
/* This avoids frequent malloc/free calls and simplifies cleanup. */

/* Initialized in main() */
/* Freed in cleanup() */
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



/**
 * get_executable_dir() – return malloc-ed absolute directory that
 * contains the binary currently running.  Result is cached after the
 * first call (thread-unsafe but main() is single-threaded).
 *
 * WHY: permits a last-chance search for .llm_ctx.conf next to the
 * shipped binary, enabling “copy-anywhere” workflows without polluting
 * $HOME or /etc.  Falls back silently if platform support is missing.
 */
/* Made non-static so it can be used by packs.c to locate language packs */
char *get_executable_dir(void)
{
    static char *cached = NULL;
    if (cached) return cached;

    char pathbuf[PATH_MAX] = {0};
    char resolved_path[PATH_MAX] = {0};
    
#ifdef __linux__
    ssize_t len = readlink("/proc/self/exe", pathbuf, sizeof(pathbuf) - 1);
    if (len <= 0) return NULL; /* Error or empty path */
    pathbuf[len] = '\0';
#elif defined(__APPLE__)
    uint32_t size = sizeof(pathbuf);
    if (_NSGetExecutablePath(pathbuf, &size) != 0)
        return NULL;            /* buffer too small – very unlikely */
#else
    /* POSIX fallback: Use argv[0] */
    if (!g_argv0) return NULL;
    
    /* If argv[0] contains a slash, use it directly */
    if (strchr(g_argv0, '/')) {
        strncpy(pathbuf, g_argv0, sizeof(pathbuf) - 1);
        pathbuf[sizeof(pathbuf) - 1] = '\0';
    } else {
        /* Otherwise, search in PATH */
        fprintf(stderr, "Debug: No absolute path in argv[0], using fallback\n");
        return NULL;
    }
#endif

    /* Resolve symlinks, ., and .. components to get the actual binary location */
    if (!realpath(pathbuf, resolved_path)) {
        fprintf(stderr, "Debug: Failed to resolve real path for '%s': %s\n", 
                pathbuf, strerror(errno));
        return NULL;
    }

    /* Get the directory component */
    char *dir = dirname(resolved_path);  /* modifies in-place */
    
    /* Use arena.h functions to allocate memory from the arena */
    size_t dir_len = strlen(dir) + 1;
    cached = arena_push_array(&g_arena, char, dir_len);
    if (cached) {
        memcpy(cached, dir, dir_len);
    }
    
    return cached;
}

/* ========================= EXISTING CODE ======================= */

#define MAX_PATH 4096
#define BINARY_CHECK_SIZE 1024 /* Bytes to check for binary detection */
#define TEMP_FILE_TEMPLATE "/tmp/llm_ctx_XXXXXX"
#define MAX_PATTERNS 64   /* Maximum number of patterns to support */
#define MAX_FILES 4096    /* Maximum number of files to process */
/* Increased buffer size to 80MB to support larger contexts (10x increase from 8MB) */
#define STDIN_BUFFER_SIZE (80 * 1024 * 1024) /* 80MB buffer for stdin content */
/* Clipboard size limit - most clipboard helpers fail or block beyond 8MB */
#define CLIPBOARD_SOFT_MAX (8 * 1024 * 1024) /* 8MB known safe bound for clipboard */

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
bool copy_to_clipboard(const char *buffer);
bool process_pattern(const char *pattern);
void generate_file_tree(void);
void add_to_file_tree(const char *filepath);
int compare_file_paths(const void *a, const void *b);
char *find_common_prefix(void);
void print_tree_node(const char *path, int level, bool is_last, const char *prefix);
void build_tree_recursive(char **paths, int count, int level, char *prefix, const char *path_prefix);

/* Read entire FILE* into a NUL-terminated buffer.
 * Uses arena allocation. Returns NULL on OOM or read error. */
static char *slurp_stream(FILE *fp) {
    size_t mark = arena_get_mark(&g_arena);
    size_t cap = 4096, len = 0;
    char *buf = arena_push_array_safe(&g_arena, char, cap);
    bool warning_issued = false;

    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (len + 1 >= cap) { /* +1 for the potential NUL terminator */
            /* Check for potential size_t overflow before doubling. */
            if (cap > SIZE_MAX / 2) {
                errno = ENOMEM; /* Indicate memory exhaustion due to overflow */
                return NULL;
            }
            size_t new_cap = cap * 2;
            /* Create a new larger buffer */
            char *tmp = arena_push_array_safe(&g_arena, char, new_cap);
            /* Copy existing content */
            memcpy(tmp, buf, len);
            /* Use the new buffer */
            buf = tmp;
            cap = new_cap;
        }
        buf[len++] = (char)c;
        
        /* Warn once when input exceeds STDIN_BUFFER_SIZE */
        if (!warning_issued && len > STDIN_BUFFER_SIZE) {
            fprintf(stderr, "Warning: Input stream exceeds %d MB. Large inputs may cause clipboard operations to fail.\n", 
                    STDIN_BUFFER_SIZE / (1024 * 1024));
            warning_issued = true;
        }
    }
    /* Ensure buffer is null-terminated *before* checking ferror. */
    buf[len] = '\0';

    /* Check for read errors *after* attempting to read the whole stream. */
    if (ferror(fp)) {
        int saved_errno = errno; /* Preserve errno from the failed I/O operation. */
        arena_set_mark(&g_arena, mark); /* discard */
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
    size_t mark = arena_get_mark(&g_arena);
    char *txt = slurp_stream(fp);
    int slurp_errno = errno; /* Capture errno *after* slurp_stream */

    if (fclose(fp) != 0) {
        /* If fclose fails, prioritize its errno, unless slurp_stream already failed. */
        if (txt != NULL) {
            arena_set_mark(&g_arena, mark);
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
bool process_stdin_content(void);
void output_file_callback(const char *name, const char *type, const char *content);
bool is_binary(FILE *file);
bool file_already_in_tree(const char *filepath);
void add_directory_tree(const char *base_dir);

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
char *user_instructions = NULL;   /* Allocated from arena */
static char *system_instructions = NULL;   /* Allocated from arena */
static bool want_editor_comments = false;   /* -e flag */
static char *custom_response_guide = NULL; /* Custom response guide from -e argument */
static bool raw_mode = false; /* -r flag */
static bool want_codemap = false; /* -m flag */
static bool tree_only = false; /* -t flag */
static bool global_tree_only = false; /* -T flag */
bool debug_mode = false; /* -d flag */
static Codemap g_codemap = {0}; /* Global codemap structure */

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
        
        // If custom response guide provided, use it directly
        if (custom_response_guide && *custom_response_guide) {
            fprintf(temp_file, "%s\n", custom_response_guide);
        } else {
            // Use default behavior
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
        }

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
        processed_files[num_processed_files] = arena_strdup_safe(&g_arena, filepath);
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
    
    /* We don't need to assert file_tree_count < MAX_FILES anymore since we check and handle it */
    
    /* Check if we've hit the file limit */
    if (file_tree_count >= MAX_FILES) {
        /* Log warning only once when we first hit the limit */
        static bool limit_warning_shown = false;
        if (!limit_warning_shown) {
            fprintf(stderr, "Warning: Maximum number of files (%d) exceeded. Some files will not be included in the context.\n", MAX_FILES);
            limit_warning_shown = true;
        }
        return;
    }
    
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
    file_tree[file_tree_count++] = new_file;
    
    assert(file_tree_count > 0);
}

/**
 * Check if a path already exists in the file tree
 */
bool file_already_in_tree(const char *filepath) {
    for (int i = 0; i < file_tree_count; i++) {
        if (strcmp(file_tree[i].path, filepath) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * Recursively add an entire directory tree to file_tree
 */
void add_directory_tree(const char *base_dir) {
    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;
    char path[MAX_PATH];

    if (!(dir = opendir(base_dir)))
        return;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (strcmp(entry->d_name, ".git") == 0)
            continue;
        snprintf(path, sizeof(path), "%s/%s", base_dir, entry->d_name);
        if (lstat(path, &statbuf) == -1)
            continue;
        if (respect_gitignore && should_ignore_path(path))
            continue;
        if (!file_already_in_tree(path))
            add_to_file_tree(path);
        if (S_ISDIR(statbuf.st_mode))
            add_directory_tree(path);
    }

    closedir(dir);
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
        return arena_strdup_safe(&g_arena, ".");
    }
    
    /* Start with the first file's directory */
    char *prefix = arena_strdup_safe(&g_arena, file_tree[0].path);
    char *last_slash = NULL;
    
    /* Find directory part */
    last_slash = strrchr(prefix, '/');
    if (last_slash != NULL) {
        *last_slash = '\0';
    } else {
        /* If no slash, use current directory */
        return arena_strdup_safe(&g_arena, ".");
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
            return arena_strdup_safe(&g_arena, ".");
        }
    }
    
    /* If prefix ends up empty, use current directory */
    if (prefix[0] == '\0') {
        return arena_strdup_safe(&g_arena, ".");
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
                file_tree[i].relative_path = arena_strdup_safe(&g_arena, path + prefix_len + 1);
            } else {
                file_tree[i].relative_path = arena_strdup_safe(&g_arena, path + prefix_len);
            }
        } else {
            file_tree[i].relative_path = arena_strdup_safe(&g_arena, path);
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
    
    /* common_prefix was allocated from the arena; no explicit free needed */
    
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
    
    /* Clear relative path pointers */
    for (int i = 0; i < file_tree_count; i++) {
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
    printf("  -e             Use default PR-style code review response guide\n");
    printf("  -eTEXT         Use TEXT as custom response guide (no space after -e)\n");
    printf("  -e@FILE        Read custom response guide from FILE (no space after -e)\n");
    printf("  -e@-           Read custom response guide from stdin (no space after -e)\n");
    printf("  -r             Raw mode: omit system instructions and response guide\n");
    printf("  -m[=PATTERN]   Generate code map (optionally limited to PATTERN)\n");
    printf("                 Patterns can be comma-separated (e.g., \"src/**/*.js,lib/**/*.rb\")\n");
    printf("                 If no pattern is provided, scans the entire codebase\n");
    printf("  -f [FILE...]   Process files instead of stdin content\n");
    printf("  -t             Generate file tree only for specified files\n");
    printf("  -T             Generate complete directory tree (global tree)\n");
    printf("  -o             Output to stdout instead of clipboard\n");
    printf("  -o@FILE        Write output to FILE (no space after -o)\n");
    printf("  -d, --debug    Enable debug output (prefixed with [DEBUG])\n");
    printf("  -h             Show this help message\n");
    printf("  -b N           Set token budget limit (default: 96000, exits with code 3 if exceeded)\n");
    printf("  -D[FILE]       Generate token count diagnostics (to stderr or FILE)\n");
    printf("  --token-budget=N      Set token budget limit (default: 96000)\n");
    printf("  --token-model=MODEL   Set model for token counting (default: gpt-4o)\n");
    printf("  --token-diagnostics[=FILE]  Generate token diagnostics\n");
    printf("  --list-packs   List available language packs for code map generation\n");
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
    printf("  git diff | llm_ctx -c \"Review these changes\" | pbcopy\n\n");
    printf("  # Generate file tree of specified files only\n");
    printf("  llm_ctx -t -f src/main.c src/utils.c\n\n");
    printf("  # Generate complete directory tree\n");
    printf("  llm_ctx -T -f src/main.c\n\n");
    printf("  # Generate code map from all files (scans entire codebase)\n");
    printf("  llm_ctx -m\n\n");
    printf("  # Generate code map for specific file patterns\n");
    printf("  llm_ctx -m=\"src/**/*.js,lib/**/*.rb\"\n\n");
    printf("  # Generate code map and include file content\n");
    printf("  llm_ctx -f \"src/**/*.ts\" -m\n");
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
            stdin_content_buffer = arena_push_array_safe(&g_arena, char, STDIN_BUFFER_SIZE);
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
                        /* Allocation from arena will be cleaned up globally */
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
    /* Buffer allocated from arena persists until cleanup */

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
    special_files[num_special_files].content = arena_strdup_safe(&g_arena, content);
    
    /* Invariant: memory was allocated successfully */
    assert(special_files[num_special_files].content != NULL);
    
    num_special_files++;
    
    /* Post-condition: file was added */
    assert(num_special_files > 0);
    
    /* Add to processed files list for content output and tree display */
    if (num_processed_files < MAX_FILES) {
        processed_files[num_processed_files] = arena_strdup_safe(&g_arena, name);
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
             processed_files[num_processed_files] = arena_strdup_safe(&g_arena, filepath);
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
 * Copy the given buffer content to the system clipboard.
 * Uses platform-specific commands.
 */
bool copy_to_clipboard(const char *buffer) {
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
        return false;
    #endif

    if (!cmd) { /* Should only happen on Linux if both checks fail */
         fprintf(stderr, "Warning: Could not determine clipboard command on Linux.\n");
         return false;
    }

    FILE *pipe = popen(cmd, "w");
    if (!pipe) {
        perror("popen failed for clipboard command");
        return false;
    }

    /* Write buffer to the command's stdin */
    fwrite(buffer, 1, strlen(buffer), pipe);

    /* Close the pipe and check status */
    if (pclose(pipe) == -1) {
        perror("pclose failed for clipboard command");
        return false;
    }
    /* Ignore command exit status for now, focus on pipe errors */
    return true;
}

/**
 * Cleanup function to free memory before exit
 */
void cleanup(void) {
    /* Clean up language packs */
    cleanup_pack_registry(&g_pack_registry);
    
    /* Arena cleanup will release system_instructions */
    /* Free dynamically allocated user instructions */
    if (user_instructions) {
        /* Pointer memory managed by arena; just reset */
        user_instructions = NULL;
    }

    for (int i = 0; i < num_processed_files; i++) {
        processed_files[i] = NULL;
    }
    
    /* Free special file content */
    for (int i = 0; i < num_special_files; i++) {
        special_files[i].content = NULL;
    }
    
    /* Remove temporary file */
    if (strlen(temp_file_path) > 0) {
        unlink(temp_file_path);
    }
    
    /* Remove tree file if it exists */
    if (strlen(tree_file_path) > 0) {
        unlink(tree_file_path);
    }

    arena_destroy(&g_arena);
}
/* Define command-line options for getopt_long */
/* Moved #include <getopt.h> to the top */
static const struct option long_options[] = {
    {"help",            no_argument,       0, 'h'},
    {"command",         required_argument, 0, 'c'}, /* Takes an argument */
    {"system",          optional_argument, 0, 's'}, /* Argument is optional (@file/@-/default) */
    {"files",           no_argument,       0, 'f'}, /* Indicates file args follow */
    {"editor-comments", optional_argument, 0, 'e'},
    {"raw",             no_argument,       0, 'r'},
    {"codemap",         optional_argument, 0, 'm'}, /* Generate code map with optional pattern */
    {"tree",            no_argument,       0, 't'}, /* Generate file tree only */
    {"global-tree",     no_argument,       0, 'T'}, /* Generate complete directory tree */
    {"output",          optional_argument, 0, 'o'}, /* Output to stdout or file instead of clipboard */
    {"stdout",          optional_argument, 0, 'o'}, /* Alias for --output */
    {"debug",           no_argument,       0, 'd'}, /* Enable debug output */
    {"list-packs",      no_argument,       0,  2 }, /* List available language packs */
    {"pack-info",       required_argument, 0,  3 }, /* Get info about a specific language pack */
    {"no-gitignore",    no_argument,       0,  1 }, /* Use a value > 255 for long-only */
    {"token-budget",    required_argument, 0, 400}, /* Token budget limit */
    {"token-model",     required_argument, 0, 401}, /* Model for token counting */
    {"token-diagnostics", optional_argument, 0, 402}, /* Token count diagnostics */
    {0, 0, 0, 0} /* Terminator */
};
static bool s_flag_used = false; /* Track if -s was used */
static bool c_flag_used = false; /* Track if -c/-C/--command was used */
static bool e_flag_used = false; /* Track if -e was used */
static bool r_flag_used = false; /* Track if -r was used */
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
    if (user_instructions) {
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
        user_instructions = arena_strdup_safe(&g_arena, arg);
        if (!user_instructions) fatal("Out of memory duplicating -c argument");
    }
}

/* Helper to handle argument for -s/--system */
static void handle_system_arg(const char *arg) {
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
        system_instructions = arena_strdup_safe(&g_arena, arg);
        if (!system_instructions) fatal("Out of memory duplicating -s argument");
        s_flag_used = true; /* Track that CLI flag was used */
    }
}

/* Helper to handle argument for -e/--editor-comments */
static void handle_editor_arg(const char *arg) {
    /* Case 1: -e without argument -> Use default PR-style review behavior */
    if (arg == NULL) {
        want_editor_comments = true;
        e_flag_used = true;
        custom_response_guide = NULL; /* Use default behavior */
        return;
    }

    /* Case 2: -e with argument starting with '@' */
    if (arg[0] == '@') {
        if (strcmp(arg, "@-") == 0) { /* Read from stdin */
            if (isatty(STDIN_FILENO)) {
                fprintf(stderr, "Reading custom response guide from terminal. Enter text and press Ctrl+D when done.\n");
            }
            custom_response_guide = slurp_stream(stdin);
            g_stdin_consumed_for_option = true; /* Mark stdin as used */
            if (!custom_response_guide) fatal("Error reading response guide from stdin: %s", ferror(stdin) ? strerror(errno) : "Out of memory");
            want_editor_comments = true;
            e_flag_used = true;
            file_mode = 1; /* Set file mode globally */
        } else { /* Read from file */
            custom_response_guide = slurp_file(arg + 1); /* skip '@' */
            if (!custom_response_guide)
                fatal("Cannot open or read response guide file '%s': %s", arg + 1, strerror(errno));
            want_editor_comments = true;
            e_flag_used = true;
        }
    } else {
        /* Case 3: -e with inline text -> Use as custom response guide */
        custom_response_guide = arena_strdup_safe(&g_arena, arg);
        if (!custom_response_guide) fatal("Out of memory duplicating -e argument");
        want_editor_comments = true;
        e_flag_used = true;
    }
}

/* Helper to handle argument for -o/--output */
static void handle_output_arg(const char *arg) {
    /* Always disable clipboard when -o is used */
    g_effective_copy_to_clipboard = false;
    
    /* Case 1: -o without argument -> Output to stdout */
    if (arg == NULL) {
        g_output_file = NULL; /* stdout is the default */
        return;
    }
    
    /* Case 2: -o@filename -> Output to file */
    if (arg[0] == '@') {
        if (arg[1] == '\0') {
            fatal("Error: -o@ requires a filename after @");
        }
        g_output_file = arena_strdup_safe(&g_arena, arg + 1); /* skip '@' */
        if (!g_output_file) fatal("Out of memory duplicating output filename");
    } else {
        /* Case 3: For backward compatibility, treat non-@ argument as error */
        fatal("Error: -o requires @ prefix for files (e.g., -o@output.txt). Use -o alone for stdout.");
    }
}
/**
 * Main function - program entry point
 */
int main(int argc, char *argv[]) {
    g_argv0 = argv[0]; /* Store for get_executable_dir fallback */
    bool allow_empty_context = false; /* Can we finish with no file content? */
    /* ConfigSettings loaded_settings; */ /* Config loading has been removed */
    /* Register cleanup handler */
    atexit(cleanup); /* Register cleanup handler early */

    g_arena = arena_create(MiB(256));
    if (!g_arena.base) fatal("Failed to allocate arena");

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
    /* Add 'b:' for short form of --token-budget and 'D::' for token diagnostics */
    while ((opt = getopt_long(argc, argv, "hc:s::fre::m::CtdTo::b:D::", long_options, NULL)) != -1) {
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
            case 'e': /* -e or --editor-comments with optional argument */
                /* Handle similar to -s: check if there's a non-option argument following */
                if (optarg == NULL              /* no glued arg */
                    && optind < argc            /* something left */
                    && argv[optind][0] != '-') {/* not next flag  */
                    handle_editor_arg(argv[optind++]); /* treat as arg, consume */
                } else {
                    handle_editor_arg(optarg);  /* glued/NULL */
                }
                break;
            case 'r': /* -r or --raw */
                raw_mode = true;
                r_flag_used = true;
                break;
            case 'm': /* -m or --codemap */
                want_codemap = true;
                
                // Initialize codemap if not already done
                if (!g_codemap.files) {
                    g_codemap = codemap_init(&g_arena);
                }
                
                // Handle both -m and -m=pattern forms
                // The -m form has optarg == NULL
                // The -m=pattern form has optarg starting with '='
                // The -m pattern form has optarg set to the pattern
                
                if (optarg) {
                    // Skip leading '=' for -m="pattern" form
                    if (*optarg == '=') {
                        optarg++;
                    }
                    
                    if (*optarg) { // Make sure there's still something after skipping '='
                        // Split the pattern string by commas
                        char *pattern_copy = arena_strdup_safe(&g_arena, optarg);
                        if (!pattern_copy) {
                            fprintf(stderr, "Error: Failed to allocate memory for pattern\n");
                        } else {
                            // Check if the pattern contains brace expansion
                            if (strchr(pattern_copy, '{') && strchr(pattern_copy, '}')) {
                                // If it contains braces, don't split at commas
                                char *trimmed = pattern_copy;
                                while (*trimmed && isspace(*trimmed)) trimmed++;
                                char *end = trimmed + strlen(trimmed) - 1;
                                while (end > trimmed && isspace(*end)) *end-- = '\0';
                                
                                if (*trimmed) {
                                    // Add the whole pattern
                                    codemap_add_pattern(&g_codemap, trimmed, &g_arena);
                                }
                            } else {
                                // If it doesn't contain braces, split at commas
                                char *token = strtok(pattern_copy, ",");
                                while (token) {
                                    // Trim whitespace
                                    char *trimmed = token;
                                    while (*trimmed && isspace(*trimmed)) trimmed++;
                                    char *end = trimmed + strlen(trimmed) - 1;
                                    while (end > trimmed && isspace(*end)) *end-- = '\0';
                                    
                                    if (*trimmed) {
                                        // Add the pattern to the codemap
                                        codemap_add_pattern(&g_codemap, trimmed, &g_arena);
                                    }
                                    token = strtok(NULL, ",");
                                }
                            }
                        }
                    }
                }
                
                // For brace patterns, provide a more specific message
                if (g_codemap.pattern_count > 0 && strstr(g_codemap.patterns[0], "{")) {
                    debug_printf("Codemap option enabled - will use brace pattern: %s", g_codemap.patterns[0]);
                } else {
                    debug_printf("Codemap option enabled - will %s", 
                           (g_codemap.pattern_count > 0) ? "use specified patterns" : "scan entire codebase");
                }
                break;
            case 'd': /* -d or --debug */
                debug_mode = true;
                debug_printf("Debug mode enabled");
                break;
            case 't': /* -t or --tree */
                tree_only = true;
                file_mode = 1; /* Enable file mode to process files */
                break;
            case 'T': /* -T or --global-tree */
                global_tree_only = true;
                file_mode = 1; /* Enable file mode to process files */
                break;
            case 'o': /* -o or --output with optional file argument */
                /* Handle similar to -s and -e: check if there's a non-option argument following */
                if (optarg == NULL              /* no glued arg */
                    && optind < argc            /* something left */
                    && argv[optind][0] != '-') {/* not next flag  */
                    handle_output_arg(argv[optind++]); /* treat as arg, consume */
                } else {
                    handle_output_arg(optarg);  /* glued/NULL */
                }
                break;
            case 'C': /* -C (equivalent to -c @-) */
                /* Reuse the existing handler by simulating the @- argument */
                handle_command_arg("@-"); /* Sets c_flag_used */
                break;
            case 1: /* --no-gitignore (long option without short equiv) */
                respect_gitignore = false;
                break;
            case 2: /* --list-packs */
                {
                    if (initialize_pack_registry(&g_pack_registry, &g_arena)) {
                        size_t loaded = load_language_packs(&g_pack_registry);
                        printf("Loaded %zu language pack(s).\n\n", loaded);
                        print_pack_list(&g_pack_registry);
                    } else {
                        printf("No language packs found.\n");
                    }
                    cleanup(); /* Clean up and exit */
                    exit(0);
                }
                break;
            case 3: /* --pack-info */
                {
                    if (!optarg) {
                        fprintf(stderr, "Error: --pack-info requires a language pack name\n");
                        return 1;
                    }
                    
                    if (initialize_pack_registry(&g_pack_registry, &g_arena)) {
                        load_language_packs(&g_pack_registry);
                        
                        bool found = false;
                        for (size_t i = 0; i < g_pack_registry.pack_count; i++) {
                            LanguagePack *pack = &g_pack_registry.packs[i];
                            if (strcmp(pack->name, optarg) == 0) {
                                found = true;
                                
                                printf("Language Pack: %s\n", pack->name);
                                printf("Status: %s\n", pack->available ? "Available" : "Unavailable");
                                printf("Path: %s\n", pack->path);
                                
                                if (pack->extensions && pack->extension_count > 0) {
                                    printf("Supported Extensions:\n");
                                    for (size_t j = 0; j < pack->extension_count; j++) {
                                        printf("  %s\n", pack->extensions[j]);
                                    }
                                } else {
                                    printf("No extensions defined\n");
                                }
                                
                                break;
                            }
                        }
                        
                        if (!found) {
                            printf("Language pack '%s' not found.\n", optarg);
                        }
                    } else {
                        printf("No language packs found.\n");
                    }
                    
                    cleanup(); /* Clean up and exit */
                    exit(0);
                }
                break;
            case 'b': /* -b or --token-budget */
                if (!optarg) {
                    fprintf(stderr, "Error: -b/--token-budget requires a numeric argument\n");
                    return 1;
                }
                g_token_budget = (size_t)strtoull(optarg, NULL, 10);
                if (g_token_budget == 0) {
                    fprintf(stderr, "Error: Invalid token budget: %s\n", optarg);
                    return 1;
                }
                break;
            case 'D': /* -D or --token-diagnostics with optional file argument */
                g_token_diagnostics_requested = true;
                /* Handle similar to -s and -e: check if there's a non-option argument following */
                if (optarg == NULL              /* no glued arg */
                    && optind < argc            /* something left */
                    && argv[optind][0] != '-') {/* not next flag  */
                    g_token_diagnostics_file = arena_strdup_safe(&g_arena, argv[optind++]);
                } else if (optarg) {
                    g_token_diagnostics_file = arena_strdup_safe(&g_arena, optarg);
                } else {
                    g_token_diagnostics_file = NULL; /* Write to stderr */
                }
                break;
            case 400: /* --token-budget */
                if (!optarg) {
                    fprintf(stderr, "Error: --token-budget requires a numeric argument\n");
                    return 1;
                }
                g_token_budget = (size_t)strtoull(optarg, NULL, 10);
                if (g_token_budget == 0) {
                    fprintf(stderr, "Error: Invalid token budget: %s\n", optarg);
                    return 1;
                }
                break;
            case 401: /* --token-model */
                if (!optarg) {
                    fprintf(stderr, "Error: --token-model requires a model name\n");
                    return 1;
                }
                g_token_model = arena_strdup_safe(&g_arena, optarg);
                break;
            case 402: /* --token-diagnostics */
                g_token_diagnostics_requested = true;
                if (optarg) {
                    g_token_diagnostics_file = arena_strdup_safe(&g_arena, optarg);
                } else {
                    g_token_diagnostics_file = NULL; /* Write to stderr */
                }
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
    /* Configuration file loading has been removed */
    /* --- Finalize editor_comments setting (apply toggle logic) --- */
    /* Determine initial state (default false) */
    bool initial_want_editor_comments = false;
    /* Apply toggle if -e flag was used */
    if (e_flag_used) {
        want_editor_comments = !initial_want_editor_comments;
    } else {
        want_editor_comments = initial_want_editor_comments;
    }

    /* Determine if prompt-only output is allowed based on final settings */
    /* Allow if user instructions were given (-c/-C/--command) */
    /* OR if system instructions were explicitly set (via -s or config) */
    /* OR if editor comments were requested (via -e) */
    /* OR if stdin was consumed by an option like -c @- or -s @- */
    allow_empty_context = c_flag_used || s_flag_used || e_flag_used || g_stdin_consumed_for_option;
    if (!raw_mode) {
        /* Add user instructions first, if provided */
        add_user_instructions(user_instructions);
        /* Add system instructions if provided */
        add_system_instructions(system_instructions);
        /* Add response guide (depends on user instructions and -e flag) */
        add_response_guide(user_instructions);
    } else if (user_instructions && *user_instructions) {
        /* In raw mode, just print user instructions without tags */
        fprintf(temp_file, "%s\n\n", user_instructions);
    }

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
             * and prompt-only output isn't allowed (no -c/-s/-e flags used) and codemap isn't requested,
             * it means the user likely forgot to pipe input or provide file arguments. Show help. */
            if (!allow_empty_context && !want_codemap) {
                show_help(); /* Exits */
            } /* Otherwise, allow proceeding to generate prompt-only output or codemap */

        } else {
            /* Stdin is not a terminal (piped data), process it */
            if (!process_stdin_content()) {
                 return 1; /* Exit on stdin processing error */
            }
        }
    }
    
    /* Check if any files were found or if codemap/prompt-only output is allowed */
    if (files_found == 0 && !allow_empty_context && !want_codemap) {
        fprintf(stderr, "No files to process\n");
        fclose(temp_file);
        unlink(temp_file_path);
        return 1;
    }

    /* Inform user if producing prompt-only output in interactive mode */
    if (files_found == 0 && allow_empty_context && isatty(STDERR_FILENO)) {
        fprintf(stderr, "llm_ctx: No files or stdin provided; producing prompt-only output.\n");
    }
    
    /* Expand file tree to show full directory contents */
    if (file_tree_count > 0 && (global_tree_only || !tree_only)) {
        char *tree_root = find_common_prefix();
        add_directory_tree(tree_root);
    }

    /* Generate and add file tree */
    if (tree_only || global_tree_only) {
        /* For -t/-T flags, only generate the file tree */
        generate_file_tree();
    } else {
        /* Normal mode: generate tree along with other content */
        generate_file_tree();
    }
    
    /* Skip codemap and file content if tree_only or global_tree_only is set */
    if (!tree_only && !global_tree_only) {
        /* Generate and output codemap first (before file context) if requested */
        if (want_codemap) {
        debug_printf("Generating codemap...");
        
        /* We don't need to initialize the codemap here since it was already done when processing 
         * the -m flag. Preserving the existing patterns is important */
        
        /* If in file mode and no patterns specified, add processed files as patterns */
        if (file_mode && g_codemap.pattern_count == 0 && num_processed_files > 0) {
            debug_printf("Adding %d processed files as codemap patterns", num_processed_files);
            for (int i = 0; i < num_processed_files; i++) {
                if (processed_files[i]) {
                    codemap_add_pattern(&g_codemap, processed_files[i], &g_arena);
                }
            }
        }
        
        
        /* If no patterns are defined at this point, add a default pattern to scan the entire codebase */
        if (g_codemap.pattern_count == 0) {
            debug_printf("No patterns specified, scanning entire codebase");
            codemap_add_pattern(&g_codemap, ".", &g_arena);
        }
        
        /* Use the pattern-based approach to generate the codemap */
        if (generate_codemap_from_patterns(&g_codemap, &g_arena, respect_gitignore)) {
            /* Generate codemap and write to temp_file */
            char *buffer = arena_push_array_safe(&g_arena, char, 1024 * 1024); /* 1MB buffer */
            if (buffer) {
                size_t pos = 0;
                codemap_generate(&g_codemap, buffer, &pos, 1024 * 1024);
                
                /* Write the buffer to the output file */
                if (pos > 0) {
                    fprintf(temp_file, "%.*s\n", (int)pos, buffer);
                    debug_printf("Codemap generated successfully");
                } else {
                    fprintf(stderr, "Warning: Empty codemap generated\n");
                }
            } else {
                fprintf(stderr, "Warning: Failed to allocate memory for codemap output\n");
            }
        } else {
            fprintf(stderr, "Warning: Failed to generate codemap\n");
            /* Do not fallback to JavaScript/TypeScript-only codemap as requested */
        }
    }
    
    /* Output content of each processed file */
    for (int i = 0; i < num_processed_files; i++) {
        output_file_content(processed_files[i], temp_file);
    }
    
    /* Add closing file_context tag */
    if (wrote_file_context) fprintf(temp_file, "</file_context>\n");
    } /* End of if (!tree_only && !global_tree_only) */
    
    /* Flush and close the temp file */
    fclose(temp_file);
    
    /* --- Token Counting and Budget Check --- */
    /* Read the content first to count tokens if needed */
    char *final_content = slurp_file(temp_file_path);
    if (!final_content) {
        perror("Failed to read temporary file");
        return 1;
    }
    
    /* Always try to count tokens if tokenizer is available */
    size_t total_tokens = llm_count_tokens(final_content, g_token_model);
    
    if (total_tokens != SIZE_MAX) {
        /* Token counting succeeded - always display usage */
        fprintf(stderr, "Token usage: %zu / %zu (%zu%% of budget)\n", 
                total_tokens, g_token_budget, (total_tokens * 100) / g_token_budget);
        
        /* Check budget */
        if (total_tokens > g_token_budget) {
            /* Budget exceeded */
            fprintf(stderr, "error: context uses %zu tokens > budget %zu – output rejected\n", 
                    total_tokens, g_token_budget);
            unlink(temp_file_path);
            return 3; /* Exit with status 3 for budget exceeded */
        }
        
        /* Generate diagnostics if requested */
        if (g_token_diagnostics_requested) {
                FILE *diag_out = stderr;
                if (g_token_diagnostics_file) {
                    diag_out = fopen(g_token_diagnostics_file, "w");
                    if (!diag_out) {
                        perror("Failed to open diagnostics file");
                        diag_out = stderr;
                    }
                }
                
                /* Use the detailed diagnostics function */
                generate_token_diagnostics(final_content, g_token_model, diag_out, &g_arena);
                
                if (diag_out != stderr) {
                    fclose(diag_out);
                }
        }
    }

    /* --- Output Handling --- */
    if (g_effective_copy_to_clipboard) {
        /* final_content already read above */
        if (final_content) {
            size_t final_len = strlen(final_content);
            /* Check if content exceeds clipboard limit */
            if (final_len > CLIPBOARD_SOFT_MAX) {
                fprintf(stderr, "Warning: output (%zu bytes) exceeds clipboard limit (%d MB); "
                    "writing to stdout instead.\n", final_len, CLIPBOARD_SOFT_MAX / (1024 * 1024));
                printf("%s", final_content);
            } else if (!copy_to_clipboard(final_content)) {
                /* Clipboard copy failed, fall back to stdout */
                fprintf(stderr, "Clipboard copy failed; falling back to stdout.\n");
                printf("%s", final_content);
            } else {
                /* Print confirmation message to stderr */
                fprintf(stderr, "Content copied to clipboard.\n");
            }
        }
        /* Do NOT print to stdout when copying succeeded */
    } else if (g_output_file) {
        /* Output to specified file */
        /* final_content already read above */
        if (final_content) {
            FILE *output_file = fopen(g_output_file, "w");
            if (output_file) {
                if (fwrite(final_content, 1, strlen(final_content), output_file) != strlen(final_content)) {
                    perror("Failed to write to output file");
                    fclose(output_file);
                    return 1;
                }
                fclose(output_file);
                fprintf(stderr, "Content written to %s\n", g_output_file);
            } else {
                perror("Failed to open output file");
                return 1;
            }
        }
    } else {
        /* Default: Display the output directly to stdout */
        /* final_content already read above */
        if (final_content) {
            printf("%s", final_content); /* Print directly to stdout */
        }
    }

    /* Cleanup is handled by atexit handler */
    return 0;
}
