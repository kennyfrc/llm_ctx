#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include <ctype.h>
#include <errno.h>
#include <dlfcn.h>  /* For dlopen, dlsym, etc. */

#ifdef __APPLE__
#include <mach-o/dyld.h> /* _NSGetExecutablePath */
#endif

#include "codemap.h"
#include "arena.h"
#include "packs.h"  /* For language pack support */

/* Tree-sitter typedefs and function pointers */
typedef struct TSParser TSParser;
typedef struct TSTree TSTree;
typedef struct TSLanguage TSLanguage;

// TSNode must be defined as it's passed by value
typedef struct {
    const void *tree;
    uint32_t id;
    uint32_t version;
} TSNode;

typedef TSParser* (*ts_parser_new_fn)(void);
typedef void (*ts_parser_delete_fn)(TSParser*);
typedef bool (*ts_parser_set_language_fn)(TSParser*, const TSLanguage*);
typedef TSTree* (*ts_parser_parse_string_fn)(TSParser*, const TSTree*, const char*, uint32_t);
typedef void (*ts_tree_delete_fn)(TSTree*);
typedef TSNode (*ts_tree_root_node_fn)(const TSTree*);
typedef uint32_t (*ts_node_child_count_fn)(TSNode);
typedef TSNode (*ts_node_child_fn)(TSNode, uint32_t);
typedef TSNode (*ts_node_named_child_fn)(TSNode, uint32_t);
typedef uint32_t (*ts_node_named_child_count_fn)(TSNode);
typedef bool (*ts_node_is_null_fn)(TSNode);
typedef const char* (*ts_node_type_fn)(TSNode);
typedef uint32_t (*ts_node_start_byte_fn)(TSNode);
typedef uint32_t (*ts_node_end_byte_fn)(TSNode);
typedef void (*ts_node_string_fn)(TSNode, char*, uint32_t);

/* Global function pointers */
static ts_parser_new_fn ts_parser_new;
static ts_parser_delete_fn ts_parser_delete;
static ts_parser_set_language_fn ts_parser_set_language;
static ts_parser_parse_string_fn ts_parser_parse_string;
static ts_tree_delete_fn ts_tree_delete;
static ts_tree_root_node_fn ts_tree_root_node;
static ts_node_child_count_fn ts_node_child_count;
static ts_node_child_fn ts_node_child;
static ts_node_named_child_fn ts_node_named_child;
static ts_node_named_child_count_fn ts_node_named_child_count;
static ts_node_is_null_fn ts_node_is_null;
static ts_node_type_fn ts_node_type;
static ts_node_start_byte_fn ts_node_start_byte;
static ts_node_end_byte_fn ts_node_end_byte;
static ts_node_string_fn ts_node_string;

/* Tree-sitter language function */
typedef const TSLanguage* (*language_fn)(void);

/* Global variables */
static void* tree_sitter_lib = NULL;
static void* language_lib = NULL;
static const TSLanguage* js_ts_language = NULL;

/**
 * Initialize an empty codemap
 */
Codemap codemap_init(Arena *arena) {
    (void)arena; // Unused parameter, but kept for API consistency
    
    Codemap cm = {0}; // Zero-initialized
    
    // Initialize the files array with zero elements
    cm.files = NULL;
    cm.file_count = 0;
    
    return cm;
}

/**
 * Add a new file to the codemap
 * Returns a pointer to the newly created file entry
 */
CodemapFile *codemap_add_file(Codemap *cm, const char *path, Arena *arena) {
    // Allocate new array with one more element
    CodemapFile *new_files = arena_push_array(arena, CodemapFile, cm->file_count + 1);
    if (!new_files) return NULL;
    
    // Copy existing files if any
    if (cm->file_count > 0 && cm->files) {
        memcpy(new_files, cm->files, cm->file_count * sizeof(CodemapFile));
    }
    
    // Initialize the new file entry
    CodemapFile *new_file = &new_files[cm->file_count];
    memset(new_file, 0, sizeof(CodemapFile)); // Zero out the new entry
    strncpy(new_file->path, path, MAX_PATH - 1);
    new_file->path[MAX_PATH - 1] = '\0'; // Ensure null termination
    new_file->entries = NULL;
    new_file->entry_count = 0;
    
    // Update the codemap
    cm->files = new_files;
    cm->file_count++;
    
    return new_file;
}

/**
 * Add a new entry to a codemap file
 * Returns a pointer to the newly created entry
 */
CodemapEntry *codemap_add_entry(CodemapFile *file, const char *name, const char *signature, 
                              const char *return_type, const char *container, CMKind kind, 
                              Arena *arena) {
    // Allocate new array with one more element
    CodemapEntry *new_entries = arena_push_array(arena, CodemapEntry, file->entry_count + 1);
    if (!new_entries) return NULL;
    
    // Copy existing entries if any
    if (file->entry_count > 0 && file->entries) {
        memcpy(new_entries, file->entries, file->entry_count * sizeof(CodemapEntry));
    }
    
    // Initialize the new entry
    CodemapEntry *new_entry = &new_entries[file->entry_count];
    memset(new_entry, 0, sizeof(CodemapEntry)); // Zero out the new entry
    
    // Set the fields
    strncpy(new_entry->name, name ? name : "<anonymous>", sizeof(new_entry->name) - 1);
    strncpy(new_entry->signature, signature ? signature : "()", sizeof(new_entry->signature) - 1);
    strncpy(new_entry->return_type, return_type ? return_type : "void", sizeof(new_entry->return_type) - 1);
    strncpy(new_entry->container, container ? container : "", sizeof(new_entry->container) - 1);
    new_entry->kind = kind;
    
    // Update the file
    file->entries = new_entries;
    file->entry_count++;
    
    return new_entry;
}

/**
 * Check if a file has entries of a specific kind
 */
static bool file_has_kind(CodemapFile *file, CMKind kind) {
    for (size_t i = 0; i < file->entry_count; i++) {
        if (file->entries[i].kind == kind) {
            return true;
        }
    }
    return false;
}

/**
 * Get all unique class names in a file
 */
static void get_class_names(CodemapFile *file, char **class_names, size_t *num_classes, size_t max_classes) {
    *num_classes = 0;
    
    for (size_t i = 0; i < file->entry_count; i++) {
        CodemapEntry *entry = &file->entries[i];
        
        if (entry->kind == CM_CLASS) {
            // Check if we already have this class
            bool found = false;
            for (size_t j = 0; j < *num_classes; j++) {
                if (strcmp(class_names[j], entry->name) == 0) {
                    found = true;
                    break;
                }
            }
            
            // If not found, add it
            if (!found && *num_classes < max_classes) {
                class_names[*num_classes] = entry->name;
                (*num_classes)++;
            }
        }
    }
}

/**
 * Generate the codemap output and append it to the given output_buffer
 * Returns the updated buffer position
 */
char *codemap_generate(Codemap *cm, char *output_buffer, size_t *buffer_pos, size_t buffer_size) {
    if (!cm || !output_buffer || !buffer_pos) return output_buffer;
    
    size_t pos = *buffer_pos;
    size_t remaining = buffer_size - pos;
    
    // Start the code map
    int written = snprintf(output_buffer + pos, remaining, "<code_map>\n");
    if (written < 0 || (size_t)written >= remaining) return output_buffer; // Buffer full
    pos += (size_t)written;
    remaining -= (size_t)written;
    
    // For each file
    for (size_t i = 0; i < cm->file_count; i++) {
        CodemapFile *file = &cm->files[i];
        
        // Skip empty files
        if (file->entry_count == 0) continue;
        
        // Output file header
        written = snprintf(output_buffer + pos, remaining, "[%s]\n", file->path);
        if (written < 0 || (size_t)written >= remaining) break; // Buffer full
        pos += (size_t)written;
        remaining -= (size_t)written;
        
        // 1. Output classes first (with methods)
        if (file_has_kind(file, CM_CLASS) || file_has_kind(file, CM_METHOD)) {
            // Output the Classes header
            written = snprintf(output_buffer + pos, remaining, "Classes:\n");
            if (written < 0 || (size_t)written >= remaining) break;
            pos += (size_t)written;
            remaining -= (size_t)written;
            
            // Get all class names
            char *class_names[64]; // Maximum classes per file
            size_t num_classes = 0;
            get_class_names(file, class_names, &num_classes, 64);
            
            // For each class
            for (size_t c = 0; c < num_classes; c++) {
                // Output the class name
                written = snprintf(output_buffer + pos, remaining, "  %s:\n", class_names[c]);
                if (written < 0 || (size_t)written >= remaining) break;
                pos += (size_t)written;
                remaining -= (size_t)written;
                
                // Output the methods header
                written = snprintf(output_buffer + pos, remaining, "    methods:\n");
                if (written < 0 || (size_t)written >= remaining) break;
                pos += (size_t)written;
                remaining -= (size_t)written;
                
                // Find constructor first, if any
                for (size_t j = 0; j < file->entry_count; j++) {
                    CodemapEntry *entry = &file->entries[j];
                    
                    if (entry->kind == CM_METHOD && 
                        strcmp(entry->container, class_names[c]) == 0 &&
                        strcmp(entry->name, "constructor") == 0) {
                        
                        // Output the constructor
                        written = snprintf(output_buffer + pos, remaining, "      - %s%s\n", 
                                          entry->name, entry->signature);
                        if (written < 0 || (size_t)written >= remaining) break;
                        pos += (size_t)written;
                        remaining -= (size_t)written;
                        
                        break;
                    }
                }
                
                // Then output other methods
                for (size_t j = 0; j < file->entry_count; j++) {
                    CodemapEntry *entry = &file->entries[j];
                    
                    if (entry->kind == CM_METHOD && 
                        strcmp(entry->container, class_names[c]) == 0 &&
                        strcmp(entry->name, "constructor") != 0) {
                        
                        // Output the method
                        written = snprintf(output_buffer + pos, remaining, "      - %s%s\n", 
                                          entry->name, entry->signature);
                        if (written < 0 || (size_t)written >= remaining) break;
                        pos += (size_t)written;
                        remaining -= (size_t)written;
                    }
                }
            }
        }
        
        // 2. Output functions if any
        if (file_has_kind(file, CM_FUNCTION)) {
            // Add a newline before Functions if we had Classes
            if (file_has_kind(file, CM_CLASS) || file_has_kind(file, CM_METHOD)) {
                written = snprintf(output_buffer + pos, remaining, "\n");
                if (written < 0 || (size_t)written >= remaining) break;
                pos += (size_t)written;
                remaining -= (size_t)written;
            }
            
            // Output the Functions header
            written = snprintf(output_buffer + pos, remaining, "Functions:\n");
            if (written < 0 || (size_t)written >= remaining) break;
            pos += (size_t)written;
            remaining -= (size_t)written;
            
            // Output each function
            for (size_t j = 0; j < file->entry_count; j++) {
                CodemapEntry *entry = &file->entries[j];
                
                if (entry->kind == CM_FUNCTION) {
                    // Output the function with return type if available
                    if (entry->return_type[0] && strcmp(entry->return_type, "void") != 0) {
                        written = snprintf(output_buffer + pos, remaining, "  %-25s %s -> %s\n", 
                                          entry->name, entry->signature, entry->return_type);
                    } else {
                        written = snprintf(output_buffer + pos, remaining, "  %-25s %s\n", 
                                          entry->name, entry->signature);
                    }
                    
                    if (written < 0 || (size_t)written >= remaining) break;
                    pos += (size_t)written;
                    remaining -= (size_t)written;
                }
            }
        }
        
        // 3. Output types if any
        if (file_has_kind(file, CM_TYPE)) {
            // Add a newline before Types if we had Classes or Functions
            if (file_has_kind(file, CM_CLASS) || file_has_kind(file, CM_METHOD) || 
                file_has_kind(file, CM_FUNCTION)) {
                written = snprintf(output_buffer + pos, remaining, "\n");
                if (written < 0 || (size_t)written >= remaining) break;
                pos += (size_t)written;
                remaining -= (size_t)written;
            }
            
            // Output the Types header
            written = snprintf(output_buffer + pos, remaining, "Types:\n");
            if (written < 0 || (size_t)written >= remaining) break;
            pos += (size_t)written;
            remaining -= (size_t)written;
            
            // Output each type
            for (size_t j = 0; j < file->entry_count; j++) {
                CodemapEntry *entry = &file->entries[j];
                
                if (entry->kind == CM_TYPE) {
                    // Output the type
                    written = snprintf(output_buffer + pos, remaining, "  %s\n", entry->name);
                    if (written < 0 || (size_t)written >= remaining) break;
                    pos += (size_t)written;
                    remaining -= (size_t)written;
                }
            }
        }
        
        // Add a newline between files
        if (i < cm->file_count - 1) {
            written = snprintf(output_buffer + pos, remaining, "\n");
            if (written < 0 || (size_t)written >= remaining) break;
            pos += (size_t)written;
            remaining -= (size_t)written;
        }
    }
    
    // End the code map
    written = snprintf(output_buffer + pos, remaining, "</code_map>\n");
    if (written < 0 || (size_t)written >= remaining) return output_buffer; // Buffer full
    pos += (size_t)written;
    
    // Update the buffer position
    *buffer_pos = pos;
    
    return output_buffer;
}

/**
 * Get the file extension from a path
 */
static const char* get_file_extension(const char *path) {
    if (!path) return NULL;
    
    // Get the extension
    const char *ext = strrchr(path, '.');
    if (!ext) return NULL;
    
    // Convert extension to lowercase for case-insensitive comparison
    static char ext_lower[10] = {0}; // Enough space for ".tsx" and similar
    size_t ext_len = strlen(ext);
    if (ext_len >= sizeof(ext_lower)) {
        ext_len = sizeof(ext_lower) - 1;
    }
    
    // Convert to lowercase
    for (size_t i = 0; i < ext_len; i++) {
        ext_lower[i] = tolower((unsigned char)ext[i]);
    }
    ext_lower[ext_len] = '\0';
    
    return ext_lower;
}

/**
 * Check if a file is a JavaScript or TypeScript file (legacy function)
 */
static bool is_js_ts_file(const char *path) {
    if (!path) return false;
    
    // Get the extension
    const char *ext = get_file_extension(path);
    if (!ext) return false;
    
    // Add debug output
    fprintf(stderr, "Checking file extension: %s\n", ext);
    
    // Check if it's a JavaScript or TypeScript file
    bool result = (strcmp(ext, ".js") == 0 || 
                  strcmp(ext, ".jsx") == 0 || 
                  strcmp(ext, ".ts") == 0 || 
                  strcmp(ext, ".tsx") == 0);
    
    fprintf(stderr, "File %s %s JavaScript/TypeScript file\n", path, result ? "is a" : "is NOT a");
    return result;
}

/**
 * Read file contents
 * Returns a pointer to the file contents, or NULL on error
 * Also sets *size to the size of the file
 * 
 * NOTE: This is a utility function that will be used when tree-sitter integration is added
 */
__attribute__((unused)) static char *read_file_contents(const char *path, size_t *size, Arena *arena) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Error: Failed to open file %s: %s\n", path, strerror(errno));
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Check file size limit (5 MB as per spec)
    if (file_size > 5 * 1024 * 1024) {
        fprintf(stderr, "Error: File %s exceeds size limit (5 MB)\n", path);
        fclose(file);
        return NULL;
    }
    
    // Allocate memory for the file contents
    char *buffer = arena_push_array(arena, char, file_size + 1);
    if (!buffer) {
        fprintf(stderr, "Error: Failed to allocate memory for file %s\n", path);
        fclose(file);
        return NULL;
    }
    
    // Read the file contents
    size_t bytes_read = fread(buffer, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "Error: Failed to read file %s: %s\n", path, strerror(errno));
        return NULL;
    }
    
    // Null-terminate the buffer
    buffer[file_size] = '\0';
    
    // Set the size
    *size = file_size;
    
    return buffer;
}

/**
 * Find the path to the Tree-sitter pack
 * Returns the path if found, or NULL otherwise
 */
static char* find_tree_sitter_pack_path(Arena *arena) {
    char *pack_path = NULL;
    FILE *f;
    
    // Try current directory first
    f = fopen("./packs/javascript/parser.so", "r");
    if (f) {
        fclose(f);
        pack_path = arena_push_array(arena, char, 128);
        if (pack_path) {
            strncpy(pack_path, "./packs/javascript/parser.so", 127);
            pack_path[127] = '\0';
            return pack_path;
        }
    }
    
    // Get executable path
    char exec_path[1024] = {0};
    #ifdef __APPLE__
    uint32_t size = sizeof(exec_path);
    if (_NSGetExecutablePath(exec_path, &size) == 0) {
        // Extract directory from executable path
        char *last_slash = strrchr(exec_path, '/');
        if (last_slash) {
            *last_slash = '\0'; // Truncate to directory
            
            // Try packs relative to executable directory
            char path_buf[1024] = {0};
            snprintf(path_buf, sizeof(path_buf), "%s/packs/javascript/parser.so", exec_path);
            
            f = fopen(path_buf, "r");
            if (f) {
                fclose(f);
                pack_path = arena_push_array(arena, char, 1024);
                if (pack_path) {
                    strncpy(pack_path, path_buf, 1023);
                    pack_path[1023] = '\0';
                    return pack_path;
                }
            }
        }
    }
    #endif
    
    // As a fallback, also try with an absolute path
    f = fopen("/Users/kennyfrc/Documents/code/fun/llm_ctx/packs/javascript/parser.so", "r");
    if (f) {
        fclose(f);
        pack_path = arena_push_array(arena, char, 128);
        if (pack_path) {
            strncpy(pack_path, "/Users/kennyfrc/Documents/code/fun/llm_ctx/packs/javascript/parser.so", 127);
            pack_path[127] = '\0';
            return pack_path;
        }
    }
    
    return NULL;
}

/**
 * Checks if the Tree-sitter packs are installed
 * Returns true if installed, false otherwise
 */
__attribute__((unused)) static bool check_tree_sitter_pack(void) {
    // Try current directory first
    FILE *f = fopen("./packs/javascript/parser.so", "r");
    if (f) {
        fclose(f);
        return true;
    }
    
    // Get executable path
    char exec_path[1024] = {0};
    #ifdef __APPLE__
    uint32_t size = sizeof(exec_path);
    if (_NSGetExecutablePath(exec_path, &size) == 0) {
        // Extract directory from executable path
        char *last_slash = strrchr(exec_path, '/');
        if (last_slash) {
            *last_slash = '\0'; // Truncate to directory
            
            // Try packs relative to executable directory
            char pack_path[1024] = {0};
            snprintf(pack_path, sizeof(pack_path), "%s/packs/javascript/parser.so", exec_path);
            
            f = fopen(pack_path, "r");
            if (f) {
                fclose(f);
                return true;
            }
        }
    }
    #endif
    
    // As a fallback, also try with an absolute path
    f = fopen("/Users/kennyfrc/Documents/code/fun/llm_ctx/packs/javascript/parser.so", "r");
    if (f) {
        fclose(f);
        return true;
    }
    
    return false;
}

/**
 * Load the Tree-sitter library and JavaScript/TypeScript grammar
 * Returns true if successful, false otherwise
 */
__attribute__((unused)) static bool load_tree_sitter(Arena *arena) {
    // Find the parser path
    char *parser_path = find_tree_sitter_pack_path(arena);
    if (!parser_path) {
        fprintf(stderr, "Error: Could not find Tree-sitter JavaScript/TypeScript parser\n");
        return false;
    }
    
    // Load the language library
    language_lib = dlopen(parser_path, RTLD_LAZY);
    if (!language_lib) {
        fprintf(stderr, "Error: Could not load Tree-sitter language library: %s\n", dlerror());
        return false;
    }
    
    // Get the tree_sitter_javascript function
    language_fn tree_sitter_javascript = (language_fn)dlsym(language_lib, "tree_sitter_javascript");
    if (!tree_sitter_javascript) {
        fprintf(stderr, "Error: Could not find tree_sitter_javascript function: %s\n", dlerror());
        dlclose(language_lib);
        language_lib = NULL;
        return false;
    }
    
    // Get the language
    js_ts_language = tree_sitter_javascript();
    if (!js_ts_language) {
        fprintf(stderr, "Error: Could not get Tree-sitter JavaScript language\n");
        dlclose(language_lib);
        language_lib = NULL;
        return false;
    }
    
    // Load the tree-sitter library
    tree_sitter_lib = dlopen("libtree-sitter.2.dylib", RTLD_LAZY);
    if (!tree_sitter_lib) {
        // Try another name
        tree_sitter_lib = dlopen("libtree-sitter.dylib", RTLD_LAZY);
    }
    if (!tree_sitter_lib) {
        // Try the system library path
        tree_sitter_lib = dlopen("/usr/local/lib/libtree-sitter.2.dylib", RTLD_LAZY);
    }
    if (!tree_sitter_lib) {
        // Try the homebrew path
        tree_sitter_lib = dlopen("/opt/homebrew/lib/libtree-sitter.2.dylib", RTLD_LAZY);
    }
    if (!tree_sitter_lib) {
        fprintf(stderr, "Error: Could not load Tree-sitter library: %s\n", dlerror());
        fprintf(stderr, "Please install the Tree-sitter library with: brew install tree-sitter\n");
        dlclose(language_lib);
        language_lib = NULL;
        js_ts_language = NULL;
        return false;
    }
    
    // Load all the functions we need
    ts_parser_new = (ts_parser_new_fn)dlsym(tree_sitter_lib, "ts_parser_new");
    ts_parser_delete = (ts_parser_delete_fn)dlsym(tree_sitter_lib, "ts_parser_delete");
    ts_parser_set_language = (ts_parser_set_language_fn)dlsym(tree_sitter_lib, "ts_parser_set_language");
    ts_parser_parse_string = (ts_parser_parse_string_fn)dlsym(tree_sitter_lib, "ts_parser_parse_string");
    ts_tree_delete = (ts_tree_delete_fn)dlsym(tree_sitter_lib, "ts_tree_delete");
    ts_tree_root_node = (ts_tree_root_node_fn)dlsym(tree_sitter_lib, "ts_tree_root_node");
    ts_node_child_count = (ts_node_child_count_fn)dlsym(tree_sitter_lib, "ts_node_child_count");
    ts_node_child = (ts_node_child_fn)dlsym(tree_sitter_lib, "ts_node_child");
    ts_node_named_child = (ts_node_named_child_fn)dlsym(tree_sitter_lib, "ts_node_named_child");
    ts_node_named_child_count = (ts_node_named_child_count_fn)dlsym(tree_sitter_lib, "ts_node_named_child_count");
    ts_node_is_null = (ts_node_is_null_fn)dlsym(tree_sitter_lib, "ts_node_is_null");
    ts_node_type = (ts_node_type_fn)dlsym(tree_sitter_lib, "ts_node_type");
    ts_node_start_byte = (ts_node_start_byte_fn)dlsym(tree_sitter_lib, "ts_node_start_byte");
    ts_node_end_byte = (ts_node_end_byte_fn)dlsym(tree_sitter_lib, "ts_node_end_byte");
    ts_node_string = (ts_node_string_fn)dlsym(tree_sitter_lib, "ts_node_string");
    
    // Check if we got all the functions
    if (!ts_parser_new || !ts_parser_delete || !ts_parser_set_language || 
        !ts_parser_parse_string || !ts_tree_delete || !ts_tree_root_node || 
        !ts_node_child_count || !ts_node_child || !ts_node_named_child || 
        !ts_node_named_child_count || !ts_node_is_null || !ts_node_type || 
        !ts_node_start_byte || !ts_node_end_byte || !ts_node_string) {
        fprintf(stderr, "Error: Could not load all required Tree-sitter functions\n");
        dlclose(tree_sitter_lib);
        dlclose(language_lib);
        tree_sitter_lib = NULL;
        language_lib = NULL;
        js_ts_language = NULL;
        return false;
    }
    
    return true;
}

/**
 * Unload the Tree-sitter library and JavaScript/TypeScript grammar
 */
static void unload_tree_sitter(void) {
    if (tree_sitter_lib) {
        dlclose(tree_sitter_lib);
        tree_sitter_lib = NULL;
    }
    
    if (language_lib) {
        dlclose(language_lib);
        language_lib = NULL;
    }
    
    js_ts_language = NULL;
}

/**
 * Extract a string from a source file
 */
static char* extract_source_text(const char *source, uint32_t start_byte, uint32_t end_byte, Arena *arena) __attribute__((unused));
static char* extract_source_text(const char *source, uint32_t start_byte, uint32_t end_byte, Arena *arena) {
    size_t len = end_byte - start_byte;
    
    // Allocate memory for the string
    char *text = arena_push_array(arena, char, len + 1);
    if (!text) return NULL;
    
    // Copy the text
    memcpy(text, source + start_byte, len);
    text[len] = '\0';
    
    return text;
}

/**
 * Extract the return type from a function node
 */
static char* extract_return_type(TSNode node, const char *source, Arena *arena) {
    // Avoid unused parameter warnings
    (void)node;
    (void)source;
    
    // Default return type
    char *return_type = arena_push_array(arena, char, 5);
    if (return_type) {
        strcpy(return_type, "void");
        return return_type;
    }
    
    return NULL;
}

/**
 * Extract parameter list from a function node
 */
static char* extract_parameters(TSNode node, const char *source, Arena *arena) {
    // Since we're not yet implementing the actual Tree-sitter parsing,
    // return default parameters and mark params as unused
    (void)node;
    (void)source;
    
    // Default parameters
    char *params = arena_push_array(arena, char, 3);
    if (params) {
        strcpy(params, "()");
        return params;
    }
    return NULL;
    
    /*
    // The following code would be used with real Tree-sitter parsing
    // Find the parameters node
    bool found_params = false;
    TSNode params_node;
    
    // Look for formal_parameters or parameter_list
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);
        
        if (strcmp(type, "formal_parameters") == 0 || 
            strcmp(type, "parameter_list") == 0) {
            params_node = child;
            found_params = true;
            break;
        }
    }
    
    if (!found_params) {
        // Default parameters
        char *params = arena_push_array(arena, char, 3);
        if (params) {
            strcpy(params, "()");
            return params;
        }
        return NULL;
    }
    
    // Extract the parameter text
    uint32_t start_byte = ts_node_start_byte(params_node);
    uint32_t end_byte = ts_node_end_byte(params_node);
    
    return extract_source_text(source, start_byte, end_byte, arena);
    */
}

/**
 * Extract a function name from a function declaration node
 */
static char* extract_function_name(TSNode node, const char *source, Arena *arena) {
    // Since we're not yet implementing the actual Tree-sitter parsing,
    // return a default name and mark params as unused
    (void)node;
    (void)source;
    
    // Default function name
    char *name = arena_push_array(arena, char, 15);
    if (name) {
        strcpy(name, "function");
        return name;
    }
    
    return NULL;
    
    /*
    // The following code would be used with real Tree-sitter parsing
    // Look for the name identifier
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);
        
        if (strcmp(type, "identifier") == 0) {
            uint32_t start_byte = ts_node_start_byte(child);
            uint32_t end_byte = ts_node_end_byte(child);
            
            return extract_source_text(source, start_byte, end_byte, arena);
        }
    }
    
    // Anonymous function
    char *anon = arena_push_array(arena, char, 15);
    if (anon) {
        strcpy(anon, "<anonymous>");
        return anon;
    }
    
    return NULL;
    */
}

/**
 * Process a function declaration node
 */
static void process_function_declaration(TSNode node, const char *source, CodemapFile *file, Arena *arena) {
    // Since we're not yet implementing the actual Tree-sitter parsing logic,
    // mark node as unused
    (void)node;
    
    // Extract the function name
    char *name = extract_function_name(node, source, arena);
    if (!name) {
        fprintf(stderr, "Warning: Could not extract function name\n");
        return;
    }
    
    // Extract the parameters
    char *parameters = extract_parameters(node, source, arena);
    if (!parameters) {
        fprintf(stderr, "Warning: Could not extract function parameters\n");
        return;
    }
    
    // Extract the return type
    char *return_type = extract_return_type(node, source, arena);
    if (!return_type) {
        fprintf(stderr, "Warning: Could not extract function return type\n");
        return;
    }
    
    // Add the entry to the codemap
    codemap_add_entry(file, name, parameters, return_type, "", CM_FUNCTION, arena);
}

/**
 * Process a variable declaration node (might contain function expression or arrow function)
 */
static void process_variable_declaration(TSNode node, const char *source, CodemapFile *file, Arena *arena) {
    // Since we're not yet implementing the actual Tree-sitter parsing logic,
    // mark node as unused and add dummy content
    (void)node;
    
    // Create a dummy variable function entry
    char *var_name = arena_push_array(arena, char, 12);
    if (!var_name) {
        fprintf(stderr, "Warning: Could not allocate memory for variable name\n");
        return;
    }
    strcpy(var_name, "varFunction");
    
    // Extract the parameters
    char *parameters = extract_parameters(node, source, arena);
    if (!parameters) {
        fprintf(stderr, "Warning: Could not extract function parameters\n");
        return;
    }
    
    // Extract the return type
    char *return_type = extract_return_type(node, source, arena);
    if (!return_type) {
        fprintf(stderr, "Warning: Could not extract function return type\n");
        return;
    }
    
    // Add the entry to the codemap
    codemap_add_entry(file, var_name, parameters, return_type, "", CM_FUNCTION, arena);
    
    /*
    // The following code would be used with real Tree-sitter parsing
    // Check if the variable declaration contains a function expression or arrow function
    bool found_function = false;
    char *var_name = NULL;
    TSNode function_node;
    
    // Look for the name identifier
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);
        
        if (strcmp(type, "variable_declarator") == 0) {
            // Get the variable name
            for (uint32_t j = 0; j < ts_node_named_child_count(child); j++) {
                TSNode grandchild = ts_node_named_child(child, j);
                const char *grandchild_type = ts_node_type(grandchild);
                
                if (strcmp(grandchild_type, "identifier") == 0) {
                    uint32_t start_byte = ts_node_start_byte(grandchild);
                    uint32_t end_byte = ts_node_end_byte(grandchild);
                    var_name = extract_source_text(source, start_byte, end_byte, arena);
                }
                
                // Check if the variable is assigned a function
                if (strcmp(grandchild_type, "function") == 0 || 
                    strcmp(grandchild_type, "arrow_function") == 0) {
                    function_node = grandchild;
                    found_function = true;
                }
            }
        }
    }
    
    if (found_function && var_name) {
        // Extract the parameters
        char *parameters = extract_parameters(function_node, source, arena);
        if (!parameters) {
            fprintf(stderr, "Warning: Could not extract function parameters\n");
            return;
        }
        
        // Extract the return type
        char *return_type = extract_return_type(function_node, source, arena);
        if (!return_type) {
            fprintf(stderr, "Warning: Could not extract function return type\n");
            return;
        }
        
        // Add the entry to the codemap
        codemap_add_entry(file, var_name, parameters, return_type, "", CM_FUNCTION, arena);
    }
    */
}

/**
 * Extract a class name from a class declaration node
 */
static char* extract_class_name(TSNode node, const char *source, Arena *arena) {
    // Since we're not yet implementing the actual Tree-sitter parsing logic,
    // mark node and source as unused
    (void)node;
    (void)source;
    
    // Return a dummy class name
    char *name = arena_push_array(arena, char, 15);
    if (name) {
        strcpy(name, "DummyClass");
        return name;
    }
    
    return NULL;
}

/**
 * Extract a method name from a method definition node
 */
static char* extract_method_name(TSNode node, const char *source, Arena *arena) {
    // Since we're not yet implementing the actual Tree-sitter parsing logic,
    // mark node and source as unused
    (void)node;
    (void)source;
    
    // Return a dummy method name
    char *name = arena_push_array(arena, char, 15);
    if (name) {
        strcpy(name, "dummyMethod");
        return name;
    }
    
    return NULL;
}

/**
 * Process a method definition node
 */
static void process_method_definition(TSNode node, const char *source, const char *class_name, CodemapFile *file, Arena *arena) __attribute__((unused));
static void process_method_definition(TSNode node, const char *source, const char *class_name, CodemapFile *file, Arena *arena) {
    // Since we're not yet implementing the actual Tree-sitter parsing logic,
    // mark node as unused
    (void)node;
    
    // Extract the method name
    char *name = extract_method_name(node, source, arena);
    if (!name) {
        fprintf(stderr, "Warning: Could not extract method name\n");
        return;
    }
    
    // Extract the parameters
    char *parameters = extract_parameters(node, source, arena);
    if (!parameters) {
        fprintf(stderr, "Warning: Could not extract method parameters\n");
        return;
    }
    
    // Extract the return type
    char *return_type = extract_return_type(node, source, arena);
    if (!return_type) {
        fprintf(stderr, "Warning: Could not extract method return type\n");
        return;
    }
    
    // Add the entry to the codemap
    codemap_add_entry(file, name, parameters, return_type, class_name, CM_METHOD, arena);
}

/**
 * Process a constructor declaration node
 */
static void process_constructor_declaration(TSNode node, const char *source, const char *class_name, CodemapFile *file, Arena *arena) __attribute__((unused));
static void process_constructor_declaration(TSNode node, const char *source, const char *class_name, CodemapFile *file, Arena *arena) {
    // Since we're not yet implementing the actual Tree-sitter parsing logic,
    // mark node as unused
    (void)node;
    
    // Extract the parameters
    char *parameters = extract_parameters(node, source, arena);
    if (!parameters) {
        fprintf(stderr, "Warning: Could not extract constructor parameters\n");
        return;
    }
    
    // Add the entry to the codemap
    codemap_add_entry(file, "constructor", parameters, "", class_name, CM_METHOD, arena);
}

/**
 * Process a class declaration node
 */
static void process_class_declaration(TSNode node, const char *source, CodemapFile *file, Arena *arena) {
    // Since we're not yet implementing the actual Tree-sitter parsing logic,
    // mark node as unused
    (void)node;
    
    // Extract the class name
    char *class_name = extract_class_name(node, source, arena);
    if (!class_name) {
        fprintf(stderr, "Warning: Could not extract class name\n");
        return;
    }
    
    // Add the class entry to the codemap
    codemap_add_entry(file, class_name, "", "", "", CM_CLASS, arena);
    
    // Add a dummy constructor
    codemap_add_entry(file, "constructor", "()", "", class_name, CM_METHOD, arena);
    
    // Add dummy methods
    codemap_add_entry(file, "dummyMethod1", "(param1, param2)", "void", class_name, CM_METHOD, arena);
    codemap_add_entry(file, "dummyMethod2", "()", "string", class_name, CM_METHOD, arena);
}

/**
 * Extract a type name from a type declaration node
 */
static char* extract_type_name(TSNode node, const char *source, Arena *arena) {
    // Since we're not yet implementing the actual Tree-sitter parsing logic,
    // mark node and source as unused
    (void)node;
    (void)source;
    
    // Return a dummy type name
    char *name = arena_push_array(arena, char, 15);
    if (name) {
        strcpy(name, "DummyType");
        return name;
    }
    
    return NULL;
}

/**
 * Process a type declaration node
 */
static void process_type_declaration(TSNode node, const char *source, CodemapFile *file, Arena *arena) {
    // Since we're not yet implementing the actual Tree-sitter parsing logic,
    // mark node as unused
    (void)node;
    
    // Extract the type name
    char *type_name = extract_type_name(node, source, arena);
    if (!type_name) {
        fprintf(stderr, "Warning: Could not extract type name\n");
        return;
    }
    
    // Add the entry to the codemap
    codemap_add_entry(file, type_name, "", "", "", CM_TYPE, arena);
}

/**
 * Process an interface declaration node
 */
static void process_interface_declaration(TSNode node, const char *source, CodemapFile *file, Arena *arena) {
    // Since we're not yet implementing the actual Tree-sitter parsing logic,
    // mark node and source as unused
    (void)node;
    (void)source;
    
    // Add a dummy interface entry
    codemap_add_entry(file, "DummyInterface", "", "", "", CM_TYPE, arena);
}

/**
 * Process an enum declaration node
 */
static void process_enum_declaration(TSNode node, const char *source, CodemapFile *file, Arena *arena) {
    // Since we're not yet implementing the actual Tree-sitter parsing logic,
    // mark node and source as unused
    (void)node;
    (void)source;
    
    // Add a dummy enum entry
    codemap_add_entry(file, "DummyEnum", "", "", "", CM_TYPE, arena);
}

/**
 * Walk the AST and process each node
 */
static void walk_ast(TSNode node, const char *source, CodemapFile *file, Arena *arena) {
    // Since we're not yet implementing the actual Tree-sitter parsing logic,
    // mark node parameters as unused
    (void)node;
    
    // For demonstration, add dummy entries of all kinds
    // Add a function
    process_function_declaration(node, source, file, arena);
    
    // Add a variable function
    process_variable_declaration(node, source, file, arena);
    
    // Add a class with methods
    process_class_declaration(node, source, file, arena);
    
    // Add types
    process_type_declaration(node, source, file, arena);
    process_interface_declaration(node, source, file, arena);
    process_enum_declaration(node, source, file, arena);
    
    /*
    // The following code would be used with real Tree-sitter parsing
    // Get the node type
    const char *type = ts_node_type(node);
    
    // Process the node based on its type
    if (strcmp(type, "function_declaration") == 0) {
        process_function_declaration(node, source, file, arena);
    } 
    else if (strcmp(type, "variable_declaration") == 0) {
        process_variable_declaration(node, source, file, arena);
    }
    else if (strcmp(type, "class_declaration") == 0) {
        process_class_declaration(node, source, file, arena);
    }
    else if (strcmp(type, "type_alias_declaration") == 0) {
        process_type_declaration(node, source, file, arena);
    }
    else if (strcmp(type, "interface_declaration") == 0) {
        process_interface_declaration(node, source, file, arena);
    }
    else if (strcmp(type, "enum_declaration") == 0) {
        process_enum_declaration(node, source, file, arena);
    }
    
    // Process child nodes recursively
    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        if (!ts_node_is_null(child)) {
            walk_ast(child, source, file, arena);
        }
    }
    */
}

/**
 * Parse a JavaScript/TypeScript file and extract code symbols
 */
__attribute__((unused)) static bool parse_js_ts_file(CodemapFile *file, const char *source, size_t source_len, Arena *arena) {
    // Create a parser
    TSParser *parser = ts_parser_new();
    if (!parser) {
        fprintf(stderr, "Error: Could not create Tree-sitter parser\n");
        return false;
    }
    
    // Set the language
    if (!ts_parser_set_language(parser, js_ts_language)) {
        fprintf(stderr, "Error: Could not set parser language\n");
        ts_parser_delete(parser);
        return false;
    }
    
    // Parse the source
    TSTree *tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)source_len);
    if (!tree) {
        fprintf(stderr, "Error: Could not parse source file: %s\n", file->path);
        ts_parser_delete(parser);
        return false;
    }
    
    // Get the root node
    TSNode root = ts_tree_root_node(tree);
    if (ts_node_is_null(root)) {
        fprintf(stderr, "Error: Could not get root node: %s\n", file->path);
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return false;
    }
    
    // Walk the AST
    walk_ast(root, source, file, arena);
    
    // Clean up
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    
    return true;
}

/**
 * Process a single source file using language packs
 * Returns true if the file was processed successfully
 */
static bool process_source_file(const char *path, CodemapFile *file, PackRegistry *registry, Arena *arena) {
    if (!path || !file || !registry || !arena) {
        return false;
    }
    
    // Get the file extension
    const char *ext = get_file_extension(path);
    if (!ext) {
        fprintf(stderr, "Warning: File %s has no extension, skipping\n", path);
        return false;
    }
    
    // Look up the language pack for this extension
    LanguagePack *pack = find_pack_for_extension(registry, ext);
    if (!pack || !pack->available || !pack->handle) {
        fprintf(stderr, "Warning: No language pack available for %s (extension %s)\n", path, ext);
        return false;
    }
    
    // Read the file contents
    size_t file_size = 0;
    char *source = NULL;
    
    // Open the file
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Failed to open file %s: %s\n", path, strerror(errno));
        return false;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Check file size limit (5 MB as per spec)
    if (file_len > 5 * 1024 * 1024) {
        fprintf(stderr, "Error: File %s exceeds size limit (5 MB)\n", path);
        fclose(f);
        return false;
    }
    
    // Allocate memory for the file contents
    source = arena_push_array(arena, char, file_len + 1);
    if (!source) {
        fprintf(stderr, "Error: Failed to allocate memory for file %s\n", path);
        fclose(f);
        return false;
    }
    
    // Read the file contents
    size_t bytes_read = fread(source, 1, file_len, f);
    fclose(f);
    
    if (bytes_read != (size_t)file_len) {
        fprintf(stderr, "Error: Failed to read file %s: %s\n", path, strerror(errno));
        return false;
    }
    
    // Null-terminate the buffer
    source[file_len] = '\0';
    file_size = file_len;
    
    // Parse the file using the language pack
    bool result = pack->parse_file(path, source, file_size, file, arena);
    
    // If parsing fails, report the error and return false
    if (!result || file->entry_count == 0) {
        fprintf(stderr, "Error: Failed to parse %s or no entries found\n", path);
        // Don't add fallback entries - let the error propagate
        return false;
    }
    
    return result;
}

/**
 * Process files and build the codemap using language packs
 */
bool process_js_ts_files(Codemap *cm, const char **processed_files, size_t processed_count, Arena *arena) {
    fprintf(stderr, "process_js_ts_files called with cm=%p, processed_files=%p, processed_count=%zu, arena=%p\n", 
            (void*)cm, (void*)processed_files, processed_count, (void*)arena);
    
    if (!cm) {
        fprintf(stderr, "Error: Codemap pointer is NULL\n");
        return false;
    }
    
    if (!processed_files) {
        fprintf(stderr, "Error: processed_files pointer is NULL\n");
        return false;
    }
    
    if (!arena) {
        fprintf(stderr, "Error: Arena pointer is NULL\n");
        return false;
    }
    
    /* Print each processed file */
    for (size_t i = 0; i < processed_count; i++) {
        if (processed_files[i]) {
            fprintf(stderr, "  File %zu: %s\n", i, processed_files[i]);
        } else {
            fprintf(stderr, "  File %zu: NULL\n", i);
        }
    }
    
    // Initialize and use the global registry
    bool registry_initialized = initialize_pack_registry(&g_pack_registry, arena);
    if (registry_initialized) {
        load_language_packs(&g_pack_registry);
        build_extension_map(&g_pack_registry, arena);
    }
    
    // Process each file
    for (size_t i = 0; i < processed_count; i++) {
        const char *path = processed_files[i];
        if (!path) continue;
        
        // Get the file extension
        const char *ext = get_file_extension(path);
        if (!ext) {
            fprintf(stderr, "Warning: File %s has no extension, skipping\n", path);
            continue;
        }
        
        // Add the file to the codemap
        CodemapFile *file = codemap_add_file(cm, path, arena);
        if (!file) {
            fprintf(stderr, "Warning: Failed to add file to codemap: %s\n", path);
            continue;
        }
        
        // Check if we have language packs loaded
        if (registry_initialized && g_pack_registry.pack_count > 0 && g_pack_registry.extension_map_size > 0) {
            // Try to process the file with the appropriate language pack
            if (process_source_file(path, file, &g_pack_registry, arena)) {
                continue;  // File processed, continue to next file
            }
        }
        
        // If we get here, either no packs are loaded or none matched this file
        // For JavaScript/TypeScript files, show a clear error message
        if (is_js_ts_file(path)) {
            fprintf(stderr, "Error: No suitable language pack available for %s\n", path);
            fprintf(stderr, "To process JavaScript/TypeScript files, you must install Tree-sitter:\n");
            fprintf(stderr, "  brew install tree-sitter\n");
            // Don't add any placeholder entries - leave the file empty
        }
        else {
            // Not a supported file type, remove it from the codemap
            // (this is a bit of a hack, as we're just setting the entry count to 0)
            file->entry_count = 0;
        }
    }
    
    // Unload the tree-sitter library (legacy code, kept for compatibility)
    unload_tree_sitter();
    
    // If after all processing we have no files with entries, return failure
    bool any_files_with_entries = false;
    for (size_t i = 0; i < cm->file_count; i++) {
        if (cm->files[i].entry_count > 0) {
            any_files_with_entries = true;
            break;
        }
    }
    
    if (!any_files_with_entries) {
        fprintf(stderr, "Warning: No code symbols found in any files, codemap will be empty\n");
        // Reset the codemap but we can't free the arena memory
        cm->files = NULL;
        cm->file_count = 0;
        return false;
    }
    
    return true;
}