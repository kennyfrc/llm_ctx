# Language Pack Developer Guide

This guide explains how to create and use language packs for LLM_CTX's codemap feature.

## What are Language Packs?

Language packs are plugins that enable LLM_CTX to understand the structure of source code in specific programming languages. They extract functions, classes, methods, and other code entities to provide a structured view for better context in LLM prompts.

## Developer Workflow: Creating a Language Pack

Here's the typical workflow for creating a new language pack:

### 1. Getting Started

Start by copying the template or referencing the JavaScript pack:

```bash
# Option 1: Copy the template
cp -r packs/template packs/your-language
cd packs/your-language
mv template_pack.c your_lang_pack.c

# Option 2: Reference the JavaScript pack
# Study packs/javascript/js_pack.c as a complete example
```

### 2. Using an Existing Tree-sitter Grammar

Most languages already have a Tree-sitter grammar available. Here's how to use one:

```bash
# Clone the Tree-sitter grammar repository
git clone https://github.com/tree-sitter/tree-sitter-your-language

# Build the grammar
cd tree-sitter-your-language
npm install  # If it uses the Node.js build system
# The grammar is now built in the project directory
```

### 3. Modifying Your Pack Files

You'll need to update three key files:

1. **Makefile**: Point to your grammar library
   ```makefile
   TS_LANG_LIB = ../../tree-sitter-your-language/libtree-sitter-your-language.a
   
   parser.so: your_lang_pack.c
       $(CC) $(CFLAGS) -o $@ $< $(TS_LANG_LIB) -L/opt/homebrew/lib -ltree-sitter
   ```

2. **tree-sitter.h**: Update the language function declaration
   ```c
   extern const TSLanguage *tree_sitter_your_language(void);
   ```

3. **your_lang_pack.c**: Implement the required interface functions
   ```c
   bool initialize(void) { /* ... */ }
   void cleanup(void) { /* ... */ }
   const char **get_extensions(size_t *count) { /* ... */ }
   bool parse_file(const char *path, const char *source, size_t source_len, 
                  CodemapFile *file, Arena *arena) { /* ... */ }
   ```

### 4. Testing Your Language Pack

Once your pack is implemented, verify it works:

```bash
# Build your language pack
cd packs/your-language
make

# Test if LLM_CTX can find and load your pack
cd ../..
./llm_ctx --list-packs  # Should show your language in the list

# View detailed information about your pack
./llm_ctx --pack-info your-language  # Should show supported extensions

# Test with an actual file
./llm_ctx -f "path/to/test/file.ext" -m  # Should show codemap
```

### 5. Debugging and Troubleshooting

If your language pack doesn't work as expected:

1. **Check for linking errors**: Make sure both Tree-sitter libraries are linked
2. **Print node types**: Add debug prints in the `process_node` function
3. **Verify extension mappings**: Check that file extensions are properly registered
4. **Test with simple files**: Start with minimal code examples

## How the Pack Loading System Works

The language pack system uses a plugin architecture with these components:

### 1. Pack Discovery

When LLM_CTX starts:
- It scans the `packs/` directory for subdirectories
- For each subdirectory, it checks if a `parser.so` file exists
- If found, it's added to the registry as an available language pack

### 2. Dynamic Loading

For each available pack:
- The `parser.so` file is loaded using `dlopen()`
- Required functions (`initialize`, `cleanup`, etc.) are resolved using `dlsym()`
- The pack's `initialize()` function is called
- Supported file extensions are obtained via `get_extensions()`

### 3. Extension Mapping

After loading all packs:
- A mapping table is built from file extensions to language packs
- When a file is processed, its extension is used to find the appropriate pack
- If no pack is found for an extension, the file is processed as plain text

### 4. File Processing

For each matched file:
- The file is read into memory
- The appropriate language pack's `parse_file()` function is called
- The language pack builds a parse tree using Tree-sitter
- It traverses the tree to extract code entities (functions, classes, etc.)
- These entities are added to the codemap

---

## Tutorial: Creating Your First Language Pack

This tutorial walks you through creating a basic language pack from scratch.

### Prerequisites

1. **Tree-sitter installation**:
   ```bash
   # macOS
   brew install tree-sitter

   # Ubuntu/Debian
   sudo apt-get install libtree-sitter-dev

   # From source
   git clone https://github.com/tree-sitter/tree-sitter
   cd tree-sitter
   make
   sudo make install
   ```

2. **Development tools**:
   - C compiler (gcc/clang)
   - Make
   - Git

### Step 1: Set up your language pack directory

```bash
# Create language pack directory
mkdir -p packs/your-language

# Clone the Tree-sitter grammar
git clone https://github.com/tree-sitter/tree-sitter-your-language
cd tree-sitter-your-language

# Build the grammar library
npm install  # Only if using Node.js build system
gcc -c src/parser.c -Isrc -o parser.o  # Direct compilation
ar rcs libtree-sitter-your-language.a parser.o
```

### Step 2: Create the basic file structure

In your `packs/your-language/` directory, create these files:

1. **Makefile** - For building your language pack
2. **your_lang_pack.c** - Main implementation
3. **tree-sitter.h** - Tree-sitter API declarations
4. **README.md** - Documentation

### Step 3: Create a minimal tree-sitter.h

```c
// tree-sitter.h
#ifndef TREE_SITTER_H
#define TREE_SITTER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct TSParser TSParser;
typedef struct TSTree TSTree;
typedef struct TSLanguage TSLanguage;

typedef struct {
    const void *tree;
    const void *id;
    uint32_t context[4];
} TSNode;

// Tree-sitter API declarations
extern TSParser *ts_parser_new(void);
extern void ts_parser_delete(TSParser *parser);
extern bool ts_parser_set_language(TSParser *parser, const TSLanguage *language);
extern TSTree *ts_parser_parse_string(TSParser *parser, const TSTree *old_tree, const char *string, uint32_t length);
extern void ts_tree_delete(TSTree *tree);
extern TSNode ts_tree_root_node(const TSTree *tree);

// Node API
extern uint32_t ts_node_child_count(TSNode node);
extern TSNode ts_node_child(TSNode node, uint32_t index);
extern const char *ts_node_type(TSNode node);
extern uint32_t ts_node_start_byte(TSNode node);
extern uint32_t ts_node_end_byte(TSNode node);
extern bool ts_node_is_null(TSNode node);

// Your language's Tree-sitter function
extern const TSLanguage *tree_sitter_your_language(void);

#endif
```

### Step 4: Implement the language pack interface

```c
// your_lang_pack.c
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "tree-sitter.h"

// Required typedefs for code mapping
typedef enum {
    CM_FUNCTION = 0,
    CM_CLASS = 1,
    CM_METHOD = 2,
    CM_TYPE = 3
} CMKind;

typedef struct {
    char  name[128];
    char  signature[256];
    char  return_type[64];
    char  container[128];
    CMKind kind;
} CodemapEntry;

typedef struct {
    char          path[4096];
    CodemapEntry *entries;
    size_t        entry_count;
} CodemapFile;

typedef struct Arena {
    unsigned char *base;
    size_t pos;
    size_t size;
#ifdef ARENA_ENABLE_COMMIT
    size_t commit;
#endif
} Arena;

// Utility function to copy substring from source
static char *copy_substring(const char *source, uint32_t start, uint32_t end, Arena *arena) {
    size_t len = end - start;
    char *result = (char *)arena->base + arena->pos;
    arena->pos += len + 1;
    memcpy(result, source + start, len);
    result[len] = '\0';
    return result;
}

// Node processing function
static void process_node(TSNode node, const char *source, CodemapFile *file, Arena *arena, const char *container) {
    if (ts_node_is_null(node)) return;
    
    const char *node_type = ts_node_type(node);
    
    // Handle different node types based on your language's grammar
    // Example for a C-like language:
    if (strcmp(node_type, "function_definition") == 0) {
        // Extract function details
        // This is just an example - adjust for your language's grammar
        TSNode name_node = ts_node_child(node, 1);  // Assuming function name is second child
        
        if (!ts_node_is_null(name_node)) {
            uint32_t start = ts_node_start_byte(name_node);
            uint32_t end = ts_node_end_byte(name_node);
            
            CodemapEntry *entry = &file->entries[file->entry_count++];
            strncpy(entry->name, copy_substring(source, start, end, arena), sizeof(entry->name) - 1);
            entry->kind = CM_FUNCTION;
            
            // Fill in other fields as needed
            entry->signature[0] = '\0';
            entry->return_type[0] = '\0';
            
            if (container) {
                strncpy(entry->container, container, sizeof(entry->container) - 1);
            } else {
                entry->container[0] = '\0';
            }
        }
    }
    
    // Process children
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        process_node(child, source, file, arena, container);
    }
}

// Interface functions

static const char *extensions[] = {".yourlang"};  // Replace with your language's extensions

bool initialize(void) {
    // Initialize any resources needed
    return true;
}

void cleanup(void) {
    // Clean up resources
}

const char **get_extensions(size_t *count) {
    *count = sizeof(extensions) / sizeof(extensions[0]);
    return extensions;
}

bool parse_file(const char *path, const char *source, size_t source_len, 
               CodemapFile *file, Arena *arena) {
    printf("Parsing file with tree-sitter: %s\n", path);
    
    // Create parser
    TSParser *parser = ts_parser_new();
    if (!parser) {
        fprintf(stderr, "Failed to create tree-sitter parser\n");
        return false;
    }
    
    // Set language
    const TSLanguage *language = tree_sitter_your_language();
    if (!ts_parser_set_language(parser, language)) {
        fprintf(stderr, "Failed to set language for parser\n");
        ts_parser_delete(parser);
        return false;
    }
    
    // Parse the source code
    TSTree *tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)source_len);
    if (!tree) {
        fprintf(stderr, "Failed to parse source code\n");
        ts_parser_delete(parser);
        return false;
    }
    
    // Get the root node and process the tree
    TSNode root_node = ts_tree_root_node(tree);
    process_node(root_node, source, file, arena, NULL);
    
    // Clean up
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    
    printf("Successfully extracted %zu code entities from %s.\n", file->entry_count, path);
    return true;
}
```

### Step 5: Create a Makefile

```makefile
# Makefile for your language pack
CC = gcc
CFLAGS = -Wall -Wextra -fPIC -shared

# Path to Tree-sitter grammar library
TS_LANG_LIB = ../../tree-sitter-your-language/libtree-sitter-your-language.a

# Build the language pack
parser.so: your_lang_pack.c
	$(CC) $(CFLAGS) -o $@ $< $(TS_LANG_LIB) -L/opt/homebrew/lib -ltree-sitter

clean:
	rm -f parser.so

test: parser.so
	echo "Testing your language pack..."
	# Add test commands here

.PHONY: clean test
```

### Step 6: Test your language pack

Create a simple test program to verify your language pack works:

```c
// test_lang_pack.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../your_lang_pack.c"  // Include the implementation directly for testing

int main() {
    // Test sample code
    const char *sample_code = "function test() { return 42; }";
    
    // Create temporary file and arena
    CodemapFile file = {0};
    strcpy(file.path, "test.yourlang");
    
    // Allocate memory for entries and arena
    file.entries = malloc(10 * sizeof(CodemapEntry));
    Arena arena = {0};
    arena.base = malloc(1024 * 1024);  // 1MB arena
    arena.size = 1024 * 1024;
    
    // Initialize and parse
    initialize();
    if (parse_file(file.path, sample_code, strlen(sample_code), &file, &arena)) {
        printf("Parsing succeeded!\n");
        printf("Found %zu entries:\n", file.entry_count);
        
        for (size_t i = 0; i < file.entry_count; i++) {
            printf("  %zu. %s: %s\n", i+1, 
                  file.entries[i].kind == CM_FUNCTION ? "Function" :
                  file.entries[i].kind == CM_CLASS ? "Class" :
                  file.entries[i].kind == CM_METHOD ? "Method" :
                  "Type",
                  file.entries[i].name);
        }
    } else {
        printf("Parsing failed!\n");
    }
    
    // Clean up
    cleanup();
    free(file.entries);
    free(arena.base);
    
    return 0;
}
```

---

## How-to Guide: Language Pack Implementation Patterns

This section provides practical recipes for common language pack tasks.

### How to handle different code entities

#### Functions

```c
// For function declarations/definitions
if (strcmp(node_type, "function_declaration") == 0 ||
    strcmp(node_type, "function_definition") == 0) {
    
    // Find the identifier/name node
    TSNode name_node = find_child_by_type(node, "identifier");
    
    if (!ts_node_is_null(name_node)) {
        uint32_t start = ts_node_start_byte(name_node);
        uint32_t end = ts_node_end_byte(name_node);
        
        // Extract the function name
        char *func_name = copy_substring(source, start, end, arena);
        
        // Add to codemap entries
        CodemapEntry *entry = &file->entries[file->entry_count++];
        strncpy(entry->name, func_name, sizeof(entry->name) - 1);
        entry->kind = CM_FUNCTION;
        
        // Extract parameters for signature (implementation depends on language grammar)
        extract_function_signature(node, source, entry, arena);
    }
}
```

#### Classes and Methods

```c
// For class declarations
if (strcmp(node_type, "class_declaration") == 0) {
    // Find the class name
    TSNode name_node = find_child_by_type(node, "identifier");
    
    if (!ts_node_is_null(name_node)) {
        uint32_t start = ts_node_start_byte(name_node);
        uint32_t end = ts_node_end_byte(name_node);
        
        // Extract the class name
        char *class_name = copy_substring(source, start, end, arena);
        
        // Add class to codemap entries
        CodemapEntry *entry = &file->entries[file->entry_count++];
        strncpy(entry->name, class_name, sizeof(entry->name) - 1);
        entry->kind = CM_CLASS;
        
        // Find class body to process methods
        TSNode body_node = find_child_by_type(node, "class_body");
        if (!ts_node_is_null(body_node)) {
            // Process body node for methods, passing class_name as container
            process_node(body_node, source, file, arena, class_name);
        }
        
        // Skip automatic processing of children since we handled the body
        return;
    }
}

// For methods within a class
if (strcmp(node_type, "method_definition") == 0 && container != NULL) {
    // Find the method name
    TSNode name_node = find_child_by_type(node, "property_identifier");
    
    if (!ts_node_is_null(name_node)) {
        uint32_t start = ts_node_start_byte(name_node);
        uint32_t end = ts_node_end_byte(name_node);
        
        // Extract the method name
        char *method_name = copy_substring(source, start, end, arena);
        
        // Add method to codemap entries
        CodemapEntry *entry = &file->entries[file->entry_count++];
        strncpy(entry->name, method_name, sizeof(entry->name) - 1);
        strncpy(entry->container, container, sizeof(entry->container) - 1);
        entry->kind = CM_METHOD;
        
        // Extract parameters for signature
        extract_method_signature(node, source, entry, arena);
    }
}
```

### How to detect and extract interfaces/types

```c
// For interface or type declarations (e.g., TypeScript)
if (strcmp(node_type, "interface_declaration") == 0 ||
    strcmp(node_type, "type_alias_declaration") == 0) {
    
    // Find the identifier node
    TSNode name_node = find_child_by_type(node, "type_identifier");
    
    if (!ts_node_is_null(name_node)) {
        uint32_t start = ts_node_start_byte(name_node);
        uint32_t end = ts_node_end_byte(name_node);
        
        // Extract the type name
        char *type_name = copy_substring(source, start, end, arena);
        
        // Add to codemap entries
        CodemapEntry *entry = &file->entries[file->entry_count++];
        strncpy(entry->name, type_name, sizeof(entry->name) - 1);
        entry->kind = CM_TYPE;
    }
}
```

### How to handle special cases in class methods

```c
// Special case for constructors
if (strcmp(node_type, "method_definition") == 0) {
    TSNode name_node = ts_node_child(node, 0);
    const char *method_type = ts_node_type(name_node);
    
    if (strcmp(method_type, "constructor") == 0) {
        // Handle constructor specially
        CodemapEntry *entry = &file->entries[file->entry_count++];
        strcpy(entry->name, "constructor");
        strncpy(entry->container, container, sizeof(entry->container) - 1);
        entry->kind = CM_METHOD;
        
        // Extract constructor parameters
        extract_method_signature(node, source, entry, arena);
    }
}
```

### How to create a utility for finding specific child nodes

```c
// Utility function to find a child node by its type
TSNode find_child_by_type(TSNode parent, const char *type) {
    uint32_t child_count = ts_node_child_count(parent);
    
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(parent, i);
        const char *child_type = ts_node_type(child);
        
        if (strcmp(child_type, type) == 0) {
            return child;
        }
        
        // Optionally recurse into children
        TSNode result = find_child_by_type(child, type);
        if (!ts_node_is_null(result)) {
            return result;
        }
    }
    
    // Return null node if not found
    return ts_node_child(parent, ts_node_child_count(parent)); // This creates a null node
}
```

---

## Explanation: How Language Packs Work with LLM_CTX

This section explains the concepts and architecture of language packs.

### Core Architecture

Language packs in LLM_CTX follow a plugin architecture where each pack implements a standard interface. When LLM_CTX processes source code, it:

1. **Discovery**: Finds available language packs in the `packs/` directory
2. **Extension Mapping**: Maps file extensions to appropriate language packs
3. **Loading**: Loads the language pack when a matching file is encountered
4. **Parsing**: Calls the language pack's `parse_file` function to extract code structure
5. **Integration**: Uses the extracted information to build the codemap for LLM context

### The Two-Part Tree-sitter Architecture

Tree-sitter has a critical two-part architecture that must be understood:

1. **Language Grammar**: Specific to each language (e.g., `libtree-sitter-javascript.a`)
   - Contains the parsing rules for a specific language
   - Defines the node types and structure of the syntax tree
   - Is compiled separately for each language

2. **Runtime Library**: Common across all languages (`libtree-sitter`)
   - Provides the parsing engine and API functions
   - Handles memory management and tree traversal
   - Must be linked separately from the grammar

**Important**: Both components must be properly linked for parsing to work. Many issues arise from failing to link with the runtime library.

### Memory Management with Arena Allocator

LLM_CTX uses an arena allocator to efficiently manage memory for parsed code entities:

1. The arena is pre-allocated and passed to the language pack
2. The language pack allocates memory from this arena for strings and data structures
3. This avoids individual malloc/free calls and simplifies memory management
4. The entire arena is freed at once when processing is complete

### Abstract Syntax Tree Traversal

Tree-sitter provides a structured syntax tree that must be traversed to extract code entities:

1. Start with the root node from `ts_tree_root_node()`
2. Recursively visit each node
3. Identify nodes of interest based on their types (e.g., "function_declaration")
4. Extract information from the relevant nodes
5. Build up the codemap entries as you traverse

The specific node types and structure depend on the language grammar, so you'll need to understand your language's Tree-sitter grammar.

---

## Reference: Language Pack Interface

This section provides complete reference information for the language pack API.

### Required Interface Functions

Every language pack must implement these four functions:

```c
// Initialize any resources needed by the language pack
bool initialize(void);

// Free resources when the language pack is unloaded
void cleanup(void);

// Return a list of file extensions this pack handles
const char **get_extensions(size_t *count);

// Parse a file and extract code structure
bool parse_file(
    const char *path,       // Path to the file
    const char *source,     // Source code content
    size_t source_len,      // Length of source code
    CodemapFile *file,      // Output structure to fill
    Arena *arena            // Memory allocator
);
```

### Codemap Data Structures

The language pack fills these structures to communicate code entities:

```c
// Type of code entity
typedef enum {
    CM_FUNCTION = 0,  // Function definition
    CM_CLASS = 1,     // Class declaration
    CM_METHOD = 2,    // Method within a class
    CM_TYPE = 3       // Interface, type alias, etc.
} CMKind;

// Individual code entity
typedef struct {
    char  name[128];        // Name of the entity
    char  signature[256];   // Function/method signature
    char  return_type[64];  // Return type (if applicable)
    char  container[128];   // Container (e.g., class name for methods)
    CMKind kind;            // Type of entity
} CodemapEntry;

// Collection of entities for a file
typedef struct {
    char           path[4096];   // File path
    CodemapEntry  *entries;      // Array of entities
    size_t         entry_count;  // Number of entities
} CodemapFile;
```

### Arena Allocator API

The arena allocator provides memory management:

```c
typedef struct Arena {
    unsigned char *base;  // Base address of memory block
    size_t pos;           // Current position
    size_t size;          // Total size
#ifdef ARENA_ENABLE_COMMIT
    size_t commit;        // Commit point for transactions
#endif
} Arena;

// To allocate memory from the arena:
void *data = (void *)(arena->base + arena->pos);
arena->pos += size_needed;
// Always check that arena->pos <= arena->size
```

### Tree-sitter Key Functions

Essential Tree-sitter functions used in language packs:

```c
// Parser creation and setup
TSParser *ts_parser_new(void);
void ts_parser_delete(TSParser *parser);
bool ts_parser_set_language(TSParser *parser, const TSLanguage *language);

// Parsing
TSTree *ts_parser_parse_string(TSParser *parser, const TSTree *old_tree, 
                               const char *string, uint32_t length);
void ts_tree_delete(TSTree *tree);
TSNode ts_tree_root_node(const TSTree *tree);

// Node inspection
uint32_t ts_node_child_count(TSNode node);
TSNode ts_node_child(TSNode node, uint32_t index);
const char *ts_node_type(TSNode node);
uint32_t ts_node_start_byte(TSNode node);
uint32_t ts_node_end_byte(TSNode node);
bool ts_node_is_null(TSNode node);
```

---

## Troubleshooting

Common issues and their solutions when developing language packs.

### Linking Problems

**Undefined symbols errors:**
```
Undefined symbols for architecture arm64:
  "_ts_parser_new", referenced from...
```

**Solution**: Add `-ltree-sitter` to link with the runtime library:
```makefile
$(CC) $(CFLAGS) -o $@ $< $(TS_LANG_LIB) -L/opt/homebrew/lib -ltree-sitter
```

### Tree-sitter Grammar Issues

**No parse tree generated:**
```
Failed to parse source code
```

**Solution**: Verify your language grammar is correctly built:
```bash
# Check if grammar library exists
ls -l libtree-sitter-your-language.a

# Check symbols in the library
nm libtree-sitter-your-language.a | grep tree_sitter_your_language
```

### No Entities Found

**Parsing succeeds but no code entities extracted:**

**Solution**: Debug by printing node types during traversal:
```c
// Add to process_node function
printf("Node type: %s\n", node_type);
```

### Node Type Mismatches

**Wrong node types in your code:**

**Solution**: Create a simple test to dump the full tree structure:
```c
void print_tree(TSNode node, const char *source, int depth) {
    if (ts_node_is_null(node)) return;
    
    // Print indentation
    for (int i = 0; i < depth; i++) printf("  ");
    
    // Print node type
    const char *type = ts_node_type(node);
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    
    // Print node content
    char content[64] = {0};
    if (end - start < sizeof(content) - 1) {
        strncpy(content, source + start, end - start);
        content[end - start] = '\0';
    } else {
        snprintf(content, sizeof(content) - 1, "[content too long]");
    }
    
    printf("%s: '%s'\n", type, content);
    
    // Print children
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        print_tree(ts_node_child(node, i), source, depth + 1);
    }
}
```

## Common Errors and Solutions

1. **Undefined symbols for architecture** (e.g., `ts_parser_new`):
   - **Problem**: Missing tree-sitter runtime library
   - **Solution**: Add `-ltree-sitter` to your link command

2. **Failed to load language grammar**:
   - **Problem**: Grammar library not found or incompatible
   - **Solution**: Check that the grammar is built correctly and linked

3. **No code entities found**:
   - **Problem**: Parser not recognizing syntax structures
   - **Solution**: Debug by printing node types during traversal

4. **Constructors not detected properly**:
   - **Problem**: Special handling required for constructor methods
   - **Solution**: Add explicit check for "constructor" node type

5. **Memory allocation failures**:
   - **Problem**: Arena allocator exhausted
   - **Solution**: Check arena size and memory usage patterns