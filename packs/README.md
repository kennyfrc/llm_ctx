# Language Pack Developer Guide

This guide explains how to create and use language packs for LLM_CTX's codemap feature.

## What are Language Packs?

Language packs are plugins that enable LLM_CTX to understand the structure of source code in specific programming languages. They extract functions, classes, methods, and other code entities to provide a structured view for better context in LLM prompts.

## How Language Packs Work

Language packs use Tree-sitter's query language for pattern matching:

1. **Pattern Definition**: Define patterns in `codemap.scm` using S-expressions
2. **Minimal C Code**: The C file just loads queries and processes matches
3. **Easy Maintenance**: Update patterns without recompiling C code
4. **Declarative**: Describe what to find, not how to find it

Example `codemap.scm`:
```scheme
; Functions
(function_declaration
  name: (identifier) @function.name
  parameters: (formal_parameters) @function.params) @function

; Classes  
(class_declaration
  name: (identifier) @class.name) @class
```

## Developer Workflow: Creating a Language Pack

Creating a language pack involves defining patterns in a `.scm` file and writing minimal C code to process matches.

### 1. Getting Started

Start by copying the template:

```bash
# Copy the template
cp -r packs/template packs/your-language
cd packs/your-language
mv template_pack.c your_lang_pack.c

# The template includes:
# - codemap.scm with example patterns
# - template_pack.c with the query-based implementation
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

You'll need to update four key files:

1. **codemap.scm**: Define Tree-sitter query patterns
   ```scheme
   ; Function declarations
   (function_declaration
     name: (identifier) @function.name
     parameters: (formal_parameters) @function.params) @function
   
   ; Class declarations
   (class_declaration
     name: (identifier) @class.name) @class
   ```

2. **Makefile**: Point to your grammar library
   ```makefile
   TS_LANG_LIB = ../../tree-sitter-your-language/libtree-sitter-your-language.a
   
   parser.so: your_lang_pack.c
       $(CC) $(CFLAGS) -o $@ $< $(TS_LANG_LIB) -L/opt/homebrew/lib -ltree-sitter
   ```

3. **tree-sitter.h**: Update with Query API declarations
   ```c
   extern const TSLanguage *tree_sitter_your_language(void);
   // Query API declarations are already included in the template
   ```

4. **your_lang_pack.c**: The template already includes the query-based implementation
   - Just update the language function and file extensions
   - The query loading and processing code is ready to use

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

// Load query patterns from codemap.scm
static TSQuery *load_query_file(const TSLanguage *language) {
    FILE *f = fopen("codemap.scm", "r");
    if (!f) {
        fprintf(stderr, "Could not open codemap.scm\n");
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *query_source = malloc(size + 1);
    fread(query_source, 1, size, f);
    query_source[size] = '\0';
    fclose(f);
    
    uint32_t error_offset;
    TSQueryError error_type;
    TSQuery *query = ts_query_new(language, query_source, strlen(query_source), &error_offset, &error_type);
    
    if (!query) {
        fprintf(stderr, "Query compilation failed at offset %u: %d\n", error_offset, error_type);
    }
    
    free(query_source);
    return query;
}

// Process query matches
static void process_match(const TSQueryMatch *match, TSQuery *query, const char *source, 
                         CodemapFile *file, Arena *arena) {
    // Extract captures based on pattern names
    for (uint16_t i = 0; i < match->capture_count; i++) {
        const TSQueryCapture *capture = &match->captures[i];
        uint32_t length;
        const char *capture_name = ts_query_capture_name_for_id(query, capture->index, &length);
        
        // Process based on capture name (e.g., "function.name", "class.name", etc.)
        // See template_pack.c for full implementation
    }
}

bool parse_file(const char *path, const char *source, size_t source_len, 
               CodemapFile *file, Arena *arena) {
    // Create parser and set language
    TSParser *parser = ts_parser_new();
    const TSLanguage *language = tree_sitter_your_language();
    ts_parser_set_language(parser, language);
    
    // Parse the source
    TSTree *tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)source_len);
    TSNode root_node = ts_tree_root_node(tree);
    
    // Load query patterns from codemap.scm
    TSQuery *query = load_query_file(language);
    if (!query) {
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return false;
    }
    
    // Execute queries
    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, root_node);
    
    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match)) {
        process_match(&match, query, source, file, arena);
    }
    
    // Clean up
    ts_query_cursor_delete(cursor);
    ts_query_delete(query);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    
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

### How to write query patterns

#### Functions

In your `codemap.scm` file:

```scheme
; Function declarations
(function_declaration
  name: (identifier) @function.name
  parameters: (formal_parameters) @function.params) @function

; Function expressions  
(function_expression
  name: (identifier)? @function.name
  parameters: (formal_parameters) @function.params) @function

; Arrow functions
(arrow_function
  parameters: (_) @function.params) @function
```

The C code will automatically process these patterns when matches are found.

#### Classes and Methods

```scheme
; Class declarations
(class_declaration
  name: (identifier) @class.name) @class

; Method definitions (JavaScript)
(method_definition
  name: (property_identifier) @method.name
  parameters: (formal_parameters) @method.params) @method

; Method definitions (Ruby)
(method
  name: (identifier) @method.name
  parameters: (method_parameters)? @method.params) @method
```

Note: The current implementation treats methods as functions for simplicity. To maintain class context, you would need to enhance the query processing logic.

### How to detect and extract interfaces/types

```scheme
; TypeScript interfaces
(interface_declaration
  name: (type_identifier) @type.name) @type

; Type aliases
(type_alias_declaration
  name: (type_identifier) @type.name) @type

; Ruby modules (treated as types)
(module
  name: (constant) @type.name) @type
```

### Query Pattern Tips

1. **Use wildcards for flexibility**:
   ```scheme
   ; Match any parameter type
   parameters: (_)? @function.params
   ```

2. **Optional captures**:
   ```scheme
   ; Function name is optional for anonymous functions
   name: (identifier)? @function.name
   ```

3. **Multiple patterns for variants**:
   ```scheme
   ; Different function syntaxes
   (function_declaration ...) @function
   (function_expression ...) @function
   (arrow_function ...) @function
   ```

4. **Test your patterns**:
   Use the Tree-sitter playground or CLI to verify your patterns match correctly.

### Debugging Query Patterns

To debug your patterns:

1. **Use ts-cli to test patterns**:
   ```bash
   echo '(function_declaration name: (identifier) @name)' > test.scm
   tree-sitter query test.scm test.js
   ```

2. **Add debug output in process_match**:
   ```c
   printf("Capture: %.*s (name: %s)\n", 
          (int)(end - start), source + start, capture_name);
   ```

3. **Check for query compilation errors**:
   The error_offset tells you exactly where in your .scm file the error occurred.

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

### Query-Based Pattern Matching

The new approach uses Tree-sitter's query language instead of manual tree traversal:

1. Define patterns in `codemap.scm` using S-expressions
2. Tree-sitter compiles these patterns into an efficient query
3. The query cursor finds all matches in the syntax tree
4. Each match contains named captures (e.g., `@function.name`)
5. The C code processes these captures to build codemap entries

This approach is more maintainable and declarative than manual traversal.

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

// Query API
TSQuery *ts_query_new(const TSLanguage *language, const char *source, 
                      uint32_t source_len, uint32_t *error_offset, 
                      TSQueryError *error_type);
void ts_query_delete(TSQuery *query);
TSQueryCursor *ts_query_cursor_new(void);
void ts_query_cursor_delete(TSQueryCursor *cursor);
void ts_query_cursor_exec(TSQueryCursor *cursor, const TSQuery *query, TSNode node);
bool ts_query_cursor_next_match(TSQueryCursor *cursor, TSQueryMatch *match);
const char *ts_query_capture_name_for_id(const TSQuery *query, uint32_t id, uint32_t *length);
```

---

## Migrating from Manual Tree Traversal

If you have an existing language pack using manual tree traversal, here's how to migrate:

1. **Identify the node types you're checking**: Look for `strcmp(node_type, "...")` calls
2. **Convert to query patterns**: Each node type check becomes a pattern in `codemap.scm`
3. **Map extractions to captures**: Replace manual `ts_node_child()` calls with named captures
4. **Simplify the C code**: Replace `process_node()` with query loading and match processing

Example migration:

**Old approach** (manual traversal):
```c
if (strcmp(node_type, "function_declaration") == 0) {
    TSNode name_node = find_child_by_type(node, "identifier");
    // ... extract and process
}
```

**New approach** (query pattern):
```scheme
(function_declaration
  name: (identifier) @function.name) @function
```

See the JavaScript and Ruby packs for complete migration examples.

---

## Troubleshooting

Common issues and their solutions when developing language packs.

### Query Compilation Errors

**Query fails to compile:**
```
Query compilation failed at offset 123: Unknown error
```

**Common causes and solutions**:
1. **Invalid node types**: Check the grammar's node types using `tree-sitter parse`
2. **Syntax errors**: Ensure parentheses match and captures are properly named
3. **Complex patterns**: Simplify nested patterns that Tree-sitter can't handle

**Debug approach**:
```bash
# Test your query file
tree-sitter query codemap.scm test-file.js
```

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