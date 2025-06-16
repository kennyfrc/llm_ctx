
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
#include <math.h>     /* For log() in TF-IDF calculation */
#ifdef __APPLE__
#  include <mach-o/dyld.h> /* _NSGetExecutablePath */
#endif
#include "gitignore.h"
#include "arena.h"
#include "debug.h"
#include "tokenizer.h"
#include "config.h"

static Arena g_arena;

/* Token counting globals */
static size_t g_token_budget = 96000; /* Default budget: 96k tokens */
static const char *g_token_model = "gpt-4o"; /* Default model */
static char *g_token_diagnostics_file = NULL;
static bool g_token_diagnostics_requested = true; /* Diagnostics shown by default */

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

/* FileRank structure for relevance scoring */
typedef struct {
    const char *path;   /* shares memory with processed_files[i] */
    double      score;  /* relevance score */
    size_t      bytes;  /* cached to avoid stat() twice */
    size_t      tokens; /* set later by tokenizer */
} FileRank;

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
void rank_files(const char *query, FileRank *ranks, int num_files);

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

/* Helper function to expand tilde (~) in file paths */
static char *expand_tilde_path(const char *path) {
    if (!path || path[0] != '~') {
        /* No tilde to expand, return a copy of the original path */
        return arena_strdup_safe(&g_arena, path);
    }

    glob_t glob_result;
    int glob_flags = GLOB_TILDE;
    
    /* Use glob to expand the tilde */
    int glob_status = glob(path, glob_flags, NULL, &glob_result);
    if (glob_status != 0) {
        /* glob failed - return NULL and preserve errno */
        if (glob_status == GLOB_NOMATCH) {
            errno = ENOENT; /* File not found */
        } else {
            errno = EINVAL; /* Invalid path */
        }
        return NULL;
    }
    
    /* Check that we got exactly one match */
    if (glob_result.gl_pathc != 1) {
        globfree(&glob_result);
        errno = EINVAL; /* Ambiguous path */
        return NULL;
    }
    
    /* Copy the expanded path */
    char *expanded_path = arena_strdup_safe(&g_arena, glob_result.gl_pathv[0]);
    globfree(&glob_result);
    
    if (!expanded_path) {
        errno = ENOMEM;
        return NULL;
    }
    
    return expanded_path;
}

bool process_stdin_content(void);
void output_file_callback(const char *name, const char *type, const char *content);
bool is_binary(FILE *file);
bool file_already_in_tree(const char *filepath);
void add_directory_tree(const char *base_dir);
void add_directory_tree_with_depth(const char *base_dir, int current_depth);

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
static bool g_filerank_debug = false; /* --filerank-debug flag */
static bool tree_only = false; /* -T flag - filtered tree */
static bool global_tree_only = false; /* -t flag - global tree */
static bool tree_only_output = false; /* -O flag - tree only without file content */
static int tree_max_depth = 4; /* -L flag - default depth of 4 for web dev projects */
/* debug_mode defined in debug.c */

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
    // Only add the guide if -e flag was explicitly used
    if (want_editor_comments) {
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
            fprintf(temp_file, "    2. Return **PR-style code review comments**: use GitHub inline-diff syntax, group notes per file, justify each change, and suggest concrete refactors.\n");
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
 * Recursively add an entire directory tree to file_tree (wrapper)
 */
void add_directory_tree(const char *base_dir) {
    add_directory_tree_with_depth(base_dir, 0);
}

/**
 * Recursively add an entire directory tree to file_tree with depth tracking
 */
void add_directory_tree_with_depth(const char *base_dir, int current_depth) {
    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;
    char path[MAX_PATH];

    /* Check if we've reached max depth */
    if (current_depth >= tree_max_depth) {
        return;
    }


    if (!(dir = opendir(base_dir)))
        return;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        /* Skip hidden files (those starting with a dot), similar to tree's default behavior */
        if (entry->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s", base_dir, entry->d_name);
        if (lstat(path, &statbuf) == -1)
            continue;
        if (respect_gitignore && should_ignore_path(path))
            continue;
        if (!file_already_in_tree(path))
            add_to_file_tree(path);
        if (S_ISDIR(statbuf.st_mode))
            add_directory_tree_with_depth(path, current_depth + 1);
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
    
    /* Check if we've reached max depth for display */
    if (level >= tree_max_depth) {
        return;
    }
    
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
    
    /* Don't remove tree file yet - FileRank might need it later */
    /* Tree file will be cleaned up in cleanup() function */
    
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
    printf("  -s             Enable system prompt from config file\n");
    printf("  -s:TEMPLATE    Use named template for system prompt (no space after -s)\n");
    printf("  -sTEXT         Use TEXT as inline system prompt (no space after -s)\n");
    printf("  -s@FILE        Read system prompt from FILE (no space after -s)\n");
    printf("  -s@-           Read system prompt from standard input (no space after -s)\n");
    printf("  -e             Enable response guide from config file or default PR-style\n");
    printf("  -e:TEMPLATE    Use named template for response guide (no space after -e)\n");
    printf("  -eTEXT         Use TEXT as custom response guide (no space after -e)\n");
    printf("  -e@FILE        Read custom response guide from FILE (no space after -e)\n");
    printf("  -e@-           Read custom response guide from stdin (no space after -e)\n");
    printf("  -r             Raw mode: omit system instructions and response guide\n");
    printf("  -f [FILE...]   Process files instead of stdin content\n");
    printf("  -t             Generate complete directory tree (full tree)\n");
    printf("  -T             Generate file tree only for specified files (filtered tree)\n");
    printf("  -O             Generate tree only (no file content)\n");
    printf("  -L N           Limit tree depth to N levels (default: 4)\n");
    printf("  -o             Output to stdout instead of clipboard\n");
    printf("  -o@FILE        Write output to FILE (no space after -o)\n");
    printf("  -d, --debug    Enable debug output (prefixed with [DEBUG])\n");
    printf("  -h             Show this help message\n");
    printf("  -b N           Set token budget limit (default: 96000, exits with code 3 if exceeded)\n");
    printf("                 Token diagnostics are shown automatically when available\n");
    printf("  --token-budget=N      Set token budget limit (default: 96000)\n");
    printf("  --token-model=MODEL   Set model for token counting (default: gpt-4o)\n");
    printf("  --no-gitignore Ignore .gitignore files when collecting files\n");
    printf("  --ignore-config       Skip loading configuration file\n\n");
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
    printf("  # Generate complete directory tree\n");
    printf("  llm_ctx -t -f src/main.c\n\n");
    printf("  # Generate file tree of specified files only\n");
    printf("  llm_ctx -T -f src/main.c src/utils.c\n\n");
    printf("  # Use named templates from config\n");
    printf("  llm_ctx -s:concise -e:detailed -f src/*.c\n\n");
    printf("  # Mix template with custom instruction\n");
    printf("  llm_ctx -s:architect -c \"Design a cache layer\" -f src/*.c\n\n");
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
    
    /* First check if the pattern is a directory */
    struct stat statbuf;
    if (lstat(pattern, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
        /* It's a directory - add it to tree and recursively find all files */
        add_to_file_tree(pattern);
        find_recursive(pattern, "*");
        return (files_found > initial_files_found);
    }
    
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
    {"tree",            no_argument,       0, 't'}, /* Generate complete directory tree */
    {"filtered-tree",   no_argument,       0, 'T'}, /* Generate file tree for specified files */
    {"tree-only",       no_argument,       0, 'O'}, /* Generate tree only without file content */
    {"level",           required_argument, 0, 'L'}, /* Max depth for tree display */
    {"output",          optional_argument, 0, 'o'}, /* Output to stdout or file instead of clipboard */
    {"stdout",          optional_argument, 0, 'o'}, /* Alias for --output */
    {"debug",           no_argument,       0, 'd'}, /* Enable debug output */
    {"no-gitignore",    no_argument,       0,  1 }, /* Use a value > 255 for long-only */
    {"ignore-config",   no_argument,       0,  2 }, /* Ignore config file */
    {"token-budget",    required_argument, 0, 400}, /* Token budget limit */
    {"token-model",     required_argument, 0, 401}, /* Model for token counting */
    {"token-diagnostics", optional_argument, 0, 402}, /* Token count diagnostics */
    {"filerank-debug",  no_argument,       0, 403}, /* FileRank debug output */
    {0, 0, 0, 0} /* Terminator */
};
static bool s_flag_used = false; /* Track if -s was used */
static bool c_flag_used = false; /* Track if -c/-C/--command was used */
static bool e_flag_used = false; /* Track if -e was used */
static bool r_flag_used = false; /* Track if -r was used */
static bool g_stdin_consumed_for_option = false; /* Track if stdin was used for @- */
static bool ignore_config_flag = false; /* Track if --ignore-config was used */
/* No specific CLI flag for copy yet, so no copy_flag_used needed */
static char *s_template_name = NULL; /* Template name for -s flag */
static char *e_template_name = NULL; /* Template name for -e flag */

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
    s_template_name = NULL; /* Reset template name */

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
            const char *filename = arg + 1; /* skip '@' */
            char *expanded_path = expand_tilde_path(filename);
            if (!expanded_path) {
                fatal("Cannot expand path '%s': %s", filename, strerror(errno));
            }
            system_instructions = slurp_file(expanded_path);
            if (!system_instructions)
                fatal("Cannot open or read system prompt file '%s': %s", expanded_path, strerror(errno));
            s_flag_used = true; /* Track that CLI flag was used */
        }
    } else if (arg[0] == ':' && arg[1] != '\0') {
        /* Case 3: -s:template_name -> Use named template */
        s_template_name = arena_strdup_safe(&g_arena, arg + 1); /* skip ':' */
        if (!s_template_name) fatal("Out of memory duplicating template name");
        s_flag_used = true;
    } else {
        /* Case 4: -s with inline text -> Use as system instructions */
        system_instructions = arena_strdup_safe(&g_arena, arg);
        if (!system_instructions) fatal("Out of memory duplicating -s argument");
        s_flag_used = true; /* Track that CLI flag was used */
    }
}

/* Helper to handle argument for -e/--editor-comments */
static void handle_editor_arg(const char *arg) {
    e_template_name = NULL; /* Reset template name */
    
    /* Case 1: -e without argument -> Mark flag used, guide loaded from config later */
    if (arg == NULL) {
        want_editor_comments = true;
        e_flag_used = true;
        /* Don't set custom_response_guide here - let config loading handle it */
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
            const char *filename = arg + 1; /* skip '@' */
            char *expanded_path = expand_tilde_path(filename);
            if (!expanded_path) {
                fatal("Cannot expand path '%s': %s", filename, strerror(errno));
            }
            custom_response_guide = slurp_file(expanded_path);
            if (!custom_response_guide)
                fatal("Cannot open or read response guide file '%s': %s", expanded_path, strerror(errno));
            want_editor_comments = true;
            e_flag_used = true;
        }
    } else if (arg[0] == ':' && arg[1] != '\0') {
        /* Case 3: -e:template_name -> Use named template */
        e_template_name = arena_strdup_safe(&g_arena, arg + 1); /* skip ':' */
        if (!e_template_name) fatal("Out of memory duplicating template name");
        want_editor_comments = true;
        e_flag_used = true;
    } else {
        /* Case 4: -e with inline text -> Use as custom response guide */
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
 * Check if character is a word boundary
 */
static bool is_word_boundary(char c) {
    return !isalnum(c) && c != '_';
}

/**
 * Case-insensitive word boundary search
 * Returns number of times needle appears as a whole word in haystack
 */
static int count_word_hits(const char *haystack, const char *needle) {
    int count = 0;
    size_t needle_len = strlen(needle);
    const char *pos = haystack;
    
    while ((pos = strcasestr(pos, needle)) != NULL) {
        /* Check word boundaries */
        bool start_ok = (pos == haystack || is_word_boundary(*(pos - 1)));
        bool end_ok = is_word_boundary(*(pos + needle_len));
        
        if (start_ok && end_ok) {
            count++;
        }
        pos++;
    }
    
    return count;
}

/**
 * Tokenize query into lowercase words
 * Returns array of tokens allocated from arena
 */
static char **tokenize_query(const char *query, int *num_tokens) {
    if (!query || !num_tokens) {
        *num_tokens = 0;
        return NULL;
    }
    
    /* Create a working copy */
    size_t query_len = strlen(query);
    char *work = arena_push_array_safe(&g_arena, char, query_len + 1);
    if (!work) {
        *num_tokens = 0;
        return NULL;
    }
    strcpy(work, query);
    
    /* Convert to lowercase */
    for (size_t i = 0; i < query_len; i++) {
        work[i] = tolower((unsigned char)work[i]);
    }
    
    /* Count tokens first */
    int count = 0;
    char *temp = arena_push_array_safe(&g_arena, char, query_len + 1);
    if (!temp) {
        *num_tokens = 0;
        return NULL;
    }
    strcpy(temp, work);
    
    char *token = strtok(temp, " \t\n\r.,;:!?()[]{}\"'`~@#$%^&*+=|\\/<>");
    while (token) {
        count++;
        token = strtok(NULL, " \t\n\r.,;:!?()[]{}\"'`~@#$%^&*+=|\\/<>");
    }
    
    if (count == 0) {
        *num_tokens = 0;
        return NULL;
    }
    
    /* Allocate token array */
    char **tokens = arena_push_array_safe(&g_arena, char*, count);
    if (!tokens) {
        *num_tokens = 0;
        return NULL;
    }
    
    /* Tokenize again to fill array */
    int idx = 0;
    token = strtok(work, " \t\n\r.,;:!?()[]{}\"'`~@#$%^&*+=|\\/<>");
    while (token && idx < count) {
        tokens[idx++] = token;
        token = strtok(NULL, " \t\n\r.,;:!?()[]{}\"'`~@#$%^&*+=|\\/<>");
    }
    
    *num_tokens = idx;
    return tokens;
}

/**
 * Compare FileRank entries by score (descending)
 */
static int compare_filerank(const void *a, const void *b) {
    const FileRank *ra = (const FileRank *)a;
    const FileRank *rb = (const FileRank *)b;
    
    /* Sort by score descending */
    if (ra->score > rb->score) return -1;
    if (ra->score < rb->score) return 1;
    
    /* If scores are equal, maintain original order (stable sort) */
    return 0;
}

/**
 * Implementation of rank_files for Slice 3
 * Uses TF-IDF scoring with path/content hits and size penalty
 * TF-IDF helps prioritize files with unique/rare terms
 */
void rank_files(const char *query, FileRank *ranks, int num_files) {
    /* Parse query into tokens */
    int num_tokens;
    char **tokens = tokenize_query(query, &num_tokens);
    
    if (!tokens || num_tokens == 0) {
        /* No query tokens - all scores remain 0 */
        for (int i = 0; i < num_files; i++) {
            ranks[i].score = 0.0;
        }
        return;
    }
    
    /* Slice 3: First pass - calculate document frequency for each term */
    int *doc_freq = calloc(num_tokens, sizeof(int));
    if (!doc_freq) {
        /* Fallback to basic scoring if allocation fails */
        goto basic_scoring;
    }
    
    for (int i = 0; i < num_files; i++) {
        /* Track which terms appear in this document */
        int *term_found = calloc(num_tokens, sizeof(int));
        if (!term_found) {
            free(doc_freq);
            goto basic_scoring;
        }
        
        /* Check path for terms */
        for (int j = 0; j < num_tokens; j++) {
            if (count_word_hits(ranks[i].path, tokens[j]) > 0) {
                term_found[j] = 1;
            }
        }
        
        /* Check content for terms */
        FILE *f = fopen(ranks[i].path, "r");
        if (f && !is_binary(f)) {
            char buffer[4096];
            rewind(f);
            while (fgets(buffer, sizeof(buffer), f)) {
                for (int j = 0; j < num_tokens; j++) {
                    if (!term_found[j] && count_word_hits(buffer, tokens[j]) > 0) {
                        term_found[j] = 1;
                    }
                }
            }
            fclose(f);
        } else if (f) {
            fclose(f);
        }
        
        /* Update document frequency counts */
        for (int j = 0; j < num_tokens; j++) {
            if (term_found[j]) {
                doc_freq[j]++;
            }
        }
        
        free(term_found);
    }
    
    /* Second pass - calculate TF-IDF scores */
    for (int i = 0; i < num_files; i++) {
        double path_hits = 0;
        double content_hits = 0;
        double tfidf_score = 0;
        
        /* Count hits in path */
        for (int j = 0; j < num_tokens; j++) {
            path_hits += count_word_hits(ranks[i].path, tokens[j]);
        }
        
        /* Get file size for penalty calculation */
        struct stat st;
        if (stat(ranks[i].path, &st) == 0) {
            ranks[i].bytes = st.st_size;
        } else {
            ranks[i].bytes = 0;
        }
        
        /* Read file content to count hits and calculate TF-IDF */
        FILE *f = fopen(ranks[i].path, "r");
        if (f) {
            /* Check if binary */
            if (!is_binary(f)) {
                /* Count total words for TF calculation */
                int total_words = 0;
                int *term_freq = calloc(num_tokens, sizeof(int));
                
                if (term_freq) {
                    /* Read content and count term frequencies */
                    char buffer[4096];
                    char buffer_copy[4096];
                    rewind(f);
                    while (fgets(buffer, sizeof(buffer), f)) {
                        /* Make a copy for word counting since strtok modifies the buffer */
                        strcpy(buffer_copy, buffer);
                        
                        /* Simple word count - count space-separated tokens */
                        char *word = strtok(buffer_copy, " \t\n\r");
                        while (word) {
                            total_words++;
                            word = strtok(NULL, " \t\n\r");
                        }
                        
                        /* Count term hits on original buffer */
                        for (int j = 0; j < num_tokens; j++) {
                            int hits = count_word_hits(buffer, tokens[j]);
                            term_freq[j] += hits;
                            content_hits += hits;
                        }
                    }
                    
                    /* Calculate TF-IDF score */
                    if (total_words > 0) {
                        for (int j = 0; j < num_tokens; j++) {
                            if (term_freq[j] > 0 && doc_freq[j] > 0) {
                                double tf = (double)term_freq[j] / total_words;
                                double idf = log((double)num_files / doc_freq[j]);
                                tfidf_score += tf * idf;
                            }
                        }
                    }
                    
                    free(term_freq);
                } else {
                    /* Fallback to simple counting */
                    char buffer[4096];
                    rewind(f);
                    while (fgets(buffer, sizeof(buffer), f)) {
                        for (int j = 0; j < num_tokens; j++) {
                            content_hits += count_word_hits(buffer, tokens[j]);
                        }
                    }
                }
            }
            fclose(f);
        }
        
        /* Calculate score: TF-IDF weight + content_hits + 2*path_hits - λ*(bytes/1MiB) */
        double size_penalty = 0.05 * (ranks[i].bytes / (1024.0 * 1024.0));
        ranks[i].score = tfidf_score * 10.0 + content_hits + 2.0 * path_hits - size_penalty;
    }
    
    free(doc_freq);
    return;
    
basic_scoring:
    /* Fallback: basic scoring without TF-IDF */
    for (int i = 0; i < num_files; i++) {
        double path_hits = 0;
        double content_hits = 0;
        
        /* Count hits in path */
        for (int j = 0; j < num_tokens; j++) {
            path_hits += count_word_hits(ranks[i].path, tokens[j]);
        }
        
        /* Get file size for penalty calculation */
        struct stat st;
        if (stat(ranks[i].path, &st) == 0) {
            ranks[i].bytes = st.st_size;
        } else {
            ranks[i].bytes = 0;
        }
        
        /* Read file content to count hits */
        FILE *f = fopen(ranks[i].path, "r");
        if (f) {
            /* Check if binary */
            if (!is_binary(f)) {
                /* Read content and count hits */
                char buffer[4096];
                rewind(f);
                while (fgets(buffer, sizeof(buffer), f)) {
                    for (int j = 0; j < num_tokens; j++) {
                        content_hits += count_word_hits(buffer, tokens[j]);
                    }
                }
            }
            fclose(f);
        }
        
        /* Calculate score: content_hits + 2*path_hits - λ*(bytes/1MiB) */
        double size_penalty = 0.05 * (ranks[i].bytes / (1024.0 * 1024.0));
        ranks[i].score = content_hits + 2.0 * path_hits - size_penalty;
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
    
    /* Set executable directory for tokenizer library loading */
    char *exe_dir = get_executable_dir();
    if (exe_dir) {
        llm_set_executable_dir(exe_dir);
    }

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
    while ((opt = getopt_long(argc, argv, "hc:s::fre::CtdTOL:o::b:D::", long_options, NULL)) != -1) {
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
            case 'd': /* -d or --debug */
                debug_mode = true;
                debug_printf("Debug mode enabled");
                break;
            case 't': /* -t or --tree - show full directory tree */
                global_tree_only = true;
                /* tree_only_output is NOT set here - file content should still be shown */
                file_mode = 1; /* Enable file mode to process files */
                break;
            case 'T': /* -T or --filtered-tree - show filtered tree based on params */
                tree_only = true;
                /* tree_only_output is NOT set here - file content should still be shown */
                file_mode = 1; /* Enable file mode to process files */
                break;
            case 'O': /* -O or --tree-only */
                tree_only_output = true;
                /* If neither -t nor -T was specified, default to filtered tree behavior */
                if (!global_tree_only && !tree_only) {
                    tree_only = true;
                }
                file_mode = 1; /* Enable file mode to process files */
                break;
            case 'L': /* -L or --level */
                if (!optarg) {
                    fprintf(stderr, "Error: -L/--level requires a numeric argument\n");
                    return 1;
                }
                tree_max_depth = (int)strtol(optarg, NULL, 10);
                if (tree_max_depth <= 0) {
                    fprintf(stderr, "Error: Invalid tree depth: %s\n", optarg);
                    return 1;
                }
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
            case 2: /* --ignore-config */
                ignore_config_flag = true;
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
            case 'D': /* -D deprecated - diagnostics are now shown by default */
                /* For backward compatibility, just ignore this flag */
                if (optarg == NULL              /* no glued arg */
                    && optind < argc            /* something left */
                    && argv[optind][0] != '-') {/* not next flag  */
                    optind++; /* consume the argument */
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
            case 402: /* --token-diagnostics - deprecated, diagnostics now shown by default */
                /* For backward compatibility, just ignore this flag */
                break;
            case 403: /* --filerank-debug */
                g_filerank_debug = true;
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
    ConfigSettings loaded_settings = {0};
    bool config_loaded = false;
    
    if (!ignore_config_flag) {
        config_loaded = config_load(&loaded_settings, &g_arena);
        if (debug_mode && config_loaded) {
            config_debug_print(&loaded_settings);
        }
    }
    
    /* Apply configuration values if no CLI flags override them */
    if (config_loaded) {
        /* system_prompt_file - only if -s flag was used and no direct content provided */
        if (s_flag_used && !system_instructions) {
            char *prompt_file = NULL;
            
            /* Check if a template name was specified */
            if (s_template_name) {
                ConfigTemplate *tmpl = config_find_template(&loaded_settings, s_template_name);
                if (tmpl && tmpl->system_prompt_file) {
                    prompt_file = tmpl->system_prompt_file;
                } else {
                    fatal("Error: template '%s' not found or has no system_prompt_file in config", s_template_name);
                }
            } else if (loaded_settings.system_prompt_file) {
                /* Use default system prompt file */
                prompt_file = loaded_settings.system_prompt_file;
            }
            
            if (prompt_file) {
                char *expanded_path = config_expand_path(prompt_file, &g_arena);
                if (expanded_path) {
                    system_instructions = slurp_file(expanded_path);
                    if (!system_instructions) {
                        fprintf(stderr, "warning: config refers to %s (not found)\n", expanded_path);
                    }
                }
            }
        }
        
        /* response_guide_file - only if -e flag was used and no direct content provided */
        if (e_flag_used && !custom_response_guide) {
            char *guide_file = NULL;
            
            /* Check if a template name was specified */
            if (e_template_name) {
                ConfigTemplate *tmpl = config_find_template(&loaded_settings, e_template_name);
                if (tmpl && tmpl->response_guide_file) {
                    guide_file = tmpl->response_guide_file;
                } else {
                    fatal("Error: template '%s' not found or has no response_guide_file in config", e_template_name);
                }
            } else if (loaded_settings.response_guide_file) {
                /* Use default response guide file */
                guide_file = loaded_settings.response_guide_file;
            }
            
            if (guide_file) {
                char *expanded_path = config_expand_path(guide_file, &g_arena);
                if (expanded_path) {
                    custom_response_guide = slurp_file(expanded_path);
                    if (!custom_response_guide) {
                        fprintf(stderr, "warning: config refers to %s (not found)\n", expanded_path);
                    } else {
                        want_editor_comments = true; /* Enable editor comments if guide loaded */
                    }
                }
            }
        }
        
        /* copy_to_clipboard - will be applied after all flags are processed */
        
        /* token_budget - only if not set via CLI */
        if (loaded_settings.token_budget > 0 && g_token_budget == 96000) {
            g_token_budget = loaded_settings.token_budget;
        }
    }
    /* --- Finalize copy_to_clipboard setting --- */
    /* Note: g_effective_copy_to_clipboard is already set to false if -o was used */
    /* Apply config setting only if -o was not used */
    if (g_effective_copy_to_clipboard && config_loaded && loaded_settings.copy_to_clipboard != -1) {
        g_effective_copy_to_clipboard = (loaded_settings.copy_to_clipboard == 1);
    }
    
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
    
    /* Expand file tree to show full directory contents */
    if (file_tree_count > 0 && global_tree_only) {
        char *tree_root = find_common_prefix();
        
        /* For full tree, add the entire directory tree from the common root */
        if (strcmp(tree_root, ".") == 0) {
            /* If root is current dir, use the first file's top-level directory */
            if (file_tree_count > 0) {
                char first_dir[MAX_PATH];
                strcpy(first_dir, file_tree[0].path);
                char *first_slash = strchr(first_dir, '/');
                if (first_slash) {
                    *first_slash = '\0';
                    add_directory_tree(first_dir);
                } else {
                    add_directory_tree(".");
                }
            }
        } else {
            /* Add everything under the common root */
            add_directory_tree(tree_root);
        }
    }

    /* Generate and add file tree if -t, -T, or -O flag is passed */
    if (tree_only || global_tree_only || tree_only_output) {
        generate_file_tree();
    }
    
    /* FileRank array - declared at broader scope for budget handling */
    FileRank *ranks = NULL;
    
    /* Process codemap and file content unless tree_only_output is set */
    if (!tree_only_output) {
    
    /* Apply FileRank if we have files and a query */
    if (num_processed_files > 0 && user_instructions) {
        /* Allocate FileRank array */
        ranks = arena_push_array_safe(&g_arena, FileRank, num_processed_files);
        if (!ranks) {
            fatal("Out of memory allocating FileRank array");
        }
        
        /* Initialize FileRank structures */
        for (int i = 0; i < num_processed_files; i++) {
            ranks[i].path = processed_files[i];
            ranks[i].score = 0.0;
            ranks[i].bytes = 0;
            ranks[i].tokens = 0;
        }
        
        /* Call ranking function */
        rank_files(user_instructions, ranks, num_processed_files);
        
        /* Sort files by score (for budget-based selection) */
        qsort(ranks, num_processed_files, sizeof(FileRank), compare_filerank);
        
        /* Debug output if requested - show sorted order */
        if (g_filerank_debug) {
            fprintf(stderr, "FileRank (query: \"%s\")\n", user_instructions);
            for (int i = 0; i < num_processed_files; i++) {
                fprintf(stderr, "  %.2f  %s\n", ranks[i].score, ranks[i].path);
            }
        }
        
        /* Update processed_files array to match sorted order */
        for (int i = 0; i < num_processed_files; i++) {
            processed_files[i] = (char *)ranks[i].path;
        }
    }
    
    /* First pass: try outputting all files */
    for (int i = 0; i < num_processed_files; i++) {
        output_file_content(processed_files[i], temp_file);
    }
    
    /* Add closing file_context tag */
    if (wrote_file_context) fprintf(temp_file, "</file_context>\n");
    }
    
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
            /* Budget exceeded - try FileRank-based selection if we have ranked files */
            if (ranks && user_instructions) {
                fprintf(stderr, "\nBudget exceeded (%zu > %zu) - using FileRank to select most relevant files\n", 
                        total_tokens, g_token_budget);
                fprintf(stderr, "Query: \"%s\"\n", user_instructions);
                
                /* Recreate temp file with only files that fit in budget */
                fclose(fopen(temp_file_path, "w")); /* Truncate file */
                temp_file = fopen(temp_file_path, "w");
                if (!temp_file) {
                    fatal("Failed to reopen temp file for FileRank selection");
                }
                
                /* Write headers and user instructions again */
                if (user_instructions) {
                    fprintf(temp_file, "<user_instructions>\n%s\n</user_instructions>\n\n", user_instructions);
                }
                if (system_instructions) {
                    fprintf(temp_file, "%s\n\n", system_instructions);
                }
                if (custom_response_guide) {
                    fprintf(temp_file, "<response_guide>\n%s\n</response_guide>\n\n", custom_response_guide);
                }
                
                /* Include file tree if it was generated */
                if ((tree_only || global_tree_only) && strlen(tree_file_path) > 0) {
                    FILE *tree_f = fopen(tree_file_path, "r");
                    if (tree_f) {
                        fprintf(temp_file, "<file_tree>\n");
                        char buffer[4096];
                        size_t bytes_read;
                        while ((bytes_read = fread(buffer, 1, sizeof(buffer), tree_f)) > 0) {
                            fwrite(buffer, 1, bytes_read, temp_file);
                        }
                        fprintf(temp_file, "</file_tree>\n\n");
                        fclose(tree_f);
                    }
                }
                
                /* Accumulate files until we would exceed budget */
                size_t running_tokens = 0;
                size_t base_tokens = 0;
                int files_included = 0;
                
                /* Count tokens for the base content (instructions, tree, etc) */
                fflush(temp_file);
                char *base_content = slurp_file(temp_file_path);
                if (base_content) {
                    base_tokens = llm_count_tokens(base_content, g_token_model);
                    if (base_tokens == SIZE_MAX) base_tokens = 0;
                    running_tokens = base_tokens;
                }
                
                /* Open file context */
                fprintf(temp_file, "<file_context>\n\n");
                
                /* Add files in ranked order until budget is exceeded */
                for (int i = 0; i < num_processed_files; i++) {
                    /* Create a temporary buffer to count tokens for this file */
                    size_t mark = arena_get_mark(&g_arena);
                    char *file_content = arena_push_array_safe(&g_arena, char, 1024*1024); /* 1MB buffer */
                    if (!file_content) continue;
                    
                    /* Format file content to buffer */
                    FILE *mem_file = fmemopen(file_content, 1024*1024, "w");
                    if (mem_file) {
                        output_file_content(processed_files[i], mem_file);
                        fclose(mem_file);
                        
                        /* Count tokens for this file */
                        size_t file_tokens = llm_count_tokens(file_content, g_token_model);
                        if (file_tokens != SIZE_MAX) {
                            /* Check if adding this file would exceed budget */
                            if (running_tokens + file_tokens + 50 <= g_token_budget) { /* 50 token buffer for closing tags */
                                /* File fits - add it */
                                fprintf(temp_file, "%s", file_content);
                                running_tokens += file_tokens;
                                files_included++;
                                ranks[i].tokens = file_tokens; /* Store for diagnostics */
                            } else {
                                /* Would exceed budget - stop here */
                                fprintf(stderr, "Skipping remaining %d files - adding '%s' would exceed budget\n", 
                                        num_processed_files - i, processed_files[i]);
                                break;
                            }
                        }
                    }
                    
                    arena_set_mark(&g_arena, mark); /* Reset arena */
                }
                
                /* Close file context */
                fprintf(temp_file, "</file_context>\n");
                fclose(temp_file);
                
                /* Re-read and count final content */
                final_content = slurp_file(temp_file_path);
                if (final_content) {
                    total_tokens = llm_count_tokens(final_content, g_token_model);
                    fprintf(stderr, "\nFileRank selection complete:\n");
                    fprintf(stderr, "  - Selected %d most relevant files out of %d total\n", 
                            files_included, num_processed_files);
                    fprintf(stderr, "  - Token usage: %zu / %zu (%zu%% of budget)\n", 
                            total_tokens, g_token_budget, (total_tokens * 100) / g_token_budget);
                }
            } else {
                /* No FileRank available - provide helpful error message */
                fprintf(stderr, "error: context uses %zu tokens > budget %zu – output rejected\n", 
                        total_tokens, g_token_budget);
                fprintf(stderr, "\nHint: Use -c \"query terms\" to enable FileRank, which will select the most relevant files\n");
                fprintf(stderr, "      that fit within your token budget based on your search query.\n");
                unlink(temp_file_path);
                return 3; /* Exit with status 3 for budget exceeded */
            }
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
                if (tree_only || global_tree_only) {
                    fprintf(stderr, "File tree printed using depth %d.\n", tree_max_depth);
                }
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
