
/* Extract file content with fenced blocks for LLM context */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glob.h>
#include <libgen.h>
#include <errno.h>
#include <fnmatch.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>
#include <getopt.h>
#include <math.h>
#include <float.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include "gitignore.h"
#include "arena.h"
#include "debug.h"
#include "tokenizer.h"
#include "config.h"

static Arena g_arena;

static size_t g_token_budget = 96000;
static const char* g_token_model = "gpt-4o";
static char* g_token_diagnostics_file = NULL;
static bool g_token_diagnostics_requested = true;

typedef struct
{
    char* path;
    int start_line; /* 1-based; 0 means beginning */
    int end_line;   /* 1-based inclusive; 0 means end of file */
} ProcessedFile;

void cleanup(void);

static bool g_effective_copy_to_clipboard = true;
static const char* g_argv0 = NULL;
static char* g_output_file = NULL;

#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <pwd.h>

__attribute__((noreturn)) static void fatal(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    cleanup();
    _Exit(EXIT_FAILURE);
}


static char* generate_prompt_uuid(Arena* arena)
{
    struct timeval tv;
    struct tm* tm_info;
    
    if (gettimeofday(&tv, NULL) != 0)
    {
        return NULL;
    }
    
    tm_info = localtime(&tv.tv_sec);
    if (!tm_info)
    {
        return NULL;
    }
    
    size_t uuid_len = 23;
    char* uuid = arena_push_array_safe(arena, char, uuid_len);
    if (!uuid)
    {
        return NULL;
    }
    
    strftime(uuid, uuid_len, "%Y%m%d-%H%M%S-", tm_info);
    
    unsigned int seed = (unsigned int)(tv.tv_usec ^ getpid() ^ tv.tv_sec);
    const char* chars = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    
    for (int i = 0; i < 6; i++)
    {
        seed = seed * 1103515245 + 12345;
        uuid[16 + i] = chars[seed % 62];
    }
    uuid[22] = '\0';
    
    return uuid;
}


static char* ensure_prompts_dir(Arena* arena)
{
    char config_base[MAX_PATH];
    const char* home = getenv("HOME");
    const char* xdg_config = getenv("XDG_CONFIG_HOME");
    
    if (xdg_config)
    {
        snprintf(config_base, sizeof(config_base), "%s/llm_ctx", xdg_config);
    }
    else if (home)
    {
        snprintf(config_base, sizeof(config_base), "%s/.config/llm_ctx", home);
    }
    else
    {
        struct passwd* pw = getpwuid(getuid());
        if (!pw || !pw->pw_dir)
        {
            return NULL;
        }
        snprintf(config_base, sizeof(config_base), "%s/.config/llm_ctx", pw->pw_dir);
    }
    
    char prompts_dir[MAX_PATH];
    snprintf(prompts_dir, sizeof(prompts_dir), "%s/prompts", config_base);
    
    struct stat st;
    if (stat(prompts_dir, &st) != 0)
    {
        if (mkdir(prompts_dir, 0755) != 0)
        {
            if (mkdir(config_base, 0755) != 0 && errno != EEXIST)
            {
                fprintf(stderr, "Warning: Could not create config directory %s: %s\n", 
                        config_base, strerror(errno));
                return NULL;
            }
            
            if (mkdir(prompts_dir, 0755) != 0)
            {
                fprintf(stderr, "Warning: Could not create prompts directory %s: %s\n", 
                        prompts_dir, strerror(errno));
                return NULL;
            }
        }
    }
    else if (!S_ISDIR(st.st_mode))
    {
        fprintf(stderr, "Warning: %s exists but is not a directory\n", prompts_dir);
        return NULL;
    }
    
    return arena_strdup_safe(arena, prompts_dir);
}

static char* slurp_stream(FILE* fp);


static char* save_prompt(const char* content, int argc, char* argv[],
                        ProcessedFile* processed_files, int num_processed_files,
                        Arena* arena)
{
    if (!content || !arena)
    {
        return NULL;
    }

    char* prompts_dir = ensure_prompts_dir(arena);
    if (!prompts_dir)
    {
        return NULL;
    }

    char* uuid = generate_prompt_uuid(arena);
    if (!uuid)
    {
        return NULL;
    }

    char prompt_path[MAX_PATH];
    snprintf(prompt_path, sizeof(prompt_path), "%s/%s", prompts_dir, uuid);
    
    FILE* fp = fopen(prompt_path, "w");
    if (!fp)
    {
        fprintf(stderr, "Warning: Could not save prompt to %s: %s\n", 
                prompt_path, strerror(errno));
        return NULL;
    }

    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S UTC", gmtime(&now));

    fprintf(fp, "# llm_ctx saved prompt\n");
    fprintf(fp, "# UUID: %s\n", uuid);
    fprintf(fp, "# Saved: %s\n", timestamp);
    
    fprintf(fp, "# CLI:");
    for (int i = 0; i < argc; i++)
    {
        fprintf(fp, " %s", argv[i]);
    }
    fprintf(fp, "\n");
    
    fprintf(fp, "# Files: %d files processed\n", num_processed_files);
    fprintf(fp, "#\n");
    
    if (num_processed_files > 0 && processed_files)
    {
        fprintf(fp, "<file_list>\n");
        for (int i = 0; i < num_processed_files; i++)
        {
            if (processed_files[i].start_line > 0 || processed_files[i].end_line > 0)
            {
                if (processed_files[i].end_line > 0)
                {
                    fprintf(fp, "%s:%d-%d\n", processed_files[i].path, processed_files[i].start_line,
                            processed_files[i].end_line);
                }
                else
                {
                    fprintf(fp, "%s:%d-\n", processed_files[i].path, processed_files[i].start_line);
                }
            }
            else
            {
                fprintf(fp, "%s\n", processed_files[i].path);
            }
        }
        fprintf(fp, "</file_list>\n");
        fprintf(fp, "\n");
    }
    
    if (fprintf(fp, "%s", content) < 0)
    {
        fprintf(stderr, "Warning: Failed to write prompt content to %s: %s\n", 
                prompt_path, strerror(errno));
        fclose(fp);
        return NULL;
    }
    
    if (fclose(fp) != 0)
    {
        fprintf(stderr, "Warning: Error closing prompt file %s: %s\n", 
                prompt_path, strerror(errno));
        return NULL;
    }

    return uuid;
}


static char* load_prompt(const char* uuid, Arena* arena)
{
    if (!uuid || !arena)
    {
        return NULL;
    }

    size_t uuid_len = strlen(uuid);
    if (uuid_len < 16 || uuid_len > 64)
    {
        return NULL;
    }
    
    for (size_t i = 0; i < uuid_len; i++)
    {
        char c = uuid[i];
        if (!(isalnum(c) || c == '-'))
        {
            return NULL;
        }
    }

    char* prompts_dir = ensure_prompts_dir(arena);
    if (!prompts_dir)
    {
        return NULL;
    }

    char prompt_path[MAX_PATH];
    snprintf(prompt_path, sizeof(prompt_path), "%s/%s", prompts_dir, uuid);

    struct stat st;
    if (stat(prompt_path, &st) != 0 || !S_ISREG(st.st_mode))
    {
        return NULL;
    }

    FILE* fp = fopen(prompt_path, "r");
    if (!fp)
    {
        return NULL;
    }

    char line[MAX_PATH];
    long content_start = 0;
    
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        if (line[0] != '#')
        {
            content_start = ftell(fp) - strlen(line);
            break;
        }
    }

    if (content_start == 0)
    {
        fclose(fp);
        return NULL;
    }

    fseek(fp, content_start, SEEK_SET);

    char* content = slurp_stream(fp);
    fclose(fp);

    return content;
}

/* WHY: enables copy-anywhere workflows by searching for .llm_ctx.conf
   next to the binary, avoiding $HOME/etc pollution. */
char* get_executable_dir(void)
{
    static char* cached = NULL;
    if (cached)
        return cached;

    char pathbuf[PATH_MAX] = {0};
    char resolved_path[PATH_MAX] = {0};

#ifdef __linux__
    ssize_t len = readlink("/proc/self/exe", pathbuf, sizeof(pathbuf) - 1);
    if (len <= 0)
        return NULL;
    pathbuf[len] = '\0';
#elif defined(__APPLE__)
    uint32_t size = sizeof(pathbuf);
    if (_NSGetExecutablePath(pathbuf, &size) != 0)
        return NULL;
#else
    if (!g_argv0)
        return NULL;

    if (strchr(g_argv0, '/'))
    {
        strncpy(pathbuf, g_argv0, sizeof(pathbuf) - 1);
        pathbuf[sizeof(pathbuf) - 1] = '\0';
    }
    else
    {
        fprintf(stderr, "Debug: No absolute path in argv[0], using fallback\n");
        return NULL;
    }
#endif

    if (!realpath(pathbuf, resolved_path))
    {
        fprintf(stderr, "Debug: Failed to resolve real path for '%s': %s\n", pathbuf,
                strerror(errno));
        return NULL;
    }

    char* dir = dirname(resolved_path);

    size_t dir_len = strlen(dir) + 1;
    cached = arena_push_array(&g_arena, char, dir_len);
    if (cached)
    {
        memcpy(cached, dir, dir_len);
    }

    return cached;
}



#define MAX_PATH 4096
#define BINARY_CHECK_SIZE 1024
#define TEMP_FILE_TEMPLATE "/tmp/llm_ctx_XXXXXX"
#define MAX_PATTERNS 64
#define MAX_FILES 4096
#define STDIN_BUFFER_SIZE (80 * 1024 * 1024)
#define CLIPBOARD_SOFT_MAX (8 * 1024 * 1024)

typedef struct
{
    char path[MAX_PATH];
    char* relative_path;
    bool is_dir;
} FileInfo;

typedef struct
{
    const char* path;
    double score;
    size_t bytes;
    size_t tokens;
} FileRank;


void show_help(void);
bool collect_file(const char* filepath, int start_line, int end_line);
bool output_file_content(const ProcessedFile* file, FILE* output);
void add_user_instructions(const char* instructions);
void find_recursive(const char* base_dir, const char* pattern);
bool copy_to_clipboard(const char* buffer);
bool process_pattern(const char* pattern);
void generate_file_tree(void);
void add_to_file_tree(const char* filepath);
int compare_file_paths(const void* a, const void* b);
char* find_common_prefix(void);
void print_tree_node(const char* path, int level, bool is_last, const char* prefix);
void build_tree_recursive(char** paths, int count, int level, char* prefix,
                          const char* path_prefix);
void rank_files(const char* query, FileRank* ranks, int num_files);


static char* slurp_stream(FILE* fp)
{
    size_t mark = arena_get_mark(&g_arena);
    size_t cap = 4096, len = 0;
    char* buf = arena_push_array_safe(&g_arena, char, cap);
    bool warning_issued = false;

    int c;
    while ((c = fgetc(fp)) != EOF)
    {
        if (len + 1 >= cap)
        {
            if (cap > SIZE_MAX / 2)
            {
                errno = ENOMEM;
                return NULL;
            }
            size_t new_cap = cap * 2;
            char* tmp = arena_push_array_safe(&g_arena, char, new_cap);
            memcpy(tmp, buf, len);
            buf = tmp;
            cap = new_cap;
        }
        buf[len++] = (char)c;

        if (!warning_issued && len > STDIN_BUFFER_SIZE)
        {
            fprintf(stderr,
                    "Warning: Input stream exceeds %d MB. Large inputs may cause clipboard "
                    "operations to fail.\n",
                    STDIN_BUFFER_SIZE / (1024 * 1024));
            warning_issued = true;
        }
    }
    buf[len] = '\0';

    if (ferror(fp))
    {
        int saved_errno = errno;
        arena_set_mark(&g_arena, mark);
        errno = saved_errno;
        return NULL;
    }
    return buf;
}


static char* slurp_file(const char* path)
{
    FILE* fp = fopen(path, "r");
    if (!fp)
    {
        return NULL;
    }
    size_t mark = arena_get_mark(&g_arena);
    char* txt = slurp_stream(fp);
    int slurp_errno = errno;

    if (fclose(fp) != 0)
    {
        if (txt != NULL)
        {
            arena_set_mark(&g_arena, mark);
            return NULL;
        }
        errno = slurp_errno;
        return NULL;
    }

    errno = slurp_errno;
    return txt;
}


static char* expand_tilde_path(const char* path)
{
    if (!path || path[0] != '~')
    {
        return arena_strdup_safe(&g_arena, path);
    }

    glob_t glob_result;
    int glob_flags = GLOB_TILDE;

    int glob_status = glob(path, glob_flags, NULL, &glob_result);
    if (glob_status != 0)
    {
        if (glob_status == GLOB_NOMATCH)
        {
            errno = ENOENT;
        }
        else
        {
            errno = EINVAL;
        }
        return NULL;
    }

    if (glob_result.gl_pathc != 1)
    {
        globfree(&glob_result);
        errno = EINVAL;
        return NULL;
    }

    char* expanded_path = arena_strdup_safe(&g_arena, glob_result.gl_pathv[0]);
    globfree(&glob_result);

    if (!expanded_path)
    {
        errno = ENOMEM;
        return NULL;
    }

    return expanded_path;
}

bool process_stdin_content(void);
void output_file_callback(const char* name, const char* type, const char* content);
bool is_binary(FILE* file);
bool file_already_in_tree(const char* filepath);
void add_directory_tree(const char* base_dir);
void add_directory_tree_with_depth(const char* base_dir, int current_depth);

typedef struct
{
    char filename[MAX_PATH];
    char type[32];
    char* content;
} SpecialFile;

char temp_file_path[MAX_PATH];
FILE* temp_file = NULL;
int files_found = 0;
ProcessedFile processed_files[MAX_FILES];
int num_processed_files = 0;
FileInfo file_tree[MAX_FILES];
int file_tree_count = 0;
FILE* tree_file = NULL;
char tree_file_path[MAX_PATH];
SpecialFile special_files[10];
int num_special_files = 0;
static int file_mode = 0;
char* user_instructions = NULL;
static char* system_instructions = NULL;

/* Default system instructions based on traits of pragmatic developers */
static const char* DEFAULT_SYSTEM_INSTRUCTIONS =
    "You are pragmatic, direct, and focused on simplicity. You prioritize elegant solutions "
    "with minimal complexity, favor data-driven designs over excessive abstraction, and "
    "communicate technical ideas clearly without unnecessary verbosity.";
static bool want_editor_comments = false;  /* -e flag */
static char* custom_response_guide = NULL; /* Custom response guide from -e argument */
static bool raw_mode = false;              /* -R flag */
static bool enable_filerank = false;       /* -r flag */
static bool g_filerank_debug = false;      /* --filerank-debug flag */
static bool tree_only = false;             /* -T flag - filtered tree */
static bool global_tree_only = false;      /* -t flag - global tree */
static bool tree_only_output = false;      /* -O flag - tree only without file content */
static int tree_max_depth = 4;             /* -L flag - default depth of 4 for web dev projects */
/* debug_mode defined in debug.c */

/* FileRank weight configuration */
static double g_filerank_weight_path = 8.0;    /* Weight for path hits */
static double g_filerank_weight_content = 0.8; /* Weight for content hits */
static double g_filerank_weight_size = 0.08;   /* Weight for size penalty */
static double g_filerank_weight_tfidf = 16.0;  /* Weight for TF-IDF score */
static char* g_filerank_cutoff_spec = NULL;    /* Cutoff specification string */

/* Keywords boost configuration */
#define MAX_KEYWORDS 32
#define KEYWORD_BASE_MULTIPLIER 64.0

typedef struct
{
    char* token;   /* lowercase, arena-owned */
    double weight; /* factor * BASE_MULTIPLIER */
} KeywordBoost;

/* globals */
static KeywordBoost g_kw_boosts[MAX_KEYWORDS];
static int g_kw_boosts_len = 0;           /* 0..MAX_KEYWORDS */
static bool g_keywords_flag_used = false; /* Track if --keywords was used */

/* CLI exclude pattern configuration */
#define MAX_CLI_EXCLUDE_PATTERNS 128
static char* g_cli_exclude_patterns[MAX_CLI_EXCLUDE_PATTERNS];
static int g_cli_exclude_count = 0;

static bool parse_keywords(const char* spec)
{
    if (!spec || !*spec)
    {
        return true;
    }

    g_kw_boosts_len = 0;

    char* str_copy = arena_strdup_safe(&g_arena, spec);
    if (!str_copy)
        return false;

    char* saveptr;
    char* token_spec = strtok_r(str_copy, ", ", &saveptr);

    while (token_spec)
    {
        if (g_kw_boosts_len >= MAX_KEYWORDS)
        {
            fprintf(stderr,
                    "Warning: Maximum %d keywords allowed, ignoring '%s' and remaining keywords\n",
                    MAX_KEYWORDS, token_spec);
            break;
        }
        char* colon = strchr(token_spec, ':');
        double factor = 2.0;

        if (colon)
        {
            *colon = '\0';
            char* weight_str = colon + 1;
            char* endptr;
            factor = strtod(weight_str, &endptr);

            if (*endptr != '\0')
            {
                fprintf(stderr, "Warning: Invalid factor '%s' for keyword '%s', using default 2\n",
                        weight_str, token_spec);
                factor = 2.0;
            }
            else if (factor <= 0)
            {
                fprintf(stderr,
                        "Warning: Factor must be positive for keyword '%s', using default 2\n",
                        token_spec);
                factor = 2.0;
            }
        }

        double weight = factor * KEYWORD_BASE_MULTIPLIER;

        if (!*token_spec)
        {
            token_spec = strtok_r(NULL, ", ", &saveptr);
            continue;
        }

        char* lowercase_token = arena_strdup_safe(&g_arena, token_spec);
        if (!lowercase_token)
            return false;

        for (char* p = lowercase_token; *p; p++)
        {
            *p = tolower((unsigned char)*p);
        }

        bool is_duplicate = false;
        for (int i = 0; i < g_kw_boosts_len; i++)
        {
            if (strcmp(g_kw_boosts[i].token, lowercase_token) == 0)
            {
                double new_factor = weight / KEYWORD_BASE_MULTIPLIER;
                fprintf(stderr,
                        "Warning: Duplicate keyword '%s', updating to factor %.1f (%.0fx boost)\n",
                        lowercase_token, new_factor, weight);
                g_kw_boosts[i].weight = weight;
                is_duplicate = true;
                break;
            }
        }

        if (!is_duplicate)
        {
            g_kw_boosts[g_kw_boosts_len].token = lowercase_token;
            g_kw_boosts[g_kw_boosts_len].weight = weight;
            g_kw_boosts_len++;
        }

        token_spec = strtok_r(NULL, ", ", &saveptr);
    }

    return true;
}

/** Get keyword weight for a token */
static inline double kw_weight_for(const char* tok)
{
    /* Look up token in keywords array */
    for (int i = 0; i < g_kw_boosts_len; ++i)
    {
        if (strcasecmp(g_kw_boosts[i].token, tok) == 0)
        {
            return g_kw_boosts[i].weight;
        }
    }

    return 1.0; /* No boost */
}

/** Parse FileRank weights from string */
static bool parse_filerank_weights(const char* weight_str)
{
    if (!weight_str || !*weight_str)
        return false;

    /* Make a copy to tokenize */
    char* str_copy = strdup(weight_str);
    if (!str_copy)
        return false;

    char* token = strtok(str_copy, ",");
    while (token)
    {
        char* colon = strchr(token, ':');
        if (!colon)
        {
            fprintf(stderr, "Error: Invalid weight format '%s' (expected name:value)\n", token);
            free(str_copy);
            return false;
        }

        *colon = '\0';
        char* name = token;
        char* value_str = colon + 1;
        char* endptr;
        double value = strtod(value_str, &endptr);

        if (*endptr != '\0')
        {
            fprintf(stderr, "Error: Invalid weight value '%s' for '%s'\n", value_str, name);
            free(str_copy);
            return false;
        }

        /* Apply the weight */
        if (strcmp(name, "path") == 0)
        {
            g_filerank_weight_path = value;
        }
        else if (strcmp(name, "content") == 0)
        {
            g_filerank_weight_content = value;
        }
        else if (strcmp(name, "size") == 0)
        {
            g_filerank_weight_size = value;
        }
        else if (strcmp(name, "tfidf") == 0)
        {
            g_filerank_weight_tfidf = value;
        }
        else
        {
            fprintf(stderr,
                    "Warning: Unknown weight name '%s' (valid: path, content, size, tfidf)\n",
                    name);
        }

        token = strtok(NULL, ",");
    }

    free(str_copy);
    return true;
}

/** Add CLI exclude pattern to global list */
static void add_cli_exclude_pattern(const char* raw)
{
    if (g_cli_exclude_count >= MAX_CLI_EXCLUDE_PATTERNS)
    {
        fprintf(stderr, "Warning: Maximum %d exclude patterns allowed, ignoring '%s'\n",
                MAX_CLI_EXCLUDE_PATTERNS, raw);
        return;
    }
    g_cli_exclude_patterns[g_cli_exclude_count++] = arena_strdup_safe(&g_arena, raw);
}

/** Check if path matches CLI exclude pattern */
/* WHY: Supports git-style globs with ** for recursive matching,
 * combining fnmatch efficiency with custom ** handling */
static bool matches_cli_exclude(const char* path)
{
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;

    for (int i = 0; i < g_cli_exclude_count; ++i)
    {
        const char* pat = g_cli_exclude_patterns[i];

        /* Handle ** patterns for recursive directory matching */
        const char* double_star = strstr(pat, "**");
        if (double_star)
        {
            /* Pattern contains **, needs special handling */
            size_t prefix_len = double_star - pat;

            /* Check if path starts with the prefix before ** */
            if (prefix_len > 0 && strncmp(path, pat, prefix_len) != 0)
            {
                continue;
            }

            /* If pattern ends with double-star or slash-double-star, match any path with that
             * prefix */
            if (double_star[2] == '\0' || (double_star[2] == '/' && double_star[3] == '\0'))
            {
                return true;
            }

            /* If pattern is like dir/double-star, match anything under dir/ */
            if (prefix_len > 0 && pat[prefix_len - 1] == '/')
            {
                return true;
            }

            /* For patterns like dir/double-star/file, use simple substring match for now */
            const char* suffix = double_star + 2;
            if (*suffix == '/')
                suffix++;
            if (*suffix && strstr(path + prefix_len, suffix))
            {
                return true;
            }
        }
        else
        {
            /* Standard fnmatch for patterns without ** */
            if (fnmatch(pat, path, FNM_PATHNAME | FNM_PERIOD) == 0 || fnmatch(pat, base, 0) == 0)
                return true;
        }
    }
    return false;
}

/** Add system instructions to output if provided */
static void add_system_instructions(const char* msg)
{
    if (!msg || !*msg)
        return;
    fprintf(temp_file, "<system_instructions>\n%s\n</system_instructions>\n\n", msg);
}

/* Global flag to track if any file content has been written */
static bool wrote_file_context = false;

/** Open file context block lazily if needed */
static void open_file_context_if_needed(void)
{
    if (!wrote_file_context)
    {
        fprintf(temp_file, "<file_context>\n\n");
        wrote_file_context = true;
    }
}

/** Add response guide block to output */
static void add_response_guide(const char* problem)
{
    // Only add the guide if -e flag was explicitly used
    if (want_editor_comments)
    {
        fprintf(temp_file, "<response_guide>\n");

        // If custom response guide provided, use it directly
        if (custom_response_guide && *custom_response_guide)
        {
            fprintf(temp_file, "%s\n", custom_response_guide);
        }
        else
        {
            // Use default behavior
            fprintf(temp_file, "LLM: Please respond using the markdown format below.\n");

            // Only include Problem Statement section if user instructions were provided
            if (problem && *problem)
            {
                fprintf(temp_file, "## Problem Statement\n");
                fprintf(temp_file, "Summarize the user's request or problem based on the overall "
                                   "context provided.\n");
            }

            fprintf(temp_file, "## Response\n");
            fprintf(temp_file, "    1. Provide a clear, step-by-step solution or explanation.\n");
            fprintf(
                temp_file,
                "    2. Return **PR-style code review comments**: use GitHub inline-diff syntax, "
                "group notes per file, justify each change, and suggest concrete refactors.\n");
        }

        fprintf(temp_file, "</response_guide>\n\n");
    }
}
/** Check if file already processed to avoid duplicates */
bool file_already_processed(const char* filepath, int start_line, int end_line)
{
    /* Pre-condition: valid filepath parameter */
    assert(filepath != NULL);
    assert(strlen(filepath) > 0);

    /* Invariant: num_processed_files is always valid */
    assert(num_processed_files >= 0);
    assert(num_processed_files <= MAX_FILES);

    for (int i = 0; i < num_processed_files; i++)
    {
        /* Invariant: each processed_files entry is valid */
        if (strcmp(processed_files[i].path, filepath) == 0 &&
            processed_files[i].start_line == start_line && processed_files[i].end_line == end_line)
        {
            /* Post-condition: found matching file */
            return true;
        }
    }
    /* Post-condition: file not found */
    return false;
}

/** Add file to processed files list */
void add_to_processed_files(const char* filepath, int start_line, int end_line)
{
    /* Pre-condition: valid filepath */
    assert(filepath != NULL);
    assert(strlen(filepath) > 0);

    /* Pre-condition: we have space for more files */
    assert(num_processed_files < MAX_FILES);

    if (num_processed_files < MAX_FILES)
    {
        processed_files[num_processed_files].path = arena_strdup_safe(&g_arena, filepath);
        processed_files[num_processed_files].start_line = start_line;
        processed_files[num_processed_files].end_line = end_line;
        if (!processed_files[num_processed_files].path)
        {
            fatal("Out of memory duplicating file path: %s", filepath);
        }
        num_processed_files++;

        /* Post-condition: file was added successfully */
        assert(num_processed_files > 0);
        assert(strcmp(processed_files[num_processed_files - 1].path, filepath) == 0);
    }
}

/** Add file to file tree structure */
void add_to_file_tree(const char* filepath)
{
    /* Pre-condition: valid filepath */
    assert(filepath != NULL);
    assert(strlen(filepath) > 0);

    /* We don't need to assert file_tree_count < MAX_FILES anymore since we check and handle it */

    /* Check if we've hit the file limit */
    if (file_tree_count >= MAX_FILES)
    {
        /* Log warning only once when we first hit the limit */
        static bool limit_warning_shown = false;
        if (!limit_warning_shown)
        {
            fprintf(stderr,
                    "Warning: Maximum number of files (%d) exceeded. Some files will not be "
                    "included in the context.\n",
                    MAX_FILES);
            limit_warning_shown = true;
        }
        return;
    }

    bool is_special = (strcmp(filepath, "stdin_content") == 0);
    struct stat statbuf;
    bool is_dir = false; /* Default to not a directory */

    /* Only call lstat for actual file paths, not special names */
    if (!is_special)
    {
        if (lstat(filepath, &statbuf) == 0)
        {
            is_dir = S_ISDIR(statbuf.st_mode);
        }
        /* If lstat fails, keep is_dir as false */
    }
    /* For special files like "stdin_content", is_dir remains false */

    FileInfo new_file = {
        .path = "", .relative_path = NULL, .is_dir = is_dir /* Use determined or default value */
    };

    /* Store file path using snprintf for guaranteed null termination. */
    /* This avoids potential truncation issues with strncpy if filepath */
    /* has length >= MAX_PATH - 1. */
    int written = snprintf(new_file.path, sizeof(new_file.path), "%s", filepath);

    /* Check for truncation or encoding errors from snprintf. */
    if (written < 0 || (size_t)written >= sizeof(new_file.path))
    {
        fprintf(stderr, "Warning: File path truncated or encoding error for '%s'\n", filepath);
        /* Decide how to handle: skip this file or fatal? Skipping for now. */
        return; /* Skip adding this file to the tree */
    }

    /* Add to file tree array */
    file_tree[file_tree_count++] = new_file;

    assert(file_tree_count > 0);
}

/** Check if path already exists in file tree */
bool file_already_in_tree(const char* filepath)
{
    for (int i = 0; i < file_tree_count; i++)
    {
        if (strcmp(file_tree[i].path, filepath) == 0)
        {
            return true;
        }
    }
    return false;
}

/** Recursively add directory tree to file_tree */
void add_directory_tree(const char* base_dir)
{
    add_directory_tree_with_depth(base_dir, 0);
}

/** Recursively add directory tree with depth tracking */
void add_directory_tree_with_depth(const char* base_dir, int current_depth)
{
    DIR* dir;
    struct dirent* entry;
    struct stat statbuf;
    char path[MAX_PATH];

    /* Check if we've reached max depth */
    if (current_depth >= tree_max_depth)
    {
        return;
    }

    if (!(dir = opendir(base_dir)))
        return;

    while ((entry = readdir(dir)) != NULL)
    {
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
        if (matches_cli_exclude(path))
            continue;
        if (!file_already_in_tree(path))
            add_to_file_tree(path);
        if (S_ISDIR(statbuf.st_mode))
            add_directory_tree_with_depth(path, current_depth + 1);
    }

    closedir(dir);
}
/** Compare function for sorting file paths */
int compare_file_paths(const void* a, const void* b)
{
    const FileInfo* file_a = (const FileInfo*)a;
    const FileInfo* file_b = (const FileInfo*)b;
    return strcmp(file_a->path, file_b->path);
}

/** Find common prefix of all files in tree */
char* find_common_prefix(void)
{
    if (file_tree_count == 0)
    {
        return arena_strdup_safe(&g_arena, ".");
    }

    /* Start with the first file's directory */
    char* prefix = arena_strdup_safe(&g_arena, file_tree[0].path);
    char* last_slash = NULL;

    /* Find directory part */
    last_slash = strrchr(prefix, '/');
    if (last_slash != NULL)
    {
        *last_slash = '\0';
    }
    else
    {
        /* If no slash, use current directory */
        return arena_strdup_safe(&g_arena, ".");
    }

    /* Check each file to find common prefix */
    for (int i = 1; i < file_tree_count; i++)
    {
        char* path = file_tree[i].path;
        int j;

        /* Find common prefix */
        for (j = 0; prefix[j] && path[j]; j++)
        {
            if (prefix[j] != path[j])
            {
                break;
            }
        }

        /* Truncate to common portion */
        prefix[j] = '\0';

        /* Find last directory separator */
        last_slash = strrchr(prefix, '/');
        if (last_slash != NULL)
        {
            *last_slash = '\0';
        }
        else
        {
            /* If no common directory, use current directory */
            return arena_strdup_safe(&g_arena, ".");
        }
    }

    /* If prefix ends up empty, use current directory */
    if (prefix[0] == '\0')
    {
        return arena_strdup_safe(&g_arena, ".");
    }

    return prefix;
}

/** Print tree node with indentation */
void print_tree_node(const char* path, int level, bool is_last, const char* prefix)
{
    /* Print indentation */
    fprintf(tree_file, "%s", prefix);

    /* Print node connector */
    if (level > 0)
    {
        fprintf(tree_file, is_last ? "└── " : "├── ");
    }

    /* Print the filename part (after the last slash) */
    const char* filename = strrchr(path, '/');
    if (filename)
    {
        fprintf(tree_file, "%s\n", filename + 1);
    }
    else
    {
        fprintf(tree_file, "%s\n", path);
    }
}

/** Build directory structure recursively */
void build_tree_recursive(char** paths, int count, int level, char* prefix,
                          const char* path_prefix __attribute__((unused)))
{
    if (count <= 0)
        return;

    /* Check if we've reached max depth for display */
    if (level >= tree_max_depth)
    {
        return;
    }

    char current_dir[MAX_PATH] = "";

    /* Find current directory for this level */
    for (int i = 0; i < count; i++)
    {
        char* path = paths[i];
        char* slash = strchr(path, '/');

        if (slash)
        {
            int dir_len = slash - path;
            if (current_dir[0] == '\0')
            {
                strncpy(current_dir, path, dir_len);
                current_dir[dir_len] = '\0';
            }
        }
    }

    /* First print files at current level */
    for (int i = 0; i < count; i++)
    {
        if (strchr(paths[i], '/') == NULL)
        {
            fprintf(tree_file, "%s%s%s\n", prefix, (level > 0) ? "├── " : "", paths[i]);
        }
    }

    /* Then process subdirectories */
    for (int i = 0; i < count;)
    {
        char* slash = strchr(paths[i], '/');
        if (!slash)
        {
            i++; /* Skip files, already processed */
            continue;
        }

        int dir_len = slash - paths[i];
        char dirname[MAX_PATH];
        strncpy(dirname, paths[i], dir_len);
        dirname[dir_len] = '\0';

        /* Count how many entries are in this directory */
        int subdir_count = 0;
        for (int j = i; j < count; j++)
        {
            if (strncmp(paths[j], dirname, dir_len) == 0 && paths[j][dir_len] == '/')
            {
                subdir_count++;
            }
            else if (j > i)
            {
                break; /* No more entries in this directory */
            }
        }

        if (subdir_count > 0)
        {
            /* Print directory entry */
            fprintf(tree_file, "%s%s%s\n", prefix, (level > 0) ? "├── " : "", dirname);

            /* Create new prefix for subdirectory items */
            char new_prefix[MAX_PATH];
            sprintf(new_prefix, "%s%s", prefix, (level > 0) ? "│   " : "");

            /* Create path array for subdirectory items */
            char* subdirs[MAX_FILES];
            int subdir_idx = 0;

            /* Extract paths relative to this subdirectory */
            for (int j = i; j < i + subdir_count; j++)
            {
                subdirs[subdir_idx++] = paths[j] + dir_len + 1; /* Skip dirname and slash */
            }

            /* Process subdirectory */
            build_tree_recursive(subdirs, subdir_count, level + 1, new_prefix, dirname);

            i += subdir_count;
        }
        else
        {
            i++; /* Move to next entry */
        }
    }
}

/** Generate and add file tree to output */
void generate_file_tree(void)
{
    if (file_tree_count == 0)
    {
        return;
    }

    /* Create a temporary file for the tree */
    strcpy(tree_file_path, "/tmp/llm_ctx_tree_XXXXXX");
    int fd = mkstemp(tree_file_path);
    if (fd == -1)
    {
        return;
    }

    tree_file = fdopen(fd, "w");
    if (!tree_file)
    {
        close(fd);
        return;
    }

    /* Sort files for easier tree generation */
    qsort(file_tree, file_tree_count, sizeof(FileInfo), compare_file_paths);

    /* Find common prefix */
    char* common_prefix = find_common_prefix();
    int prefix_len = strlen(common_prefix);

    /* Set relative paths and collect non-directory paths */
    char* paths[MAX_FILES];
    int path_count = 0;

    for (int i = 0; i < file_tree_count; i++)
    {
        const char* path = file_tree[i].path;

        /* Skip directories, we only want files */
        if (file_tree[i].is_dir)
        {
            continue;
        }

        /* If path starts with common prefix, skip it for relative path */
        if (strncmp(path, common_prefix, prefix_len) == 0)
        {
            if (path[prefix_len] == '/')
            {
                file_tree[i].relative_path = arena_strdup_safe(&g_arena, path + prefix_len + 1);
            }
            else
            {
                file_tree[i].relative_path = arena_strdup_safe(&g_arena, path + prefix_len);
            }
        }
        else
        {
            file_tree[i].relative_path = arena_strdup_safe(&g_arena, path);
        }

        /* Add to paths array for tree building */
        if (file_tree[i].relative_path && path_count < MAX_FILES)
        {
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
    FILE* f = fopen(tree_file_path, "r");
    if (f)
    {
        char buffer[4096];
        size_t bytes_read;

        while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0)
        {
            fwrite(buffer, 1, bytes_read, temp_file);
        }

        fclose(f);
    }

    fprintf(temp_file, "</file_tree>\n\n");

    /* Don't remove tree file yet - FileRank might need it later */
    /* Tree file will be cleaned up in cleanup() function */

    /* Clear relative path pointers */
    for (int i = 0; i < file_tree_count; i++)
    {
        file_tree[i].relative_path = NULL;
    }
}
/**
 * Check if file stream contains binary data
 *
 * Detects null bytes and non-printable characters, rewinds stream.
 */
bool is_binary(FILE* file)
{
    /* Pre-condition: file must be non-NULL and opened in binary read mode */
    assert(file != NULL);

    char buffer[BINARY_CHECK_SIZE];
    size_t bytes_read;
    long original_pos = ftell(file);
    /* Check if ftell failed (e.g., on a pipe/socket) */
    if (original_pos == -1)
    {
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
    if (ferror(file))
    {
        fseek(file, original_pos, SEEK_SET); /* Attempt to restore position */
        return false;                        /* Treat read error as non-binary for safety */
    }

    if (bytes_read > 0)
    {
        /* Check for Null Bytes (strongest indicator) and specific non-whitespace */
        /* C0 control codes (0x01-0x08, 0x0B, 0x0C, 0x0E-0x1F). */
        /* This combined check avoids locale issues with isprint() and handles */
        /* common binary file characteristics efficiently. */
        for (size_t i = 0; i < bytes_read; i++)
        {
            unsigned char c = (unsigned char)buffer[i];
            if (c == '\0')
            {
                likely_binary = true;
                break; /* Found null byte, definitely binary */
            }
            /* Check for control characters excluding common whitespace (TAB, LF, CR) */
            if (c < 0x20 && c != '\t' && c != '\n' && c != '\r')
            {
                likely_binary = true;
                break; /* Found suspicious control code */
            }
        }
        /* No need for goto, loop completion handles the non-binary case. */
    }

    /* Restore original file position. Check for fseek error. */
    if (fseek(file, original_pos, SEEK_SET) != 0)
    {
        /* Failed to restore position, file stream might be compromised. */
        /* Handle appropriately, e.g., log a warning or return an error state. */
        /* For now, proceed but be aware the file position is wrong. */
        fprintf(stderr, "Warning: Failed to restore file position for binary check.\n");
    }

    /* Post-condition: file pointer is restored (best effort) */
    assert(ftell(file) == original_pos);

    return likely_binary;
}
/** Display help message with usage instructions */
void show_help(void)
{
    printf("Usage: llm_ctx [OPTIONS] [FILE...]\n");
    printf("       llm_ctx get <UUID>\n");
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
    printf("  -r, --rank     Enable FileRank to sort files by relevance to query\n");
    printf("                 (default: preserve file order as specified)\n");
    printf("  -R, --raw      Raw mode: omit system instructions and response guide\n");
    printf("  -f [FILE...]   Process files instead of stdin content (supports file:START-END)\n");
    printf("                 Examples: app.c:10-20, notes.txt:15-, readme.md:8\n");
    printf("  -t             Generate complete directory tree (full tree)\n");
    printf("  -T             Generate file tree only for specified files (filtered tree)\n");
    printf("  -O             Generate tree only (no file content)\n");
    printf("  -L N           Limit tree depth to N levels (default: 4)\n");
    printf("  -o [FILE]      Output to stdout (default) or write to FILE\n");
    printf("                 Accepts -oFILE, -o FILE, -o@FILE, --output=FILE\n");
    printf("  -d, --debug    Enable debug output (prefixed with [DEBUG])\n");
    printf("  -h             Show this help message\n");
    printf("  -b N           Set token budget limit (default: 96000)\n");
    printf("                 Shows warning if exceeded (use -r to auto-select files)\n");
    printf("  --token-budget=N      Set token budget limit (default: 96000)\n");
    printf("  --token-model=MODEL   Set model for token counting (default: gpt-4o)\n");
    printf("  --filerank-debug      Show FileRank scoring details (requires -r flag)\n");
    printf("  --filerank-weight=W   Set FileRank weights (requires -r flag)\n");
    printf("                        Format: path:2,content:1,size:0.05,tfidf:10\n");
    printf("  --filerank-cutoff=SPEC Set FileRank score threshold (requires -r flag)\n");
    printf("                        Format: ratio:0.125, topk:10, percentile:75, auto\n");
    printf("  -k, --keywords=SPEC   Boost specific keywords in FileRank scoring (requires -r)\n");
    printf("                        Format: token1:factor1,token2:factor2 or token1,token2\n");
    printf("                        Factors are multiplied by 64 (e.g., factor 2 = 128x boost)\n");
    printf("                        Default factor is 2 if not specified\n");
    printf("                        Example: -k 'chat_input:3,prosemirror:1.5'\n");
    printf("  -x, --exclude=PATTERN Exclude files/directories matching PATTERN (repeatable)\n");
    printf("                        Patterns use git-style glob syntax\n");
    printf("                        Applied after .gitignore processing\n");
    printf("  --no-gitignore        Ignore .gitignore files when collecting files\n");
    printf("  --ignore-config       Skip loading configuration file\n\n");
    printf("By default, llm_ctx reads content from stdin.\n");
    printf("Use -f flag to indicate file arguments are provided.\n");
    printf("Files are processed in the order specified unless -r flag is used.\n\n");
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
    printf("  # Enable FileRank to sort files by relevance to your query\n");
    printf("  llm_ctx -c \"How does authentication work?\" -r -f src/**/*.js\n\n");
    printf("  # Boost specific keywords for better ranking (requires -r)\n");
    printf("  llm_ctx -c \"How does auth work?\" -r -k auth:3,session:2 -f src/**/*.js\n");
    printf("  # auth gets 192x boost (3*64), session gets 128x boost (2*64)\n\n");
    printf("  # Handle token budget overflow by auto-selecting most relevant files\n");
    printf("  llm_ctx -c \"explain the API\" -r -b 4000 -f src/**/*.js\n\n");
    printf("  # Exclude specific patterns\n");
    printf("  llm_ctx -f src/** -x 'src/generated/**' -x '*.min.js'\n\n");
    printf("  # Include directory but exclude subdirectory\n");
    printf("  llm_ctx -f javascripts/ -x 'javascripts/lib/cami/**'\n\n");
    printf("  # Generate and save a prompt (automatically copied to clipboard)\n");
    printf("  git diff | llm_ctx -c \"Review changes\"\n");
    printf("  # Output will include: saved as 20241117-192834-XXXXXX\n\n");
    printf("  # Retrieve a saved prompt by UUID\n");
    printf("  llm_ctx get 20241117-192834-XXXXXX\n");
    printf("  # Output: Retrieved and copied prompt\n\n");
    exit(0);
}

/** Process raw content from stdin as single file */
/* WHY: Handles piped input from commands like git/cat with proper
 * binary detection and content type recognition for LLM formatting */
bool process_stdin_content(void)
{
    char buffer[4096];
    size_t bytes_read;
    FILE* content_file_write = NULL;
    FILE* content_file_read = NULL;
    bool found_content = false;
    char* stdin_content_buffer = NULL;      /* Buffer to hold content if not binary */
    const char* content_to_register = NULL; /* Points to buffer or placeholder */
    char content_type[32] = "";             /* Detected content type */
    bool buffer_filled_completely = false;  /* Flag to detect truncation */
    bool truncation_warning_issued = false; /* Ensure warning is printed only once */

    /* Create a temporary file to store the content */
    char content_path[MAX_PATH];
    strcpy(content_path, "/tmp/llm_ctx_content_XXXXXX");
    int fd = mkstemp(content_path);
    if (fd == -1)
    {
        perror("Failed to create temporary file for stdin");
        return false;
    }

    content_file_write = fdopen(fd, "wb"); /* Open in binary write mode */
    if (!content_file_write)
    {
        perror("Failed to open temporary file for writing");
        close(fd);
        unlink(content_path);
        return false;
    }

    /* Read all data from stdin and save to temp file */
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), stdin)) > 0)
    {
        fwrite(buffer, 1, bytes_read, content_file_write);
        found_content = true;
    }
    fclose(content_file_write); /* Close write handle */

    /* Check if we actually got any content. If not, treat as empty input. */
    if (!found_content)
    {
        /* Register stdin_content with empty content */
        content_to_register = "";
        strcpy(content_type, ""); /* No type for empty */
        /* Register stdin_content with empty content */
        content_to_register = "";
        strcpy(content_type, ""); /* No type for empty */
        /* content_file_read remains NULL */
    }
    else
    {
        /* Content was found, reopen the temp file for reading */
        content_file_read = fopen(content_path, "rb");
        if (!content_file_read)
        {
            perror("Failed to reopen temporary file for reading");
            unlink(content_path);
            return false;
        }

        /* Check if the content is binary */
        if (is_binary(content_file_read))
        {
            content_to_register = "[Binary file content skipped]";
            strcpy(content_type, ""); /* No type for binary */
        }
        else
        {
            /* Not binary, determine content type and read content */
            rewind(content_file_read); /* Rewind after binary check */
            char first_line[1024] = "";
            if (fgets(first_line, sizeof(first_line), content_file_read) != NULL)
            {
                /* Content type detection logic */
                if (strstr(first_line, "diff --git") == first_line ||
                    strstr(first_line, "commit ") == first_line ||
                    strstr(first_line, "index ") == first_line ||
                    strstr(first_line, "--- a/") == first_line)
                {
                    strcpy(content_type, "diff");
                }
                else if (first_line[0] == '{' || first_line[0] == '[')
                {
                    strcpy(content_type, "json");
                }
                else if (strstr(first_line, "<?xml") == first_line ||
                         strstr(first_line, "<") != NULL)
                {
                    strcpy(content_type, "xml");
                }
                else if (first_line[0] == '#' || strstr(first_line, "```") != NULL)
                {
                    strcpy(content_type, "markdown");
                }
            }

            /* Allocate buffer and read the full content */
            rewind(content_file_read); /* Rewind again before full read */
            stdin_content_buffer = arena_push_array_safe(&g_arena, char, STDIN_BUFFER_SIZE);
            if (!stdin_content_buffer)
            {
                /* Post-condition: Allocation failed */
                assert(stdin_content_buffer == NULL);
                perror("Failed to allocate memory for stdin content");
                fclose(content_file_read);
                unlink(content_path);
                return false;
            }

            size_t total_read = 0;
            while (total_read < STDIN_BUFFER_SIZE - 1)
            {
                size_t space_left = STDIN_BUFFER_SIZE - 1 - total_read;
                /* Read either a full buffer or just enough to fill stdin_content_buffer */
                size_t bytes_to_read = (space_left < sizeof(buffer)) ? space_left : sizeof(buffer);

                bytes_read = fread(buffer, 1, bytes_to_read, content_file_read);

                if (bytes_read > 0)
                {
                    memcpy(stdin_content_buffer + total_read, buffer, bytes_read);
                    total_read += bytes_read;

                    /* Check if we've filled the buffer exactly */
                    if (total_read == STDIN_BUFFER_SIZE - 1)
                    {
                        buffer_filled_completely = true;
                        break; /* Exit loop to check if there's more data */
                    }
                }
                else
                {
                    /* Check for read error */
                    if (ferror(content_file_read))
                    {
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
            if (buffer_filled_completely)
            {
                /* Try reading one more byte to see if input was longer */
                int next_char = fgetc(content_file_read);
                if (next_char != EOF)
                {
                    /* More data exists - input was truncated */
                    fprintf(
                        stderr,
                        "Warning: Standard input exceeded buffer size (%d MB) and was truncated.\n",
                        STDIN_BUFFER_SIZE / (1024 * 1024));
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
    if (content_file_read)
    { /* Only close if it was opened */
        fclose(content_file_read);
    }
    unlink(content_path); /* Always remove temp file */
    /* Buffer allocated from arena persists until cleanup */

    /* Increment files found so we don't error out */
    files_found++;

    return true; /* Indicate success */
}

/* Function to register a callback for a special file */
void output_file_callback(const char* name, const char* type, const char* content)
{
    /* Pre-condition: validate inputs */
    assert(name != NULL);
    assert(type != NULL);
    assert(content != NULL);

    /* Check if we have room for more special files */
    assert(num_special_files < 10);
    if (num_special_files >= 10)
    {
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
    if (num_processed_files < MAX_FILES)
    {
        processed_files[num_processed_files].path = arena_strdup_safe(&g_arena, name);
        processed_files[num_processed_files].start_line = 0;
        processed_files[num_processed_files].end_line = 0;
        /* Check allocation immediately */
        if (!processed_files[num_processed_files].path)
        {
            fatal("Out of memory duplicating special file name: %s", name);
        }
        num_processed_files++;

        /* Also add to the file tree structure */
        // REVIEW: Replaced assert with fatal() call on allocation failure for consistency
        // and robustness, normalizing the OOM failure path.
        add_to_file_tree(name); /* Handles special "stdin_content" case */
    }
}

/** Collect file for processing without outputting content */
bool collect_file(const char* filepath, int start_line, int end_line)
{
    // Avoid duplicates in content output
    if (file_already_processed(filepath, start_line, end_line))
    {
        return true;
    }

    struct stat statbuf;
    // Check if it's a regular file and readable
    // lstat check is slightly redundant as caller likely did it, but safe.
    if (lstat(filepath, &statbuf) == 0 && S_ISREG(statbuf.st_mode) && access(filepath, R_OK) == 0)
    {
        if (num_processed_files < MAX_FILES)
        {
            processed_files[num_processed_files].path = arena_strdup_safe(&g_arena, filepath);
            processed_files[num_processed_files].start_line = start_line;
            processed_files[num_processed_files].end_line = end_line;
            /* Check allocation immediately */
            if (!processed_files[num_processed_files].path)
            {
                /* Use fatal for consistency on OOM */
                fatal("Out of memory duplicating file path in collect_file: %s", filepath);
            }
            num_processed_files++;
            files_found++; // Increment count only for files we will output content for
            return true;
        }
        else
        {
            // Too many files - log warning?
            fprintf(stderr, "Warning: Maximum number of files (%d) exceeded. Skipping %s\n",
                    MAX_FILES, filepath);
            return false; // Indicate that we couldn't process it
        }
    }
    // Not a regular readable file, don't add to processed_files for content output.
    // Return true because it's not an error, just skipping content for this path.
    return true;
}

/** Output file content with fenced code blocks for LLM */
bool output_file_content(const ProcessedFile* file_info, FILE* output)
{
    /* Ensure the file context block is opened before writing file content */
    open_file_context_if_needed();

    if (!file_info || !file_info->path)
    {
        return true;
    }

    const char* filepath = file_info->path;

    /* Check if this is a special file (e.g., stdin content) */
    for (int i = 0; i < num_special_files; i++)
    {
        if (strcmp(filepath, special_files[i].filename) == 0)
        {
            fprintf(output, "File: %s", filepath);
            if (file_info->start_line > 0 || file_info->end_line > 0)
            {
                if (file_info->end_line > 0)
                {
                    fprintf(output, " (lines %d-%d)", file_info->start_line, file_info->end_line);
                }
                else
                {
                    fprintf(output, " (lines %d-)", file_info->start_line);
                }
            }
            fprintf(output, "\n");
            /* Check if the content is the binary placeholder */
            if (strcmp(special_files[i].content, "[Binary file content skipped]") == 0)
            {
                fprintf(output, "%s\n", special_files[i].content);
            }
            else
            {
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
    if (lstat(filepath, &statbuf) == 0 && S_ISDIR(statbuf.st_mode))
    {
        return true; /* Skip directories silently */
    }

    /* Check if file exists and is readable before attempting to open */
    if (access(filepath, R_OK) != 0)
    {
        /* Don't print error for non-readable files, just skip */
        return true;
    }

    /* Open in binary read mode for binary detection */
    FILE* file = fopen(filepath, "rb");
    if (!file)
    {
        /* Should not happen due to access check, but handle anyway */
        return true;
    }

    /* Check if the file is binary */
    if (is_binary(file))
    {
        fprintf(output, "File: %s\n", filepath);
        fprintf(output, "[Binary file content skipped]\n");
        fprintf(output, "----------------------------------------\n");
    }
    else
    {
        /* File is not binary, output its content with fences */
        fprintf(output, "File: %s", filepath);
        if (file_info->start_line > 0 || file_info->end_line > 0)
        {
            if (file_info->end_line > 0)
            {
                fprintf(output, " (lines %d-%d)", file_info->start_line, file_info->end_line);
            }
            else
            {
                fprintf(output, " (lines %d-)", file_info->start_line);
            }
        }
        fprintf(output, "\n");
        fprintf(output, "```\n");

        /* Read and write the file contents */
        char buffer[4096];
        size_t bytes_read;
        /* Ensure we read from the beginning after the binary check */
        rewind(file);

        if (file_info->start_line == 0 && file_info->end_line == 0)
        {
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
            {
                fwrite(buffer, 1, bytes_read, output);
            }
        }
        else
        {
            int current_line = 1;
            while (fgets(buffer, sizeof(buffer), file))
            {
                if (current_line >= file_info->start_line &&
                    (file_info->end_line == 0 || current_line <= file_info->end_line))
                {
                    fputs(buffer, output);
                }

                if (strchr(buffer, '\n'))
                {
                    current_line++;
                }
            }
        }

        /* Close the code fence and add a separator */
        fprintf(output, "```\n");
        fprintf(output, "----------------------------------------\n");
    }

    fclose(file);
    return true;
}

/** Add user instructions to output if provided */
void add_user_instructions(const char* instructions)
{
    if (!instructions || !instructions[0])
    {
        return;
    }

    fprintf(temp_file, "<user_instructions>\n");
    fprintf(temp_file, "%s\n", instructions);
    fprintf(temp_file, "</user_instructions>\n\n");
}

/** Recursively search directories for pattern matches */
void find_recursive(const char* base_dir, const char* pattern)
{
    DIR* dir;
    struct dirent* entry;
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
    while ((entry = readdir(dir)) != NULL)
    {
        /* Skip the special directory entries */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        /* Always skip the .git directory to mirror Git's behavior */
        /* This prevents descending into the Git internal directory structure. */
        if (strcmp(entry->d_name, ".git") == 0)
        {
            continue; // Skip .git entirely
        }

        /* Construct the full path of the current entry */
        snprintf(path, sizeof(path), "%s/%s", base_dir, entry->d_name);

        /* Get entry information - skip if can't stat */
        if (lstat(path, &statbuf) == -1)
            continue;

        /* Check if the path should be ignored *before* processing */
        if (respect_gitignore && should_ignore_path(path))
        {
            continue; /* Skip ignored files/directories */
        }

        /* Check CLI exclude patterns */
        if (matches_cli_exclude(path))
        {
            continue; /* Skip CLI-excluded files/directories */
        }

        // Add any non-ignored entry to the file tree structure
        add_to_file_tree(path);

        /* If entry is a directory, recurse into it */
        if (S_ISDIR(statbuf.st_mode))
        {
            find_recursive(path, pattern);
        }
        /* If entry is a regular file, check if it matches the pattern */
        else if (S_ISREG(statbuf.st_mode))
        {
            /* Match filename against the pattern using appropriate flags */
            if (fnmatch(pattern, entry->d_name, fnmatch_flags) == 0)
            {
                // Collect the file for content output ONLY if it matches and is readable
                // collect_file now handles adding to processed_files and files_found
                collect_file(path, 0, 0);
            }
        }
    }

    closedir(dir);
}

/** Process glob pattern to find matching files */
static bool parse_range_suffix(const char* input, char* path_out, size_t max_len, int* start, int* end)
{
    if (!input || !path_out || !start || !end)
    {
        return false;
    }

    const char* colon = strrchr(input, ':');
    if (!colon)
    {
        return false;
    }

    const char* p = colon + 1;
    if (*p == '\0')
    {
        return false; /* Empty suffix */
    }

    char* endptr;
    long s = strtol(p, &endptr, 10);
    long e = 0;

    if (p == endptr)
    {
        return false; /* No digits */
    }

    if (*endptr == '-')
    {
        if (*(endptr + 1) != '\0')
        {
            e = strtol(endptr + 1, &endptr, 10);
            if (*endptr != '\0')
            {
                return false; /* trailing garbage */
            }
        }
        else
        {
            e = 0; /* open-ended */
        }
    }
    else if (*endptr == '\0')
    {
        e = s; /* single line */
    }
    else
    {
        return false; /* invalid character */
    }

    if (s < 0)
    {
        s = 0;
    }

    size_t base_len = (size_t)(colon - input);
    if (base_len >= max_len)
    {
        return false;
    }

    strncpy(path_out, input, base_len);
    path_out[base_len] = '\0';

    *start = (int)s;
    *end = (int)e;
    return true;
}

bool process_pattern(const char* pattern)
{
    int initial_files_found = files_found;

    /* First check if the pattern is a directory */
    struct stat statbuf;
    if (lstat(pattern, &statbuf) == 0 && S_ISDIR(statbuf.st_mode))
    {
        /* It's a directory - add it to tree and recursively find all files */
        add_to_file_tree(pattern);
        find_recursive(pattern, "*");
        return (files_found > initial_files_found);
    }

    /* Check for range suffix */
    char base_path[MAX_PATH];
    int start_line = 0;
    int end_line = 0;
    bool has_range = parse_range_suffix(pattern, base_path, sizeof(base_path), &start_line, &end_line);

    if (has_range)
    {
        if (lstat(base_path, &statbuf) == 0 && S_ISREG(statbuf.st_mode))
        {
            if (respect_gitignore && should_ignore_path(base_path))
                return false;
            if (matches_cli_exclude(base_path))
                return false;

            add_to_file_tree(base_path);
            collect_file(base_path, start_line, end_line);
            return (files_found > initial_files_found);
        }
    }

    /* Check if this is a recursive pattern */
    if (strstr(pattern, "**/") != NULL || strstr(pattern, "**") != NULL)
    {
        char base_dir[MAX_PATH] = ".";
        char file_pattern[MAX_PATH] = "";

        /* Extract base directory and file pattern */
        const char* recursive_marker = strstr(pattern, "**");
        if (recursive_marker)
        {
            /* Copy base directory (everything before **) */
            size_t base_len = recursive_marker - pattern;
            if (base_len > 0)
            {
                strncpy(base_dir, pattern, base_len);
                base_dir[base_len] = '\0';

                /* Remove trailing slash if present */
                if (base_len > 0 && base_dir[base_len - 1] == '/')
                {
                    base_dir[base_len - 1] = '\0';
                }
            }

            const char* pattern_start = recursive_marker + 2;
            if (*pattern_start == '/')
            {
                pattern_start++;
            }
            strcpy(file_pattern, pattern_start);
        }

        /* Set defaults for empty values */
        if (strlen(base_dir) == 0)
        {
            strcpy(base_dir, ".");
        }

        if (strlen(file_pattern) == 0)
        {
            strcpy(file_pattern, "*");
        }

        /* Use custom recursive directory traversal */
        find_recursive(base_dir, file_pattern);
    }
    else
    {
        /* For standard patterns, use the system glob() function */
        glob_t glob_result;
        int glob_flags = GLOB_TILDE; /* Support ~ expansion for home directory */

/* Add brace expansion support if available on this platform */
#ifdef GLOB_BRACE
        glob_flags |= GLOB_BRACE; /* For patterns like *.{js,ts} */
#endif

        /* Expand the pattern to match files */
        int glob_status = glob(pattern, glob_flags, NULL, &glob_result);
        if (glob_status != 0 && glob_status != GLOB_NOMATCH)
        {
            return false;
        }

        /* Process each matched file */
        for (size_t i = 0; i < glob_result.gl_pathc; i++)
        {
            const char* path = glob_result.gl_pathv[i];

            // Check ignore rules before adding
            if (respect_gitignore && should_ignore_path(path))
            {
                continue;
            }

            // Check CLI exclude patterns
            if (matches_cli_exclude(path))
            {
                continue;
            }

            // Add to tree structure
            add_to_file_tree(path);

            // Collect file content if it's a regular file
            struct stat statbuf;
            if (lstat(path, &statbuf) == 0 && S_ISREG(statbuf.st_mode))
            {
                // collect_file now handles adding to processed_files and files_found,
                // and checks for readability.
                collect_file(path, 0, 0);
            }
            // If it's a directory matched by glob, descend into it
            else if (S_ISDIR(statbuf.st_mode))
            {
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

/** Copy buffer content to system clipboard */
bool copy_to_clipboard(const char* buffer)
{
    const char* cmd = NULL;
#ifdef __APPLE__
    cmd = "pbcopy";
#elif defined(__linux__)
    /* Prefer wl-copy if Wayland is likely running */
    if (getenv("WAYLAND_DISPLAY"))
    {
        cmd = "wl-copy";
    }
    else
    {
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

    if (!cmd)
    { /* Should only happen on Linux if both checks fail */
        fprintf(stderr, "Warning: Could not determine clipboard command on Linux.\n");
        return false;
    }

    FILE* pipe = popen(cmd, "w");
    if (!pipe)
    {
        perror("popen failed for clipboard command");
        return false;
    }

    fwrite(buffer, 1, strlen(buffer), pipe);

    /* Close the pipe and check status */
    if (pclose(pipe) == -1)
    {
        perror("pclose failed for clipboard command");
        return false;
    }
    /* Ignore command exit status for now, focus on pipe errors */
    return true;
}

/** Cleanup function to free memory before exit */
void cleanup(void)
{
    /* Arena cleanup will release system_instructions */
    /* Free dynamically allocated user instructions */
    if (user_instructions)
    {
        /* Pointer memory managed by arena; just reset */
        user_instructions = NULL;
    }

    for (int i = 0; i < num_processed_files; i++)
    {
        processed_files[i].path = NULL;
    }

    /* Free special file content */
    for (int i = 0; i < num_special_files; i++)
    {
        special_files[i].content = NULL;
    }

    /* Remove temporary file */
    if (strlen(temp_file_path) > 0)
    {
        unlink(temp_file_path);
    }

    /* Remove tree file if it exists */
    if (strlen(tree_file_path) > 0)
    {
        unlink(tree_file_path);
    }

    arena_destroy(&g_arena);
}
/* Define command-line options for getopt_long */
/* Moved #include <getopt.h> to the top */
static const struct option long_options[] = {
    {"help", no_argument, 0, 'h'},
    {"command", required_argument, 0, 'c'}, /* Takes an argument */
    {"system", optional_argument, 0, 's'},  /* Argument is optional (@file/@-/default) */
    {"files", no_argument, 0, 'f'},         /* Indicates file args follow */
    {"editor-comments", optional_argument, 0, 'e'},
    {"raw", no_argument, 0, 'R'},
    {"rank", no_argument, 0, 'r'},               /* Enable FileRank sorting */
    {"tree", no_argument, 0, 't'},               /* Generate complete directory tree */
    {"filtered-tree", no_argument, 0, 'T'},      /* Generate file tree for specified files */
    {"tree-only", no_argument, 0, 'O'},          /* Generate tree only without file content */
    {"level", required_argument, 0, 'L'},        /* Max depth for tree display */
    {"output", optional_argument, 0, 'o'},       /* Output to stdout or file instead of clipboard */
    {"stdout", optional_argument, 0, 'o'},       /* Alias for --output */
    {"debug", no_argument, 0, 'd'},              /* Enable debug output */
    {"no-gitignore", no_argument, 0, 1},         /* Use a value > 255 for long-only */
    {"ignore-config", no_argument, 0, 2},        /* Ignore config file */
    {"token-budget", required_argument, 0, 400}, /* Token budget limit */
    {"token-model", required_argument, 0, 401},  /* Model for token counting */
    {"token-diagnostics", optional_argument, 0, 402}, /* Token count diagnostics */
    {"filerank-debug", no_argument, 0, 403},          /* FileRank debug output */
    {"filerank-weight", required_argument, 0, 404},   /* FileRank custom weights */
    {"keywords", required_argument, 0, 405},          /* Keywords boost specification */
    {"filerank-cutoff", required_argument, 0, 406},   /* FileRank cutoff specification */
    {"exclude", required_argument, 0, 'x'},           /* Exclude pattern */
    {0, 0, 0, 0}                                      /* Terminator */
};
static bool s_flag_used = false;                 /* Track if -s was used */
static bool c_flag_used = false;                 /* Track if -c/-C/--command was used */
static bool e_flag_used = false;                 /* Track if -e was used */
static bool r_flag_used = false;                 /* Track if -r was used */
static bool g_stdin_consumed_for_option = false; /* Track if stdin was used for @- */
static bool ignore_config_flag = false;          /* Track if --ignore-config was used */
/* No specific CLI flag for copy yet, so no copy_flag_used needed */
static char* s_template_name = NULL; /* Template name for -s flag */
static char* e_template_name = NULL; /* Template name for -e flag */

/* Helper to handle argument for -c/--command */
static void handle_command_arg(const char* arg)
{
    /* Accept three syntactic forms:
       -cTEXT      (posix short-option glued)
       -c TEXT     (separate argv element)
       -c=TEXT     (equals-form, common in our tests)
     */
    /* getopt_long ensures arg is non-NULL if the option requires an argument */
    /* unless opterr is zero and it returns '?' or ':'. We handle NULL defensively. */
    if (!arg)
        fatal("Error: -c/--command requires an argument");

    /* Allow and strip the leading '=' for equals-form. */
    if (arg[0] == '=')
        arg++;

    /* After possible stripping, the argument must be non-empty. */
    if (*arg == '\0')
        fatal("Error: -c/--command requires a non-empty argument");

    c_flag_used = true; /* Track that CLI flag was used */
    if (user_instructions)
    {
        user_instructions = NULL;
    }

    if (arg[0] == '@')
    {
        /* Reject bare “-c@” (empty path). */
        if (arg[1] == '\0')
            fatal("Error: -c/--command requires a non-empty argument after @");

        if (strcmp(arg, "@-") == 0)
        { /* read from STDIN */
            if (isatty(STDIN_FILENO))
            {
                fprintf(
                    stderr,
                    "Reading instructions from terminal. Enter text and press Ctrl+D when done.\n");
            }
            user_instructions = slurp_stream(stdin);
            g_stdin_consumed_for_option = true; /* Mark stdin as used */
            if (!user_instructions)
                fatal("Error reading instructions from stdin: %s",
                      ferror(stdin) ? strerror(errno) : "Out of memory");
            file_mode = 1; /* Set file mode globally */
        }
        else
        { /* read from file */
            user_instructions = slurp_file(arg + 1);
            if (!user_instructions)
                fatal("Cannot open or read instruction file '%s': %s", arg + 1, strerror(errno));
        }
    }
    else
    {
        user_instructions = arena_strdup_safe(&g_arena, arg);
        if (!user_instructions)
            fatal("Out of memory duplicating -c argument");
    }
}

/* Helper to handle argument for -s/--system */
static void handle_system_arg(const char* arg)
{
    system_instructions = NULL; /* Reset before handling */
    s_template_name = NULL;     /* Reset template name */

    /* Case 1: -s without argument (optarg is NULL) -> Mark flag used, prompt loaded later */
    if (arg == NULL)
    {
        s_flag_used = true; /* Track that CLI flag was used */
        return;             /* Nothing more to do */
    }

    /* Case 2: -s with argument starting with '@' */
    if (arg[0] == '@')
    {
        if (strcmp(arg, "@-") == 0)
        { /* Read from stdin */
            if (isatty(STDIN_FILENO))
            {
                fprintf(stderr, "Reading system instructions from terminal. Enter text and press "
                                "Ctrl+D when done.\n");
            }
            system_instructions = slurp_stream(stdin);
            g_stdin_consumed_for_option = true; /* Mark stdin as used */
            if (!system_instructions)
                fatal("Error reading system instructions from stdin: %s",
                      ferror(stdin) ? strerror(errno) : "Out of memory");
            s_flag_used = true; /* Track that CLI flag was used */
            file_mode = 1;      /* Set file mode globally */
        }
        else
        {                                   /* Read from file */
            const char* filename = arg + 1; /* skip '@' */
            char* expanded_path = expand_tilde_path(filename);
            if (!expanded_path)
            {
                fatal("Cannot expand path '%s': %s", filename, strerror(errno));
            }
            system_instructions = slurp_file(expanded_path);
            if (!system_instructions)
                fatal("Cannot open or read system prompt file '%s': %s", expanded_path,
                      strerror(errno));
            s_flag_used = true; /* Track that CLI flag was used */
        }
    }
    else if (arg[0] == ':' && arg[1] != '\0')
    {
        /* Case 3: -s:template_name -> Use named template */
        s_template_name = arena_strdup_safe(&g_arena, arg + 1); /* skip ':' */
        if (!s_template_name)
            fatal("Out of memory duplicating template name");
        s_flag_used = true;
    }
    else
    {
        /* Case 4: -s with inline text -> Use as system instructions */
        system_instructions = arena_strdup_safe(&g_arena, arg);
        if (!system_instructions)
            fatal("Out of memory duplicating -s argument");
        s_flag_used = true; /* Track that CLI flag was used */
    }
}

/* Helper to handle argument for -e/--editor-comments */
static void handle_editor_arg(const char* arg)
{
    e_template_name = NULL; /* Reset template name */

    /* Case 1: -e without argument -> Mark flag used, guide loaded from config later */
    if (arg == NULL)
    {
        want_editor_comments = true;
        e_flag_used = true;
        /* Don't set custom_response_guide here - let config loading handle it */
        return;
    }

    /* Case 2: -e with argument starting with '@' */
    if (arg[0] == '@')
    {
        if (strcmp(arg, "@-") == 0)
        { /* Read from stdin */
            if (isatty(STDIN_FILENO))
            {
                fprintf(stderr, "Reading custom response guide from terminal. Enter text and press "
                                "Ctrl+D when done.\n");
            }
            custom_response_guide = slurp_stream(stdin);
            g_stdin_consumed_for_option = true; /* Mark stdin as used */
            if (!custom_response_guide)
                fatal("Error reading response guide from stdin: %s",
                      ferror(stdin) ? strerror(errno) : "Out of memory");
            want_editor_comments = true;
            e_flag_used = true;
            file_mode = 1; /* Set file mode globally */
        }
        else
        {                                   /* Read from file */
            const char* filename = arg + 1; /* skip '@' */
            char* expanded_path = expand_tilde_path(filename);
            if (!expanded_path)
            {
                fatal("Cannot expand path '%s': %s", filename, strerror(errno));
            }
            custom_response_guide = slurp_file(expanded_path);
            if (!custom_response_guide)
                fatal("Cannot open or read response guide file '%s': %s", expanded_path,
                      strerror(errno));
            want_editor_comments = true;
            e_flag_used = true;
        }
    }
    else if (arg[0] == ':' && arg[1] != '\0')
    {
        /* Case 3: -e:template_name -> Use named template */
        e_template_name = arena_strdup_safe(&g_arena, arg + 1); /* skip ':' */
        if (!e_template_name)
            fatal("Out of memory duplicating template name");
        want_editor_comments = true;
        e_flag_used = true;
    }
    else
    {
        /* Case 4: -e with inline text -> Use as custom response guide */
        custom_response_guide = arena_strdup_safe(&g_arena, arg);
        if (!custom_response_guide)
            fatal("Out of memory duplicating -e argument");
        want_editor_comments = true;
        e_flag_used = true;
    }
}

/* Helper to handle argument for -o/--output */
static void handle_output_arg(const char* arg)
{
    /* Always disable clipboard when -o is used */
    g_effective_copy_to_clipboard = false;

    /* Case 1: -o without argument -> Output to stdout */
    if (arg == NULL)
    {
        g_output_file = NULL; /* stdout is the default */
        return;
    }

    /* Case 2: -o with argument -> Output to file */
    const char* path = arg;

    if (path[0] == '@')
    {
        path++; /* Support legacy -o@FILE form */
        if (*path == '\0')
        {
            fatal("Error: -o@ requires a filename after @");
        }
    }

    if (*path == '\0')
    {
        fatal("Error: -o/--output requires a non-empty filename");
    }

    g_output_file = arena_strdup_safe(&g_arena, path);
    if (!g_output_file)
        fatal("Out of memory duplicating output filename");
}

/** Check if character is word boundary */
static bool is_word_boundary(char c)
{
    return !isalnum(c) && c != '_';
}

/** Case-insensitive word boundary search */
static int count_word_hits(const char* haystack, const char* needle)
{
    int count = 0;
    size_t needle_len = strlen(needle);
    const char* pos = haystack;

    while ((pos = strcasestr(pos, needle)) != NULL)
    {
        /* Check word boundaries */
        bool start_ok = (pos == haystack || is_word_boundary(*(pos - 1)));
        bool end_ok = is_word_boundary(*(pos + needle_len));

        if (start_ok && end_ok)
        {
            count++;
        }
        pos++;
    }

    return count;
}

/** Tokenize query into lowercase words */
static char** tokenize_query(const char* query, int* num_tokens)
{
    if (!query || !num_tokens)
    {
        *num_tokens = 0;
        return NULL;
    }

    /* Create a working copy */
    size_t query_len = strlen(query);
    char* work = arena_push_array_safe(&g_arena, char, query_len + 1);
    if (!work)
    {
        *num_tokens = 0;
        return NULL;
    }
    strcpy(work, query);

    /* Convert to lowercase */
    for (size_t i = 0; i < query_len; i++)
    {
        work[i] = tolower((unsigned char)work[i]);
    }

    /* Count tokens first */
    int count = 0;
    char* temp = arena_push_array_safe(&g_arena, char, query_len + 1);
    if (!temp)
    {
        *num_tokens = 0;
        return NULL;
    }
    strcpy(temp, work);

    char* token = strtok(temp, " \t\n\r.,;:!?()[]{}\"'`~@#$%^&*+=|\\/<>");
    while (token)
    {
        count++;
        token = strtok(NULL, " \t\n\r.,;:!?()[]{}\"'`~@#$%^&*+=|\\/<>");
    }

    if (count == 0)
    {
        *num_tokens = 0;
        return NULL;
    }

    /* Allocate token array */
    char** tokens = arena_push_array_safe(&g_arena, char*, count);
    if (!tokens)
    {
        *num_tokens = 0;
        return NULL;
    }

    /* Tokenize again to fill array */
    int idx = 0;
    token = strtok(work, " \t\n\r.,;:!?()[]{}\"'`~@#$%^&*+=|\\/<>");
    while (token && idx < count)
    {
        tokens[idx++] = token;
        token = strtok(NULL, " \t\n\r.,;:!?()[]{}\"'`~@#$%^&*+=|\\/<>");
    }

    *num_tokens = idx;
    return tokens;
}

/** Compute threshold based on cutoff specification */
static double compute_filerank_threshold(const char* spec, FileRank* ranks, int num_files)
{
    double threshold = -DBL_MAX;

    if (!spec || num_files == 0)
    {
        return threshold;
    }

    if (strncmp(spec, "ratio:", 6) == 0)
    {
        double alpha = atof(spec + 6);
        if (alpha > 0 && alpha <= 1.0)
        {
            threshold = ranks[0].score * alpha;
        }
    }
    else if (strncmp(spec, "topk:", 5) == 0)
    {
        int k = atoi(spec + 5);
        if (k > 0 && k < num_files)
        {
            threshold = ranks[k - 1].score;
        }
        else if (k >= num_files)
        {
            /* Keep all files if k >= num_files */
            threshold = -DBL_MAX;
        }
    }
    else if (strncmp(spec, "percentile:", 11) == 0)
    {
        int p = atoi(spec + 11);
        if (p > 0 && p <= 100)
        {
            int idx = ((100 - p) * num_files) / 100;
            if (idx < num_files)
            {
                threshold = ranks[idx].score;
            }
        }
    }
    else if (strcmp(spec, "auto") == 0)
    {
        /* Knee/elbow detection */
        double max_drop = 0;
        int knee = num_files - 1;
        for (int i = 0; i < num_files - 1; i++)
        {
            double drop = ranks[i].score - ranks[i + 1].score;
            if (drop > max_drop)
            {
                max_drop = drop;
                knee = i + 1;
            }
        }
        if (knee < num_files)
        {
            threshold = ranks[knee].score;
        }
    }

    /* Always discard files with score <= 0 */
    if (threshold < 0)
    {
        threshold = 0;
    }

    return threshold;
}

/** Compare FileRank entries by score (descending) */
static int compare_filerank(const void* a, const void* b)
{
    const FileRank* ra = (const FileRank*)a;
    const FileRank* rb = (const FileRank*)b;

    /* Sort by score descending */
    if (ra->score > rb->score)
        return -1;
    if (ra->score < rb->score)
        return 1;

    /* If scores are equal, maintain original order (stable sort) */
    return 0;
}

/** Rank files using TF-IDF scoring with path/content hits */
/* WHY: Prioritizes files with unique terms (TF-IDF) while considering
 * path relevance and penalizing large files for better context selection */
void rank_files(const char* query, FileRank* ranks, int num_files)
{
    /* Parse query into tokens */
    int num_tokens;
    char** tokens = tokenize_query(query, &num_tokens);

    if (!tokens || num_tokens == 0)
    {
        /* No query tokens - all scores remain 0 */
        for (int i = 0; i < num_files; i++)
        {
            ranks[i].score = 0.0;
        }
        return;
    }

    /* Slice 3: First pass - calculate document frequency for each term */
    int* doc_freq = calloc(num_tokens, sizeof(int));
    if (!doc_freq)
    {
        /* Fallback to basic scoring if allocation fails */
        goto basic_scoring;
    }

    for (int i = 0; i < num_files; i++)
    {
        /* Track which terms appear in this document */
        int* term_found = calloc(num_tokens, sizeof(int));
        if (!term_found)
        {
            free(doc_freq);
            goto basic_scoring;
        }

        /* Check path for terms */
        for (int j = 0; j < num_tokens; j++)
        {
            if (count_word_hits(ranks[i].path, tokens[j]) > 0)
            {
                term_found[j] = 1;
            }
        }

        /* Check content for terms */
        FILE* f = fopen(ranks[i].path, "r");
        if (f && !is_binary(f))
        {
            char buffer[4096];
            rewind(f);
            int current_line = 1;
            const ProcessedFile* pf = NULL;
            for (int k = 0; k < num_processed_files; k++)
            {
                if (strcmp(processed_files[k].path, ranks[i].path) == 0)
                {
                    pf = &processed_files[k];
                    break;
                }
            }
            int start_line = pf ? pf->start_line : 0;
            int end_line = pf ? pf->end_line : 0;

            while (fgets(buffer, sizeof(buffer), f))
            {
                if (start_line > 0 && current_line < start_line)
                {
                    if (strchr(buffer, '\n'))
                        current_line++;
                    continue;
                }
                if (end_line > 0 && current_line > end_line)
                {
                    break;
                }

                for (int j = 0; j < num_tokens; j++)
                {
                    if (!term_found[j] && count_word_hits(buffer, tokens[j]) > 0)
                    {
                        term_found[j] = 1;
                    }
                }

                if (strchr(buffer, '\n'))
                    current_line++;
            }
            fclose(f);
        }
        else if (f)
        {
            fclose(f);
        }

        /* Update document frequency counts */
        for (int j = 0; j < num_tokens; j++)
        {
            if (term_found[j])
            {
                doc_freq[j]++;
            }
        }

        free(term_found);
    }

    /* Second pass - calculate TF-IDF scores */
    for (int i = 0; i < num_files; i++)
    {
        double path_hits = 0;
        double content_hits = 0;
        double tfidf_score = 0;

        /* Count hits in path */
        for (int j = 0; j < num_tokens; j++)
        {
            double w = kw_weight_for(tokens[j]);
            path_hits += w * count_word_hits(ranks[i].path, tokens[j]);
        }

        /* Get file size for penalty calculation */
        struct stat st;
        if (stat(ranks[i].path, &st) == 0)
        {
            ranks[i].bytes = st.st_size;
        }
        else
        {
            ranks[i].bytes = 0;
        }

        /* Read file content to count hits and calculate TF-IDF */
        FILE* f = fopen(ranks[i].path, "r");
        if (f)
        {
            /* Check if binary */
            if (!is_binary(f))
            {
                /* Count total words for TF calculation */
                int total_words = 0;
                int* term_freq = calloc(num_tokens, sizeof(int));

                if (term_freq)
                {
                    /* Read content and count term frequencies */
                    char buffer[4096];
                    char buffer_copy[4096];
                    rewind(f);
                    while (fgets(buffer, sizeof(buffer), f))
                    {
                        /* Make a copy for word counting since strtok modifies the buffer */
                        strcpy(buffer_copy, buffer);

                        /* Simple word count - count space-separated tokens */
                        char* word = strtok(buffer_copy, " \t\n\r");
                        while (word)
                        {
                            total_words++;
                            word = strtok(NULL, " \t\n\r");
                        }

                        /* Count term hits on original buffer */
                        for (int j = 0; j < num_tokens; j++)
                        {
                            int hits = count_word_hits(buffer, tokens[j]);
                            term_freq[j] += hits;
                            double w = kw_weight_for(tokens[j]);
                            content_hits += w * hits;
                        }
                    }

                    /* Calculate TF-IDF score */
                    if (total_words > 0)
                    {
                        for (int j = 0; j < num_tokens; j++)
                        {
                            if (term_freq[j] > 0 && doc_freq[j] > 0)
                            {
                                double tf = (double)term_freq[j] / total_words;
                                double idf = log((double)num_files / doc_freq[j]);
                                double w = kw_weight_for(tokens[j]);
                                tfidf_score += w * tf * idf;
                            }
                        }
                    }

                    free(term_freq);
                }
                else
                {
                    /* Fallback to simple counting */
                    char buffer[4096];
                    rewind(f);
                    int current_line = 1;
                    const ProcessedFile* pf = NULL;
                    for (int k = 0; k < num_processed_files; k++)
                    {
                        if (strcmp(processed_files[k].path, ranks[i].path) == 0)
                        {
                            pf = &processed_files[k];
                            break;
                        }
                    }
                    int start_line = pf ? pf->start_line : 0;
                    int end_line = pf ? pf->end_line : 0;

                    while (fgets(buffer, sizeof(buffer), f))
                    {
                        if (start_line > 0 && current_line < start_line)
                        {
                            if (strchr(buffer, '\n'))
                                current_line++;
                            continue;
                        }
                        if (end_line > 0 && current_line > end_line)
                        {
                            break;
                        }

                        for (int j = 0; j < num_tokens; j++)
                        {
                            double w = kw_weight_for(tokens[j]);
                            content_hits += w * count_word_hits(buffer, tokens[j]);
                        }

                        if (strchr(buffer, '\n'))
                            current_line++;
                    }
                }
            }
            fclose(f);
        }

        /* Calculate score: TF-IDF weight + content_hits + path_weight*path_hits -
         * size_weight*(bytes/1MiB) */
        double size_penalty = g_filerank_weight_size * (ranks[i].bytes / (1024.0 * 1024.0));
        ranks[i].score = tfidf_score * g_filerank_weight_tfidf +
                         content_hits * g_filerank_weight_content +
                         g_filerank_weight_path * path_hits - size_penalty;
    }

    free(doc_freq);
    return;

basic_scoring:
    /* Fallback: basic scoring without TF-IDF */
    for (int i = 0; i < num_files; i++)
    {
        double path_hits = 0;
        double content_hits = 0;

        /* Count hits in path */
        for (int j = 0; j < num_tokens; j++)
        {
            double w = kw_weight_for(tokens[j]);
            path_hits += w * count_word_hits(ranks[i].path, tokens[j]);
        }

        /* Get file size for penalty calculation */
        struct stat st;
        if (stat(ranks[i].path, &st) == 0)
        {
            ranks[i].bytes = st.st_size;
        }
        else
        {
            ranks[i].bytes = 0;
        }

        /* Read file content to count hits */
        FILE* f = fopen(ranks[i].path, "r");
        if (f)
        {
            /* Check if binary */
            if (!is_binary(f))
            {
                /* Read content and count hits */
                char buffer[4096];
                rewind(f);
                while (fgets(buffer, sizeof(buffer), f))
                {
                    for (int j = 0; j < num_tokens; j++)
                    {
                        double w = kw_weight_for(tokens[j]);
                        content_hits += w * count_word_hits(buffer, tokens[j]);
                    }
                }
            }
            fclose(f);
        }

        /* Calculate score: content_hits + path_weight*path_hits - size_weight*(bytes/1MiB) */
        double size_penalty = g_filerank_weight_size * (ranks[i].bytes / (1024.0 * 1024.0));
        ranks[i].score = content_hits * g_filerank_weight_content +
                         g_filerank_weight_path * path_hits - size_penalty;
    }
}

/** Main function - program entry point */
/* INVARIANTS: Arena allocated once, cleanup registered early,
 * config loaded before file processing, stdin consumed only once */
int main(int argc, char* argv[])
{
    g_argv0 = argv[0];                    /* Store for get_executable_dir fallback */
    bool allow_empty_context = false;     /* Can we finish with no file content? */
    /* ConfigSettings loaded_settings; */ /* Config loading has been removed */
    /* Register cleanup handler */
    atexit(cleanup); /* Register cleanup handler early */

    g_arena = arena_create(MiB(256));
    if (!g_arena.base)
        fatal("Failed to allocate arena");

    /* Check for 'get' subcommand before other processing */
    if (argc > 1 && strcmp(argv[1], "get") == 0)
    {
        if (argc < 3)
        {
            fprintf(stderr, "Usage: %s get <uuid>\n", argv[0]);
            cleanup();
            return 1;
        }
        
        char* content = load_prompt(argv[2], &g_arena);
        if (!content)
        {
            fprintf(stderr, "Prompt not found: %s\n", argv[2]);
            cleanup();
            return 1;
        }
        
        /* Copy to clipboard */
        if (!copy_to_clipboard(content))
        {
            fprintf(stderr, "Clipboard copy failed; outputting to stdout.\n");
            printf("%s", content);
        }
        else
        {
            fprintf(stderr, "Retrieved and copied prompt %s\n", argv[2]);
        }
        
        cleanup();
        return 0;
    }

    /* Set executable directory for tokenizer library loading */
    char* exe_dir = get_executable_dir();
    if (exe_dir)
    {
        llm_set_executable_dir(exe_dir);
    }

    /* Create temporary file for output assembly */
    strcpy(temp_file_path, TEMP_FILE_TEMPLATE);
    int fd = mkstemp(temp_file_path);
    if (fd == -1)
    {
        fatal("Failed to create temporary file: %s", strerror(errno));
    }
    temp_file = fdopen(fd, "w");
    if (!temp_file)
    {
        /* fd is still open here, cleanup() will handle unlinking */
        fatal("Failed to open temporary file stream: %s", strerror(errno));
    }

    /* Silence getopt_long(); we'll print the expected diagnostic ourselves. */
    opterr = 0;

    /* Use getopt_long for robust and extensible argument parsing. */
    /* This replaces the manual strcmp/strncmp chain, reducing complexity */
    /* and adhering to the "minimize execution paths" principle. */
    int opt;
    char** explicit_files = arena_push_array_safe(&g_arena, char*, MAX_FILES);
    int explicit_file_count = 0;
    /* Add 'C' to the short options string. It takes no argument. */
    /* Add 'b:' for short form of --token-budget and 'D::' for token diagnostics */
    while ((opt = getopt_long(argc, argv, "hc:s::fRe::CtdTOL:o::b:D::k:x:r", long_options, NULL)) !=
           -1)
    {
        switch (opt)
        {
        case 'h':        /* -h or --help */
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
            if (optarg == NULL   /* no glued arg */
                && optind < argc /* something left */
                && argv[optind][0] != '-')
            {                                      /* not next flag  */
                handle_system_arg(argv[optind++]); /* treat as arg, consume */
            }
            else
            {
                handle_system_arg(optarg); /* glued/NULL */
            }
            break;
        case 'f': /* -f or --files */
            file_mode = 1;
            while (optind < argc && argv[optind] && argv[optind][0] != '-')
            {
                if (explicit_file_count >= MAX_FILES)
                {
                    fprintf(stderr,
                            "Error: Too many files specified via -f (maximum %d)\n",
                            MAX_FILES);
                    return 1;
                }
                explicit_files[explicit_file_count++] = argv[optind++];
            }
            break;
        case 'e': /* -e or --editor-comments with optional argument */
            /* Handle similar to -s: check if there's a non-option argument following */
            if (optarg == NULL   /* no glued arg */
                && optind < argc /* something left */
                && argv[optind][0] != '-')
            {                                      /* not next flag  */
                handle_editor_arg(argv[optind++]); /* treat as arg, consume */
            }
            else
            {
                handle_editor_arg(optarg); /* glued/NULL */
            }
            break;
        case 'r': /* -r or --rank */
            enable_filerank = true;
            break;
        case 'R': /* -R or --raw */
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
            if (!global_tree_only && !tree_only)
            {
                tree_only = true;
            }
            file_mode = 1; /* Enable file mode to process files */
            break;
        case 'L': /* -L or --level */
            if (!optarg)
            {
                fprintf(stderr, "Error: -L/--level requires a numeric argument\n");
                return 1;
            }
            tree_max_depth = (int)strtol(optarg, NULL, 10);
            if (tree_max_depth <= 0)
            {
                fprintf(stderr, "Error: Invalid tree depth: %s\n", optarg);
                return 1;
            }
            break;
        case 'o': /* -o or --output with optional file argument */
            /* Handle similar to -s and -e: check if there's a non-option argument following */
            if (optarg == NULL   /* no glued arg */
                && optind < argc /* something left */
                && argv[optind][0] != '-')
            {                                      /* not next flag  */
                handle_output_arg(argv[optind++]); /* treat as arg, consume */
            }
            else
            {
                handle_output_arg(optarg); /* glued/NULL */
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
            if (!optarg)
            {
                fprintf(stderr, "Error: -b/--token-budget requires a numeric argument\n");
                return 1;
            }
            g_token_budget = (size_t)strtoull(optarg, NULL, 10);
            if (g_token_budget == 0)
            {
                fprintf(stderr, "Error: Invalid token budget: %s\n", optarg);
                return 1;
            }
            break;
        case 'D': /* -D deprecated - diagnostics are now shown by default */
            /* For backward compatibility, just ignore this flag */
            if (optarg == NULL   /* no glued arg */
                && optind < argc /* something left */
                && argv[optind][0] != '-')
            {             /* not next flag  */
                optind++; /* consume the argument */
            }
            break;
        case 400: /* --token-budget */
            if (!optarg)
            {
                fprintf(stderr, "Error: --token-budget requires a numeric argument\n");
                return 1;
            }
            g_token_budget = (size_t)strtoull(optarg, NULL, 10);
            if (g_token_budget == 0)
            {
                fprintf(stderr, "Error: Invalid token budget: %s\n", optarg);
                return 1;
            }
            break;
        case 401: /* --token-model */
            if (!optarg)
            {
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
        case 404: /* --filerank-weight */
            if (!optarg)
            {
                fprintf(stderr, "Error: --filerank-weight requires an argument (e.g., "
                                "path:2,content:1,size:0.05,tfidf:10)\n");
                return 1;
            }
            if (!parse_filerank_weights(optarg))
            {
                fprintf(stderr, "Error: Failed to parse filerank weights from '%s'\n", optarg);
                return 1;
            }
            break;
        case 'k': /* -k or --keywords */
        case 405: /* --keywords (long form) */
            if (!optarg)
            {
                fprintf(stderr, "Error: -k/--keywords requires an argument\n");
                return 1;
            }
            g_keywords_flag_used = true;
            if (!parse_keywords(optarg))
            {
                fprintf(stderr, "Error: Failed to parse keywords from '%s'\n", optarg);
                return 1;
            }
            break;
        case 406: /* --filerank-cutoff */
            if (!optarg)
            {
                fprintf(stderr, "Error: --filerank-cutoff requires an argument (e.g., ratio:0.15, "
                                "topk:10, percentile:30, auto)\n");
                return 1;
            }
            g_filerank_cutoff_spec = arena_strdup_safe(&g_arena, optarg);
            break;
        case 'x': /* -x or --exclude */
            if (!optarg)
            {
                fprintf(stderr, "Error: -x/--exclude requires a pattern argument\n");
                return 1;
            }
            /* Forward declaration - will be implemented soon */
            add_cli_exclude_pattern(optarg);
            break;
        case '?': /* Unknown option OR missing required argument */
            /* optopt contains the failing option character */
            if (optopt == 'c')
            {
                /* Replicate the standard missing-argument banner */
                fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], optopt);
            }
            else if (optopt == 406)
            {
                /* Special case for --filerank-cutoff missing argument */
                fprintf(stderr, "Error: --filerank-cutoff requires an argument (e.g., ratio:0.15, "
                                "topk:10, percentile:30, auto)\n");
            }
            else if (isprint(optopt))
            {
                /* Handle other unknown options */
                fprintf(stderr, "Unknown option `-%c'.\n", optopt);
            }
            else
            {
                /* Handle unknown options with non-printable characters */
                fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
            }
            /* Match canonical GNU wording and unit-test expectation:
             * normalise to "./<basename>" regardless of invocation path. */
            {
                /* basename() might modify its argument, so pass a copy if needed, */
                /* but POSIX standard says it *returns* a pointer, possibly static. */
                /* Use argv[0] directly, providing a default if it's NULL. */
                char* prog_base = basename(argv[0] ? argv[0] : "llm_ctx");
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

    if (!ignore_config_flag)
    {
        config_loaded = config_load(&loaded_settings, &g_arena);
        if (debug_mode && config_loaded)
        {
            config_debug_print(&loaded_settings);
        }
    }

    /* Apply configuration values if no CLI flags override them */
    if (config_loaded)
    {
        /* system_prompt_file - load from config if no direct content provided */
        if (!system_instructions)
        {
            char* prompt_file = NULL;

            /* Check if a template name was specified */
            if (s_template_name)
            {
                ConfigTemplate* tmpl = config_find_template(&loaded_settings, s_template_name);
                if (tmpl && tmpl->system_prompt_file)
                {
                    prompt_file = tmpl->system_prompt_file;
                }
                else
                {
                    fatal("Error: template '%s' not found or has no system_prompt_file in config",
                          s_template_name);
                }
            }
            else if (loaded_settings.system_prompt_file)
            {
                /* Use default system prompt file */
                prompt_file = loaded_settings.system_prompt_file;
            }

            if (prompt_file)
            {
                char* expanded_path = config_expand_path(prompt_file, &g_arena);
                if (expanded_path)
                {
                    system_instructions = slurp_file(expanded_path);
                    if (!system_instructions)
                    {
                        fprintf(stderr, "warning: config refers to %s (not found)\n",
                                expanded_path);
                    }
                }
            }
        }

        /* response_guide_file - only if -e flag was used and no direct content provided */
        if (e_flag_used && !custom_response_guide)
        {
            char* guide_file = NULL;

            /* Check if a template name was specified */
            if (e_template_name)
            {
                ConfigTemplate* tmpl = config_find_template(&loaded_settings, e_template_name);
                if (tmpl && tmpl->response_guide_file)
                {
                    guide_file = tmpl->response_guide_file;
                }
                else
                {
                    fatal("Error: template '%s' not found or has no response_guide_file in config",
                          e_template_name);
                }
            }
            else if (loaded_settings.response_guide_file)
            {
                /* Use default response guide file */
                guide_file = loaded_settings.response_guide_file;
            }

            if (guide_file)
            {
                char* expanded_path = config_expand_path(guide_file, &g_arena);
                if (expanded_path)
                {
                    custom_response_guide = slurp_file(expanded_path);
                    if (!custom_response_guide)
                    {
                        fprintf(stderr, "warning: config refers to %s (not found)\n",
                                expanded_path);
                    }
                    else
                    {
                        want_editor_comments = true; /* Enable editor comments if guide loaded */
                    }
                }
            }
        }

        /* copy_to_clipboard - will be applied after all flags are processed */

        /* token_budget - only if not set via CLI */
        if (loaded_settings.token_budget > 0 && g_token_budget == 96000)
        {
            g_token_budget = loaded_settings.token_budget;
        }

        /* FileRank weights - only if not set via CLI */
        if (loaded_settings.filerank_weight_path >= 0.0)
        {
            g_filerank_weight_path = loaded_settings.filerank_weight_path;
        }
        if (loaded_settings.filerank_weight_content >= 0.0)
        {
            g_filerank_weight_content = loaded_settings.filerank_weight_content;
        }
        if (loaded_settings.filerank_weight_size >= 0.0)
        {
            g_filerank_weight_size = loaded_settings.filerank_weight_size;
        }
        if (loaded_settings.filerank_weight_tfidf >= 0.0)
        {
            g_filerank_weight_tfidf = loaded_settings.filerank_weight_tfidf;
        }
        /* FileRank cutoff - only if not set via CLI */
        if (loaded_settings.filerank_cutoff && !g_filerank_cutoff_spec)
        {
            g_filerank_cutoff_spec = arena_strdup_safe(&g_arena, loaded_settings.filerank_cutoff);
        }
    }
    /* --- Finalize copy_to_clipboard setting --- */
    /* Note: g_effective_copy_to_clipboard is already set to false if -o was used */
    /* Apply config setting only if -o was not used */
    if (g_effective_copy_to_clipboard && config_loaded && loaded_settings.copy_to_clipboard != -1)
    {
        g_effective_copy_to_clipboard = (loaded_settings.copy_to_clipboard == 1);
    }

    /* --- Finalize editor_comments setting (apply toggle logic) --- */
    /* Determine initial state (default false) */
    bool initial_want_editor_comments = false;
    /* Apply toggle if -e flag was used */
    if (e_flag_used)
    {
        want_editor_comments = !initial_want_editor_comments;
    }
    else
    {
        want_editor_comments = initial_want_editor_comments;
    }

    /* Determine if prompt-only output is allowed based on final settings */
    /* Allow if user instructions were given (-c/-C/--command) */
    /* OR if system instructions were explicitly set (via -s or config) */
    /* OR if editor comments were requested (via -e) */
    /* OR if stdin was consumed by an option like -c @- or -s @- */
    allow_empty_context = c_flag_used || s_flag_used || e_flag_used || g_stdin_consumed_for_option;

    /* Use default system instructions if none provided */
    if (!system_instructions && !raw_mode)
    {
        system_instructions = arena_strdup_safe(&g_arena, DEFAULT_SYSTEM_INSTRUCTIONS);
    }

    if (!raw_mode)
    {
        /* Add user instructions first, if provided */
        add_user_instructions(user_instructions);
        /* Add system instructions if provided */
        add_system_instructions(system_instructions);
        /* Add response guide (depends on user instructions and -e flag) */
        add_response_guide(user_instructions);
    }
    else if (user_instructions && *user_instructions)
    {
        /* In raw mode, just print user instructions without tags */
        fprintf(temp_file, "%s\n\n", user_instructions);
    }

    /* Load gitignore files if enabled */
    if (respect_gitignore)
    {
        /* Pre-condition: gitignore is enabled */
        assert(respect_gitignore == 1);
        load_all_gitignore_files();
        /* Post-condition: gitignore patterns may have been loaded */
        assert(respect_gitignore == 1);
    }

    /* Process input based on mode */
    if (file_mode)
    {
        for (int i = 0; i < explicit_file_count; i++)
        {
            process_pattern(explicit_files[i]);
        }

        /* Process files listed after options */
        if (explicit_file_count == 0 && file_args_start >= argc)
        {
            /* Check if stdin was already consumed by an @- option */
            bool used_stdin_for_args = g_stdin_consumed_for_option;
            bool tree_requested = tree_only || global_tree_only || tree_only_output;
            if (!used_stdin_for_args)
            {
                if (tree_requested)
                {
                    /* Default tree modes without explicit paths to the current directory */
                    process_pattern(".");
                }
                else
                {
                    /* -f flag likely provided but no files specified, or only options given */
                    fprintf(stderr, "Warning: File mode specified (-f or via @-) but no file "
                                    "arguments provided.\n");
                    /* Allow proceeding if stdin might have been intended but wasn't used for @- */
                    /* If stdin is not a tty, process it. Otherwise, exit if prompt-only not
                     * allowed. */
                    if (isatty(STDIN_FILENO))
                    {
                        fprintf(stderr, "No input provided.\n");
                        return 1; /* Exit if terminal and no files */
                    }
                    else
                    {
                        /* Fall through to process stdin if data is piped */
                        if (!process_stdin_content())
                        {
                            return 1; /* Exit on stdin processing error */
                        }
                    }
                }
            }
            /* If stdin *was* used for @-, proceed without file args is okay */
        }
        else
        {
            /* Process each remaining argument as a file/pattern */
            for (int i = file_args_start; i < argc; i++)
            {
                process_pattern(argv[i]);
            }
        }
    }
    else
    {
        /* Stdin mode (no -f, no @- used) */
        if (isatty(STDIN_FILENO))
        {
            /* If stdin is a terminal and we are not in file mode (which would be set by -f, -c @-,
             * -s @-, -C), and prompt-only output isn't allowed (no -c/-s/-e flags used), it means
             * the user likely forgot to pipe input or provide file arguments. Show help. */
            if (!allow_empty_context)
            {
                show_help(); /* Exits */
            } /* Otherwise, allow proceeding to generate prompt-only output */
        }
        else
        {
            /* Stdin is not a terminal (piped data), process it */
            if (!process_stdin_content())
            {
                return 1; /* Exit on stdin processing error */
            }
        }
    }

    /* Check if any files were found or if prompt-only output is allowed */
    if (files_found == 0 && !allow_empty_context)
    {
        fprintf(stderr, "No files to process\n");
        fclose(temp_file);
        unlink(temp_file_path);
        return 1;
    }

    /* Inform user if producing prompt-only output in interactive mode */
    if (files_found == 0 && allow_empty_context && isatty(STDERR_FILENO))
    {
        fprintf(stderr, "llm_ctx: No files or stdin provided; producing prompt-only output.\n");
    }

    /* Expand file tree to show full directory contents */
    if (file_tree_count > 0 && global_tree_only)
    {
        char* tree_root = find_common_prefix();

        /* For full tree, add the entire directory tree from the common root */
        if (strcmp(tree_root, ".") == 0)
        {
            /* If root is current dir, use the first file's top-level directory */
            if (file_tree_count > 0)
            {
                char first_dir[MAX_PATH];
                strcpy(first_dir, file_tree[0].path);
                char* first_slash = strchr(first_dir, '/');
                if (first_slash)
                {
                    *first_slash = '\0';
                    add_directory_tree(first_dir);
                }
                else
                {
                    add_directory_tree(".");
                }
            }
        }
        else
        {
            /* Add everything under the common root */
            add_directory_tree(tree_root);
        }
    }

    /* Generate and add file tree if -t, -T, or -O flag is passed */
    if (tree_only || global_tree_only || tree_only_output)
    {
        generate_file_tree();
    }

    /* FileRank array - declared at broader scope for budget handling */
    FileRank* ranks = NULL;

    /* Process codemap and file content unless tree_only_output is set */
    if (!tree_only_output)
    {

        /* Apply FileRank if we have files and a query and FileRank is enabled */
        if (num_processed_files > 0 && user_instructions && enable_filerank)
        {
            /* Allocate FileRank array */
            ranks = arena_push_array_safe(&g_arena, FileRank, num_processed_files);
            if (!ranks)
            {
                fatal("Out of memory allocating FileRank array");
            }

            /* Initialize FileRank structures */
            for (int i = 0; i < num_processed_files; i++)
            {
                ranks[i].path = processed_files[i].path;
                ranks[i].score = 0.0;
                ranks[i].bytes = 0;
                ranks[i].tokens = 0;
            }

            /* Call ranking function */
            rank_files(user_instructions, ranks, num_processed_files);

            /* Sort files by score (for budget-based selection) */
            qsort(ranks, num_processed_files, sizeof(FileRank), compare_filerank);

            /* Apply filerank cutoff if specified */
            if (g_filerank_cutoff_spec)
            {
                double threshold =
                    compute_filerank_threshold(g_filerank_cutoff_spec, ranks, num_processed_files);

                /* Filter files based on threshold */
                int kept = 0;
                for (int i = 0; i < num_processed_files; i++)
                {
                    if (ranks[i].score >= threshold)
                    {
                        ranks[kept++] = ranks[i];
                    }
                }

                if (kept < num_processed_files)
                {
                    fprintf(stderr, "FileRank cutoff (%s): threshold=%.2f, kept %d/%d files\n",
                            g_filerank_cutoff_spec, threshold, kept, num_processed_files);
                }

                num_processed_files = kept;
            }

            /* Debug output if requested - show sorted order */
            if (g_filerank_debug)
            {
                fprintf(stderr, "FileRank (query: \"%s\")\n", user_instructions);

                /* Show keyword boosts if any */
                if (g_kw_boosts_len > 0)
                {
                    fprintf(stderr, "Keywords: ");
                    for (int i = 0; i < g_kw_boosts_len; i++)
                    {
                        if (i > 0)
                            fprintf(stderr, ", ");
                        double factor = g_kw_boosts[i].weight / KEYWORD_BASE_MULTIPLIER;
                        fprintf(stderr, "%s:%.1fx(=%.0f)", g_kw_boosts[i].token, factor,
                                g_kw_boosts[i].weight);
                    }
                    fprintf(stderr, "\n");
                }

                for (int i = 0; i < num_processed_files; i++)
                {
                    fprintf(stderr, "  %.2f  %s\n", ranks[i].score, ranks[i].path);
                }
            }

            /* Update processed_files array to match sorted order */
            for (int i = 0; i < num_processed_files; i++)
            {
                /* Preserve ranges alongside path when reordering */
                for (int j = i; j < num_processed_files; j++)
                {
                    if (strcmp(processed_files[j].path, ranks[i].path) == 0)
                    {
                        ProcessedFile tmp = processed_files[i];
                        processed_files[i] = processed_files[j];
                        processed_files[j] = tmp;
                        break;
                    }
                }
            }
        }

        /* First pass: try outputting all files */
        for (int i = 0; i < num_processed_files; i++)
        {
            output_file_content(&processed_files[i], temp_file);
        }

        /* Add closing file_context tag */
        if (wrote_file_context)
            fprintf(temp_file, "</file_context>\n");
    }

    /* Flush and close the temp file */
    fclose(temp_file);

    /* --- Token Counting and Budget Check --- */
    /* Read the content first to count tokens if needed */
    char* final_content = slurp_file(temp_file_path);
    if (!final_content)
    {
        perror("Failed to read temporary file");
        return 1;
    }

    /* Always try to count tokens if tokenizer is available */
    size_t total_tokens = llm_count_tokens(final_content, g_token_model);
    if (total_tokens == SIZE_MAX)
    {
        fatal("Tokenizer failed to count tokens for assembled context");
    }

    /* Token counting succeeded - always display usage */
    fprintf(stderr, "Token usage: %zu / %zu (%zu%% of budget)\n", total_tokens, g_token_budget,
            (total_tokens * 100) / g_token_budget);

    /* Check budget */
    if (total_tokens > g_token_budget)
    {
        if (ranks && user_instructions)
        {
            fprintf(
                stderr,
                "\nBudget exceeded (%zu > %zu) - using FileRank to select most relevant files\n",
                total_tokens, g_token_budget);
            fprintf(stderr, "Query: \"%s\"\n", user_instructions);

            fclose(fopen(temp_file_path, "w"));
            temp_file = fopen(temp_file_path, "w");
            if (!temp_file)
            {
                fatal("Failed to reopen temp file for FileRank selection");
            }

            if (user_instructions)
            {
                fprintf(temp_file, "<user_instructions>\n%s\n</user_instructions>\n\n",
                        user_instructions);
            }
            if (system_instructions)
            {
                add_system_instructions(system_instructions);
            }
            if (custom_response_guide)
            {
                fprintf(temp_file, "<response_guide>\n%s\n</response_guide>\n\n",
                        custom_response_guide);
            }

            if ((tree_only || global_tree_only) && strlen(tree_file_path) > 0)
            {
                FILE* tree_f = fopen(tree_file_path, "r");
                if (tree_f)
                {
                    fprintf(temp_file, "<file_tree>\n");
                    char buffer[4096];
                    size_t bytes_read;
                    while ((bytes_read = fread(buffer, 1, sizeof(buffer), tree_f)) > 0)
                    {
                        fwrite(buffer, 1, bytes_read, temp_file);
                    }
                    fprintf(temp_file, "</file_tree>\n\n");
                    fclose(tree_f);
                }
            }

            size_t running_tokens = 0;
            size_t base_tokens = 0;
            int files_included = 0;

            fflush(temp_file);
            char* base_content = slurp_file(temp_file_path);
            if (base_content)
            {
                base_tokens = llm_count_tokens(base_content, g_token_model);
                if (base_tokens == SIZE_MAX)
                {
                    fatal("Tokenizer failed while counting base context tokens");
                }
                running_tokens = base_tokens;
            }

            fprintf(temp_file, "<file_context>\n\n");

            for (int i = 0; i < num_processed_files; i++)
            {
                size_t mark = arena_get_mark(&g_arena);
                char* file_content = arena_push_array_safe(&g_arena, char, 1024 * 1024);
                if (!file_content)
                {
                    arena_set_mark(&g_arena, mark);
                    continue;
                }

                FILE* mem_file = fmemopen(file_content, 1024 * 1024, "w");
                if (mem_file)
                {
                    output_file_content(&processed_files[i], mem_file);
                    fclose(mem_file);

                    size_t file_tokens = llm_count_tokens(file_content, g_token_model);
                    if (file_tokens == SIZE_MAX)
                    {
                        fatal("Tokenizer failed while counting '%s'", processed_files[i].path);
                    }

                    if (running_tokens + file_tokens + 50 <= g_token_budget)
                    {
                        fprintf(temp_file, "%s", file_content);
                        running_tokens += file_tokens;
                        files_included++;
                        ranks[i].tokens = file_tokens;
                    }
                    else
                    {
                        fprintf(stderr,
                                "Skipping remaining %d files - adding '%s' would exceed budget\n",
                                num_processed_files - i, processed_files[i].path);
                        arena_set_mark(&g_arena, mark);
                        break;
                    }
                }

                arena_set_mark(&g_arena, mark);
            }

            fprintf(temp_file, "</file_context>\n");
            fclose(temp_file);

            final_content = slurp_file(temp_file_path);
            if (final_content)
            {
                total_tokens = llm_count_tokens(final_content, g_token_model);
                if (total_tokens == SIZE_MAX)
                {
                    fatal("Tokenizer failed after FileRank selection");
                }
                fprintf(stderr, "\nFileRank selection complete:\n");
                fprintf(stderr, "  - Selected %d most relevant files out of %d total\n",
                        files_included, num_processed_files);
                fprintf(stderr, "  - Token usage: %zu / %zu (%zu%% of budget)\n", total_tokens,
                        g_token_budget, (total_tokens * 100) / g_token_budget);
            }
        }
        else
        {
            fprintf(stderr, "WARNING: context uses %zu tokens > budget %zu\n", total_tokens,
                    g_token_budget);
            if (user_instructions)
            {
                fprintf(stderr, "\nHint: Use -r flag to enable FileRank, which will select the "
                                "most relevant files\n");
                fprintf(stderr,
                        "      that fit within your token budget based on your search query.\n");
            }
            else
            {
                fprintf(stderr, "\nHint: Use -c \"query terms\" with -r flag to enable FileRank "
                                "file selection.\n");
            }
        }
    }

    if (g_token_diagnostics_requested)
    {
        FILE* diag_out = stderr;
        if (g_token_diagnostics_file)
        {
            diag_out = fopen(g_token_diagnostics_file, "w");
            if (!diag_out)
            {
                perror("Failed to open diagnostics file");
                diag_out = stderr;
            }
        }

        generate_token_diagnostics(final_content, g_token_model, diag_out, &g_arena);

        if (diag_out != stderr)
        {
            fclose(diag_out);
        }
    }

    /* --- Save prompt to disk --- */
    char* saved_uuid = save_prompt(final_content, argc, argv, processed_files, num_processed_files, &g_arena);
    if (saved_uuid)
    {
        fprintf(stderr, "Retrieve this prompt via llm_ctx get %s\n", saved_uuid);
    }

    /* --- Output Handling --- */
    if (g_effective_copy_to_clipboard)
    {
        /* final_content already read above */
        if (final_content)
        {
            size_t final_len = strlen(final_content);
            /* Check if content exceeds clipboard limit */
            if (final_len > CLIPBOARD_SOFT_MAX)
            {
                fprintf(stderr,
                        "Warning: output (%zu bytes) exceeds clipboard limit (%d MB); "
                        "writing to stdout instead.\n",
                        final_len, CLIPBOARD_SOFT_MAX / (1024 * 1024));
                printf("%s", final_content);
            }
            else if (!copy_to_clipboard(final_content))
            {
                /* Clipboard copy failed, fall back to stdout */
                fprintf(stderr, "Clipboard copy failed; falling back to stdout.\n");
                printf("%s", final_content);
            }
            else
            {
                /* Print confirmation message to stderr */
                if (tree_only || global_tree_only)
                {
                    fprintf(stderr, "File tree printed using depth %d.\n", tree_max_depth);
                }
                fprintf(stderr, "Content copied to clipboard.\n");
            }
        }
        /* Do NOT print to stdout when copying succeeded */
    }
    else if (g_output_file)
    {
        /* Output to specified file */
        /* final_content already read above */
        if (final_content)
        {
            FILE* output_file = fopen(g_output_file, "w");
            if (output_file)
            {
                if (fwrite(final_content, 1, strlen(final_content), output_file) !=
                    strlen(final_content))
                {
                    perror("Failed to write to output file");
                    fclose(output_file);
                    return 1;
                }
                fclose(output_file);
                fprintf(stderr, "Content written to %s\n", g_output_file);
            }
            else
            {
                perror("Failed to open output file");
                return 1;
            }
        }
    }
    else
    {
        /* Default: Display the output directly to stdout */
        /* final_content already read above */
        if (final_content)
        {
            printf("%s", final_content); /* Print directly to stdout */
        }
    }

    /* Cleanup is handled by atexit handler */
    return 0;
}
