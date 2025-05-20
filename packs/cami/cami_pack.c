#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include "../debug.h"

/**
 * Cami.js Language Pack for LLM_CTX
 *
 * This language pack parses JavaScript and TypeScript files that use the Cami.js framework.
 * It extracts stores, actions, async actions, queries, mutations, memos and components.
 */

/* Import Tree-sitter API definitions */
#include "tree-sitter.h"

/* Import required structures from LLM_CTX */
typedef enum { 
    CM_FUNCTION = 0, 
    CM_CLASS = 1, 
    CM_METHOD = 2, 
    CM_TYPE = 3,
    /* Cami-specific kinds */
    CM_STORE = 4,
    CM_ACTION = 5,
    CM_ASYNC_ACTION = 6,
    CM_QUERY = 7,
    CM_MUTATION = 8,
    CM_MEMO = 9,
    CM_COMPONENT = 10
} CMKind;

typedef struct {
    char  name[128];          /* Identifier */
    char  signature[256];     /* Params incl. "(...)" */
    char  return_type[64];    /* "void" default for unknown */
    char  container[128];     /* Store name for actions, class name for methods, empty otherwise */
    CMKind kind;
} CodemapEntry;

typedef struct {
    char           path[4096];
    CodemapEntry  *entries;    /* arena array */
    size_t         entry_count;
} CodemapFile;

typedef struct Arena {
    unsigned char *base;
    size_t pos;
    size_t size;
#ifdef ARENA_ENABLE_COMMIT
    size_t commit;
#endif
} Arena;

/* File extensions we support */
static const char *lang_extensions[] = {".js", ".jsx", ".ts", ".tsx", NULL};
static size_t lang_extension_count = 4;

/**
 * Utility function to copy a substring from source code
 */
static char* copy_substring(const char *source, uint32_t start, uint32_t end, Arena *arena) {
    if (!source || start >= end) return NULL;
    
    uint32_t length = end - start;
    if (length >= 256) length = 255; /* Limit string length */
    
    /* Allocate from arena */
    if (arena->pos + length + 1 > arena->size) {
        fprintf(stderr, "ERROR: Arena out of memory for substring\n");
        return NULL;
    }
    
    char *str = (char*)(arena->base + arena->pos);
    memcpy(str, source + start, length);
    str[length] = '\0'; /* Null terminate */
    
    arena->pos += length + 1;
    return str;
}

/**
 * Helper to find a child node by type name
 */
static TSNode find_child_by_type(TSNode node, const char *type) {
    uint32_t count = ts_node_child_count(node);
    
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        if (!ts_node_is_null(child)) {
            const char *node_type = ts_node_type(child);
            if (strcmp(node_type, type) == 0) {
                return child;
            }
        }
    }
    
    // Return NULL node
    TSNode null_node = {0};
    return null_node;
}

/**
 * Helper to find a named child by field name
 */
static TSNode find_field(TSNode node, const char *field_name) {
    return ts_node_child_by_field_name(node, field_name, strlen(field_name));
}

/**
 * Allocate a new codemap entry
 */
static CodemapEntry* allocate_entry(CodemapFile *file, Arena *arena) {
    /* Allocate new entries array if needed */
    size_t new_size = sizeof(CodemapEntry) * (file->entry_count + 1);
    CodemapEntry *new_entries = (CodemapEntry*)(arena->base + arena->pos);
    arena->pos += new_size;
    
    /* Copy existing entries */
    if (file->entry_count > 0 && file->entries) {
        memcpy(new_entries, file->entries, sizeof(CodemapEntry) * file->entry_count);
    }
    
    /* Initialize new entry */
    CodemapEntry *entry = &new_entries[file->entry_count];
    memset(entry, 0, sizeof(CodemapEntry));
    
    /* Update file with new entries */
    file->entries = new_entries;
    file->entry_count++;
    
    return entry;
}

/**
 * Extract string literal value from a node
 */
static bool extract_string_literal(TSNode node, const char *source, char *dest, size_t dest_size) {
    if (ts_node_is_null(node)) return false;
    
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    
    if (end - start >= dest_size) {
        // String too long
        return false;
    }
    
    // For string literals, skip the quotes
    const char *node_type = ts_node_type(node);
    if (strcmp(node_type, "string") == 0 || 
        strcmp(node_type, "string_fragment") == 0) {
        // Skip opening quote
        start++;
        // Skip closing quote
        end--;
    }
    
    strncpy(dest, source + start, end - start);
    dest[end - start] = '\0';
    
    return true;
}

/**
 * Process defineAction, defineQuery, etc. calls
 */
static void process_cami_definition(TSNode call_node, const char *source, CodemapFile *file, Arena *arena, const char *store_name) {
    // Get the function node (callee)
    TSNode function_node = find_field(call_node, "function");
    if (ts_node_is_null(function_node)) return;
    
    // Get the function name
    const char *node_type = ts_node_type(function_node);
    if (strcmp(node_type, "member_expression") == 0) {
        TSNode property = find_field(function_node, "property");
        if (ts_node_is_null(property)) return;
        
        const char *property_type = ts_node_type(property);
        if (strcmp(property_type, "property_identifier") != 0) return;
        
        char method_name[64] = {0};
        uint32_t start = ts_node_start_byte(property);
        uint32_t end = ts_node_end_byte(property);
        strncpy(method_name, source + start, end - start < 63 ? end - start : 63);
        
        // Get arguments
        TSNode arguments = find_field(call_node, "arguments");
        if (ts_node_is_null(arguments) || ts_node_named_child_count(arguments) < 1) return;
        
        // Get first argument (name of action/query/etc)
        TSNode first_arg = ts_node_named_child(arguments, 0);
        if (ts_node_is_null(first_arg)) return;
        
        char entity_name[128] = {0};
        if (!extract_string_literal(first_arg, source, entity_name, sizeof(entity_name) - 1)) {
            return;
        }
        
        // Create a new entry
        CodemapEntry *entry = allocate_entry(file, arena);
        strncpy(entry->name, entity_name, sizeof(entry->name) - 1);
        
        // Get second argument if available (handler function)
        if (ts_node_named_child_count(arguments) >= 2) {
            TSNode second_arg = ts_node_named_child(arguments, 1);
            
            // Check if it's a function and extract signature
            const char *second_arg_type = ts_node_type(second_arg);
            if (strcmp(second_arg_type, "arrow_function") == 0 || 
                strcmp(second_arg_type, "function") == 0) {
                
                TSNode params = find_field(second_arg, "parameters");
                if (!ts_node_is_null(params)) {
                    uint32_t start = ts_node_start_byte(params);
                    uint32_t end = ts_node_end_byte(params);
                    strncpy(entry->signature, source + start, end - start < 255 ? end - start : 255);
                } else {
                    strcpy(entry->signature, "()");
                }
            } else if (strcmp(second_arg_type, "object") == 0) {
                // For queries and mutations, the second arg is an object
                strcpy(entry->signature, "{...}");
            }
        } else {
            strcpy(entry->signature, "()");
        }
        
        // Set the container to the store name if available
        if (store_name) {
            strncpy(entry->container, store_name, sizeof(entry->container) - 1);
        }
        
        // Set the kind based on the method name
        if (strcmp(method_name, "defineAction") == 0) {
            entry->kind = CM_ACTION;
        } else if (strcmp(method_name, "defineAsyncAction") == 0) {
            entry->kind = CM_ASYNC_ACTION;
        } else if (strcmp(method_name, "defineQuery") == 0) {
            entry->kind = CM_QUERY;
        } else if (strcmp(method_name, "defineMutation") == 0) {
            entry->kind = CM_MUTATION;
        } else if (strcmp(method_name, "defineMemo") == 0) {
            entry->kind = CM_MEMO;
        }
    }
}

/**
 * Process store creation 
 */
static void process_store_creation(TSNode call_node, const char *source, CodemapFile *file, Arena *arena) {
    // Get the variable name to which the store is assigned
    // In tree-sitter, we need to check the context, but we don't have direct parent access
    char store_name[128] = {0};
    
    // For our standalone test, we'll rely on the store config instead
    
    // If we couldn't get the store name from the variable, try getting it from the configuration object
    if (store_name[0] == '\0') {
        TSNode arguments = find_field(call_node, "arguments");
        if (!ts_node_is_null(arguments) && ts_node_named_child_count(arguments) > 0) {
            TSNode config_obj = ts_node_named_child(arguments, 0);
            if (!ts_node_is_null(config_obj) && strcmp(ts_node_type(config_obj), "object") == 0) {
                // Search for name property in the object
                uint32_t child_count = ts_node_named_child_count(config_obj);
                for (uint32_t i = 0; i < child_count; i++) {
                    TSNode pair = ts_node_named_child(config_obj, i);
                    if (ts_node_is_null(pair) || strcmp(ts_node_type(pair), "pair") != 0) continue;
                    
                    TSNode key = find_field(pair, "key");
                    if (ts_node_is_null(key)) continue;
                    
                    uint32_t key_start = ts_node_start_byte(key);
                    uint32_t key_end = ts_node_end_byte(key);
                    char key_str[16] = {0};
                    strncpy(key_str, source + key_start, key_end - key_start < 15 ? key_end - key_start : 15);
                    
                    if (strcmp(key_str, "name") == 0) {
                        TSNode value = find_field(pair, "value");
                        if (!ts_node_is_null(value)) {
                            extract_string_literal(value, source, store_name, sizeof(store_name) - 1);
                            break;
                        }
                    }
                }
            }
        }
    }
    
    // If we still couldn't get a name, use a default
    if (store_name[0] == '\0') {
        strcpy(store_name, "unnamed-store");
    }
    
    // Create a store entry
    CodemapEntry *entry = allocate_entry(file, arena);
    strncpy(entry->name, store_name, sizeof(entry->name) - 1);
    entry->kind = CM_STORE;
    
    // Try to get state keys from the configuration object
    TSNode arguments = find_field(call_node, "arguments");
    if (!ts_node_is_null(arguments) && ts_node_named_child_count(arguments) > 0) {
        TSNode config_obj = ts_node_named_child(arguments, 0);
        if (!ts_node_is_null(config_obj) && strcmp(ts_node_type(config_obj), "object") == 0) {
            // Look for state property
            uint32_t child_count = ts_node_named_child_count(config_obj);
            for (uint32_t i = 0; i < child_count; i++) {
                TSNode pair = ts_node_named_child(config_obj, i);
                if (ts_node_is_null(pair) || strcmp(ts_node_type(pair), "pair") != 0) continue;
                
                TSNode key = find_field(pair, "key");
                if (ts_node_is_null(key)) continue;
                
                uint32_t key_start = ts_node_start_byte(key);
                uint32_t key_end = ts_node_end_byte(key);
                char key_str[16] = {0};
                strncpy(key_str, source + key_start, key_end - key_start < 15 ? key_end - key_start : 15);
                
                if (strcmp(key_str, "state") == 0) {
                    TSNode value = find_field(pair, "value");
                    if (!ts_node_is_null(value) && strcmp(ts_node_type(value), "object") == 0) {
                        // We found the state object, get its properties as keys
                        char keys[256] = "{";
                        size_t keys_len = 1;
                        
                        uint32_t state_props = ts_node_named_child_count(value);
                        for (uint32_t j = 0; j < state_props; j++) {
                            TSNode prop_pair = ts_node_named_child(value, j);
                            if (ts_node_is_null(prop_pair) || strcmp(ts_node_type(prop_pair), "pair") != 0) continue;
                            
                            TSNode prop_key = find_field(prop_pair, "key");
                            if (ts_node_is_null(prop_key)) continue;
                            
                            uint32_t prop_start = ts_node_start_byte(prop_key);
                            uint32_t prop_end = ts_node_end_byte(prop_key);
                            
                            // Only add if we have space
                            if (keys_len + (prop_end - prop_start) + 2 < sizeof(keys)) {
                                if (keys_len > 1) {
                                    // Add separator between keys
                                    keys[keys_len++] = ',';
                                    keys[keys_len++] = ' ';
                                }
                                
                                // Add the key name
                                strncpy(keys + keys_len, source + prop_start, prop_end - prop_start);
                                keys_len += prop_end - prop_start;
                            }
                            
                            if (keys_len >= sizeof(keys) - 2) break;
                        }
                        
                        // Close the state signature
                        keys[keys_len++] = '}';
                        keys[keys_len] = '\0';
                        
                        // Set the signature to the state keys
                        strncpy(entry->signature, keys, sizeof(entry->signature) - 1);
                    }
                    break;
                }
            }
        }
    }
}

/**
 * Process React component or Web Component
 */
static void process_component(TSNode call_node, const char *source, CodemapFile *file, Arena *arena) {
    // Check if this is customElements.define
    TSNode function_node = find_field(call_node, "function");
    if (ts_node_is_null(function_node)) return;
    
    if (strcmp(ts_node_type(function_node), "member_expression") != 0) return;
    
    TSNode object = find_field(function_node, "object");
    TSNode property = find_field(function_node, "property");
    
    if (ts_node_is_null(object) || ts_node_is_null(property)) return;
    
    if (strcmp(ts_node_type(object), "identifier") != 0 || 
        strcmp(ts_node_type(property), "property_identifier") != 0) return;
    
    // Check if it's customElements.define
    uint32_t obj_start = ts_node_start_byte(object);
    uint32_t obj_end = ts_node_end_byte(object);
    char obj_name[32] = {0};
    strncpy(obj_name, source + obj_start, obj_end - obj_start < 31 ? obj_end - obj_start : 31);
    
    uint32_t prop_start = ts_node_start_byte(property);
    uint32_t prop_end = ts_node_end_byte(property);
    char prop_name[32] = {0};
    strncpy(prop_name, source + prop_start, prop_end - prop_start < 31 ? prop_end - prop_start : 31);
    
    if (strcmp(obj_name, "customElements") != 0 || strcmp(prop_name, "define") != 0) return;
    
    // Get arguments
    TSNode arguments = find_field(call_node, "arguments");
    if (ts_node_is_null(arguments) || ts_node_named_child_count(arguments) < 2) return;
    
    // First argument is the component name
    TSNode component_name_node = ts_node_named_child(arguments, 0);
    char component_name[128] = {0};
    if (!extract_string_literal(component_name_node, source, component_name, sizeof(component_name) - 1)) {
        return;
    }
    
    // Create a component entry
    CodemapEntry *entry = allocate_entry(file, arena);
    strncpy(entry->name, component_name, sizeof(entry->name) - 1);
    entry->kind = CM_COMPONENT;
    
    // Second argument is the class definition
    TSNode class_def = ts_node_named_child(arguments, 1);
    if (ts_node_is_null(class_def)) return;
    
    const char *class_type = ts_node_type(class_def);
    if (strcmp(class_type, "class") == 0 || strcmp(class_type, "class_declaration") == 0) {
        // Look for methods
        TSNode class_body = find_field(class_def, "body");
        if (ts_node_is_null(class_body)) return;
        
        char methods[256] = "{";
        size_t methods_len = 1;
        
        uint32_t child_count = ts_node_named_child_count(class_body);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode method = ts_node_named_child(class_body, i);
            if (ts_node_is_null(method)) continue;
            
            const char *method_type = ts_node_type(method);
            if (strcmp(method_type, "method_definition") != 0) continue;
            
            TSNode method_name = find_field(method, "name");
            if (ts_node_is_null(method_name)) continue;
            
            uint32_t name_start = ts_node_start_byte(method_name);
            uint32_t name_end = ts_node_end_byte(method_name);
            
            // Only add if we have space
            if (methods_len + (name_end - name_start) + 2 < sizeof(methods)) {
                if (methods_len > 1) {
                    // Add separator between methods
                    methods[methods_len++] = ',';
                    methods[methods_len++] = ' ';
                }
                
                // Add the method name
                strncpy(methods + methods_len, source + name_start, name_end - name_start);
                methods_len += name_end - name_start;
            }
            
            if (methods_len >= sizeof(methods) - 2) break;
        }
        
        // Close the methods signature
        methods[methods_len++] = '}';
        methods[methods_len] = '\0';
        
        // Set the signature to the methods
        strncpy(entry->signature, methods, sizeof(entry->signature) - 1);
    }
}

/**
 * Process a Tree-sitter node and extract code entities
 */
static void process_node(TSNode node, const char *source, CodemapFile *file, Arena *arena) {
    if (ts_node_is_null(node)) return;
    
    const char *node_type = ts_node_type(node);
    
    // Check for call expressions, which could be Cami.js API calls
    if (strcmp(node_type, "call_expression") == 0) {
        TSNode function_node = find_field(node, "function");
        
        if (!ts_node_is_null(function_node)) {
            const char *func_type = ts_node_type(function_node);
            
            if (strcmp(func_type, "identifier") == 0) {
                // Direct function call like store(...)
                uint32_t start = ts_node_start_byte(function_node);
                uint32_t end = ts_node_end_byte(function_node);
                char function_name[32] = {0};
                strncpy(function_name, source + start, end - start < 31 ? end - start : 31);
                
                if (strcmp(function_name, "store") == 0 || strcmp(function_name, "createStore") == 0) {
                    process_store_creation(node, source, file, arena);
                }
            } else if (strcmp(func_type, "member_expression") == 0) {
                // Method call like storeObj.defineAction(...)
                TSNode object = find_field(function_node, "object");
                TSNode property = find_field(function_node, "property");
                
                if (!ts_node_is_null(object) && !ts_node_is_null(property)) {
                    const char *prop_type = ts_node_type(property);
                    if (strcmp(prop_type, "property_identifier") == 0) {
                        uint32_t prop_start = ts_node_start_byte(property);
                        uint32_t prop_end = ts_node_end_byte(property);
                        char property_name[32] = {0};
                        strncpy(property_name, source + prop_start, prop_end - prop_start < 31 ? prop_end - prop_start : 31);
                        
                        // Get store name
                        uint32_t obj_start = ts_node_start_byte(object);
                        uint32_t obj_end = ts_node_end_byte(object);
                        char store_name[128] = {0};
                        strncpy(store_name, source + obj_start, obj_end - obj_start < 127 ? obj_end - obj_start : 127);
                        
                        if (strcmp(property_name, "defineAction") == 0 ||
                            strcmp(property_name, "defineAsyncAction") == 0 ||
                            strcmp(property_name, "defineQuery") == 0 ||
                            strcmp(property_name, "defineMutation") == 0 ||
                            strcmp(property_name, "defineMemo") == 0) {
                            
                            process_cami_definition(node, source, file, arena, store_name);
                        } else if (strcmp(property_name, "define") == 0 && 
                                  strcmp(ts_node_type(object), "identifier") == 0 &&
                                  strcmp(store_name, "customElements") == 0) {
                            
                            process_component(node, source, file, arena);
                        }
                    }
                }
            }
        }
    }
    
    // Also check for standard function and class declarations
    else if (strcmp(node_type, "function_declaration") == 0) {
        TSNode name_node = find_field(node, "name");
        if (!ts_node_is_null(name_node)) {
            uint32_t start = ts_node_start_byte(name_node);
            uint32_t end = ts_node_end_byte(name_node);
            char *name = copy_substring(source, start, end, arena);
            
            if (name) {
                CodemapEntry *entry = allocate_entry(file, arena);
                strncpy(entry->name, name, sizeof(entry->name) - 1);
                
                // Get parameters
                TSNode params = find_field(node, "parameters");
                if (!ts_node_is_null(params)) {
                    uint32_t param_start = ts_node_start_byte(params);
                    uint32_t param_end = ts_node_end_byte(params);
                    strncpy(entry->signature, source + param_start, param_end - param_start < 255 ? param_end - param_start : 255);
                } else {
                    strcpy(entry->signature, "()");
                }
                
                entry->container[0] = '\0';
                entry->kind = CM_FUNCTION;
            }
        }
    }
    
    else if (strcmp(node_type, "class_declaration") == 0) {
        TSNode name_node = find_field(node, "name");
        if (!ts_node_is_null(name_node)) {
            uint32_t start = ts_node_start_byte(name_node);
            uint32_t end = ts_node_end_byte(name_node);
            char *name = copy_substring(source, start, end, arena);
            
            if (name) {
                CodemapEntry *entry = allocate_entry(file, arena);
                strncpy(entry->name, name, sizeof(entry->name) - 1);
                
                // Check for extends clause
                TSNode extends_clause = find_field(node, "super_class");
                if (!ts_node_is_null(extends_clause)) {
                    uint32_t extends_start = ts_node_start_byte(extends_clause);
                    uint32_t extends_end = ts_node_end_byte(extends_clause);
                    char extends_name[64] = "extends ";
                    strncpy(extends_name + 8, source + extends_start, extends_end - extends_start < 55 ? extends_end - extends_start : 55);
                    strncpy(entry->signature, extends_name, sizeof(entry->signature) - 1);
                    
                    // Check if it extends ReactiveElement
                    char extends_class[64] = {0};
                    strncpy(extends_class, source + extends_start, extends_end - extends_start < 63 ? extends_end - extends_start : 63);
                    if (strcmp(extends_class, "ReactiveElement") == 0) {
                        entry->kind = CM_COMPONENT;
                    } else {
                        entry->kind = CM_CLASS;
                    }
                } else {
                    entry->kind = CM_CLASS;
                }
                
                // Process class body to extract methods
                TSNode class_body = find_field(node, "body");
                if (!ts_node_is_null(class_body)) {
                    uint32_t child_count = ts_node_named_child_count(class_body);
                    for (uint32_t i = 0; i < child_count; i++) {
                        TSNode method = ts_node_named_child(class_body, i);
                        if (ts_node_is_null(method)) continue;
                        
                        const char *method_type = ts_node_type(method);
                        if (strcmp(method_type, "method_definition") != 0) continue;
                        
                        TSNode method_name = find_field(method, "name");
                        if (ts_node_is_null(method_name)) continue;
                        
                        uint32_t m_start = ts_node_start_byte(method_name);
                        uint32_t m_end = ts_node_end_byte(method_name);
                        char *method_name_str = copy_substring(source, m_start, m_end, arena);
                        
                        if (method_name_str) {
                            CodemapEntry *method_entry = allocate_entry(file, arena);
                            strncpy(method_entry->name, method_name_str, sizeof(method_entry->name) - 1);
                            strncpy(method_entry->container, name, sizeof(method_entry->container) - 1);
                            
                            // Get parameters
                            TSNode params = find_field(method, "parameters");
                            if (!ts_node_is_null(params)) {
                                uint32_t param_start = ts_node_start_byte(params);
                                uint32_t param_end = ts_node_end_byte(params);
                                strncpy(method_entry->signature, source + param_start, param_end - param_start < 255 ? param_end - param_start : 255);
                            } else {
                                strcpy(method_entry->signature, "()");
                            }
                            
                            method_entry->kind = CM_METHOD;
                        }
                    }
                }
            }
        }
    }
    
    // Recursively process children
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        process_node(child, source, file, arena);
    }
}

/**
 * Determine language based on file extension
 */
static const TSLanguage* get_language_for_file(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return NULL;
    
    if (strcmp(ext, ".js") == 0 || strcmp(ext, ".jsx") == 0) {
        return tree_sitter_javascript();
    } else if (strcmp(ext, ".ts") == 0) {
        return tree_sitter_typescript();
    } else if (strcmp(ext, ".tsx") == 0) {
        return tree_sitter_tsx();
    }
    
    return NULL;
}

/* ============== Required Interface Functions ============== */

/**
 * Initialize the language pack
 */
bool initialize(void) {
    debug_printf("[DEBUG] Initializing Cami.js language pack");
    return true;
}

/**
 * Clean up resources when the language pack is unloaded
 */
void cleanup(void) {
    debug_printf("[DEBUG] Cleaning up Cami.js language pack");
}

/**
 * Return the list of file extensions supported by this language pack
 */
const char **get_extensions(size_t *count) {
    if (count) {
        *count = lang_extension_count;
    }
    return lang_extensions;
}

/**
 * Parse a source file and extract code structure information
 */
bool parse_file(const char *path, const char *source, size_t source_len, 
               CodemapFile *file, Arena *arena) {
    if (!path || !source || !file || !arena) {
        fprintf(stderr, "ERROR: Invalid arguments to parse_file\n");
        return false;
    }
    
    debug_printf("[DEBUG] Parsing file with Cami language pack: %s", path);
    
    // Create a tree-sitter parser
    TSParser *parser = ts_parser_new();
    if (!parser) {
        fprintf(stderr, "ERROR: Failed to create tree-sitter parser\n");
        return false;
    }
    
    // Get the appropriate language based on file extension
    const TSLanguage *language = get_language_for_file(path);
    if (!language) {
        fprintf(stderr, "ERROR: No language selected for file: %s\n", path);
        ts_parser_delete(parser);
        return false;
    }
    
    if (!ts_parser_set_language(parser, language)) {
        fprintf(stderr, "ERROR: Failed to set parser language\n");
        ts_parser_delete(parser);
        return false;
    }
    
    // Parse the source code
    TSTree *tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)source_len);
    if (!tree) {
        fprintf(stderr, "ERROR: Failed to parse source code: %s\n", path);
        ts_parser_delete(parser);
        return false;
    }
    
    // Get the syntax tree root node
    TSNode root_node = ts_tree_root_node(tree);
    
    // Process the AST to extract code entities
    process_node(root_node, source, file, arena);
    
    // Clean up resources
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    
    printf("Successfully extracted %zu code entities from %s\n", file->entry_count, path);
    return true;
}