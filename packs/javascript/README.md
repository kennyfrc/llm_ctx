# JavaScript Language Pack Reference Implementation

This JavaScript language pack serves as the reference implementation for LLM_CTX language packs, providing code structure extraction for JavaScript and TypeScript files using the Tree-sitter parsing library.

## Overview

The JavaScript pack demonstrates best practices for language pack implementation, particularly:

1. **Static linking** with tree-sitter libraries
2. **AST traversal** techniques for robust parsing
3. **Memory management** using the arena allocator
4. **Entity extraction** for functions, classes, methods, and types

## Tutorial: Using the JavaScript Pack

### Step 1: Install Dependencies

The JavaScript pack requires the tree-sitter and tree-sitter-javascript libraries:

```bash
# Install tree-sitter runtime
brew install tree-sitter  # macOS
# apt-get install libtree-sitter-dev  # Ubuntu/Debian

# Clone and build tree-sitter-javascript grammar
git clone https://github.com/tree-sitter/tree-sitter-javascript
cd tree-sitter-javascript
npm install  # Build the grammar
```

### Step 2: Build the Parser

```bash
cd packs/javascript
make clean && make
```

### Step 3: Verify Installation

```bash
make test
```

## Implementation Guide

### Key Components

- **[js_pack.c](./js_pack.c)**: Core implementation with tree-sitter integration
- **[tree-sitter.h](./tree-sitter.h)**: Minimal tree-sitter API definitions
- **[Makefile](./Makefile)**: Build configuration with static linking
- **[parser.so](./parser.so)**: Compiled shared library (output)

### Required Interface Functions

The JavaScript pack implements the four standard language pack functions:

```c
bool initialize(void);
void cleanup(void);
const char **get_extensions(size_t *count);
bool parse_file(const char *path, const char *source, size_t source_len, 
               CodemapFile *file, Arena *arena);
```

## Code Walkthrough

### Tree-sitter Integration

The pack initializes and configures the tree-sitter parser:

```c
// Create a tree-sitter parser
TSParser *parser = ts_parser_new();

// Set the language to JavaScript
const TSLanguage *language = tree_sitter_javascript();
ts_parser_set_language(parser, language);

// Parse the source code
TSTree *tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)source_len);

// Get the syntax tree root node
TSNode root_node = ts_tree_root_node(tree);
```

### AST Traversal Techniques

The `process_node` function recursively traverses the AST:

```c
static void process_node(TSNode node, const char *source, CodemapFile *file, 
                        Arena *arena, const char *current_class) {
    if (ts_node_is_null(node)) return;
    
    const char *node_type = ts_node_type(node);
    
    // Extract entities based on node type
    if (strcmp(node_type, "function_declaration") == 0) {
        // Process function
    }
    else if (strcmp(node_type, "class_declaration") == 0) {
        // Process class and its methods
    }
    else if (strcmp(node_type, "interface_declaration") == 0) {
        // Process TypeScript interface
    }
    
    // Recursively process children
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        process_node(child, source, file, arena, current_class);
    }
}
```

### Special Case: Method Names

The pack handles special cases like constructor methods:

```c
// Check if this is a constructor
const char *first_child_type = ts_node_type(ts_node_child(child, 0));
if (strcmp(first_child_type, "constructor") == 0) {
    method_name = "constructor";
} else {
    // Look for property_identifier child
    for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
        TSNode prop = ts_node_child(child, j);
        const char *prop_type = ts_node_type(prop);
        
        if (strcmp(prop_type, "property_identifier") == 0) {
            uint32_t start = ts_node_start_byte(prop);
            uint32_t end = ts_node_end_byte(prop);
            method_name = copy_substring(source, start, end, arena);
            break;
        }
    }
}
```

### Memory Management

The language pack uses the arena allocator for efficient memory allocation:

```c
// Copy a substring from source code into arena memory
static char* copy_substring(const char *source, uint32_t start, uint32_t end, Arena *arena) {
    uint32_t length = end - start;
    if (length >= 256) length = 255; // Enforce size limit
    
    char *str = (char*)(arena->base + arena->pos);
    memcpy(str, source + start, length);
    str[length] = '\0'; // Null terminate
    
    arena->pos += length + 1;
    return str;
}
```

## Static Linking Approach

The JavaScript pack demonstrates the recommended two-part Tree-sitter linking approach:

1. Link with the **language grammar** (`libtree-sitter-javascript.a`)
2. Link with the **runtime library** (`libtree-sitter`)

This is implemented in the Makefile:

```makefile
TS_JS_LIB = ../../tree-sitter-javascript/libtree-sitter-javascript.a

parser.so: js_pack.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(TS_JS_LIB) -L/opt/homebrew/lib -ltree-sitter
```

## API Reference

### Supported Extensions

- `.js` - JavaScript files
- `.jsx` - React JSX files
- `.ts` - TypeScript files
- `.tsx` - TypeScript with JSX files

### Extracted Entity Types

| Type | Description | Example |
|------|-------------|---------|
| `CM_FUNCTION` | Standalone functions | `function getData() {...}` |
| `CM_CLASS` | Class declarations | `class Person {...}` |
| `CM_METHOD` | Methods within classes | `getFullName() {...}` |
| `CM_TYPE` | TypeScript types/interfaces | `interface User {...}` |

## Troubleshooting

### Common Issues

1. **Missing tree-sitter symbols**
   - Ensure `-ltree-sitter` is included in your link command
   - Check that the Tree-sitter runtime library is properly installed

2. **No entities extracted**
   - Verify the source code contains recognized structures
   - Check that the Tree-sitter grammar correctly parses your input

3. **Build failures**
   - Ensure `libtree-sitter-javascript.a` exists in the expected location
   - Check that the tree-sitter runtime headers are available

### Debugging Output

The JavaScript pack outputs detailed information during parsing:

```
Initializing JavaScript language pack with tree-sitter...
Parsing JavaScript/TypeScript file with tree-sitter: test.js
Successfully extracted 4 code entities from test.js.
Cleaning up JavaScript language pack resources...
```

For more verbose debugging, add additional `printf` statements in `process_node()`.

## Lessons Learned

From implementing this language pack, we learned:

1. **Tree-sitter architecture**: Understanding the two-part architecture (grammar + runtime) is crucial
2. **AST traversal strategies**: Effective AST traversal requires knowledge of the specific language's node types
3. **Special handling for constructors**: Some language elements require special detection logic
4. **Memory management**: Proper use of the arena allocator avoids memory leaks and fragmentation
5. **Static linking benefits**: Static linking provides more reliable and predictable behavior

For detailed information on implementing your own language pack, refer to the [Language Pack Developer Guide](../README.md).