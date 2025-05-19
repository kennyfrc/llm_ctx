# LLM_CTX Architecture Guide

This document provides a comprehensive explanation of how the main components of LLM_CTX work, including `main.c`, the arena allocator (`arena.h`/`arena.c`), and the Tree-sitter integration with code mapping and language packs.

## 1. Main Program Architecture (`main.c`)

LLM_CTX is a utility for extracting file content with fenced blocks to provide context for Large Language Models (LLMs). It follows the Unix philosophy of small, focused tools that can be composed with other programs using pipes.

### Core Functionality

- Processes source code files and formats them with appropriate markdown code blocks
- Extracts code structure (functions, classes, methods) for better context
- Handles various file types and detects binary files
- Supports gitignore patterns to exclude unwanted files
- Generates directory tree structures
- Processes STDIN for piped content

### Key Components

#### 1.1 Memory Management with Arena Allocator

The program uses a global arena allocator (`g_arena`) for all memory needs:

```c
static Arena g_arena;
// Reserve 64 MiB for all allocations used by the application
// This avoids frequent malloc/free calls and simplifies cleanup.
```

Helper functions for arena allocation:

```c
static void *arena_xalloc(size_t size);
static void *arena_xrealloc(void *oldp, size_t old_size, size_t new_size);
static char *arena_xstrdup(const char *s);
```

#### 1.2 File Processing

The core functionality revolves around these operations:

- Recursive directory scanning
- File filtering (via gitignore patterns)
- Content extraction with appropriate fencing
- Binary file detection
- Special file type handling (e.g., JSON, XML, markdown)

#### 1.3 Configuration and Flags

Configuration is handled through:

1. Command-line arguments (parsed using `getopt_long`)
2. Configuration files (`.llm_ctx.conf` in various locations)
3. Global defaults

Important flags:
- `-e/--editor-comments`: Enable editor comments in response guide
- `-c/--command`: Specify user instructions or problem statement
- `-s/--system`: Specify system prompt
- `-r/--raw`: Raw mode (disable special processing)
- `-m/--codemap`: Enable codemap generation

#### 1.4 Workflow

1. Parse command-line options
2. Initialize systems (arena, pack registry)
3. Find and load configuration
4. Process input files or STDIN
5. Generate file tree and codemap if requested
6. Output formatted content to temporary file or stdout
7. Copy to clipboard if configured
8. Clean up resources

## 2. Arena Allocator (`arena.h` & `arena.c`)

The arena allocator provides efficient memory management by allocating large blocks of memory at once and managing allocations from these blocks.

### 2.1 Core Concept

Instead of many small `malloc()` calls, the arena:
1. Allocates a single large memory region upfront
2. Allocates from this region by simply advancing a pointer
3. Frees everything at once when the program exits

### 2.2 Structure

```c
typedef struct Arena {
    unsigned char *base;  // Base address of memory block
    size_t pos;           // Current position (next free byte)
    size_t size;          // Total size of the arena
#ifdef ARENA_ENABLE_COMMIT
    size_t commit;        // Optional: for virtual memory optimization
#endif
} Arena;
```

### 2.3 Key Operations

#### Creation and Destruction

```c
// Create an arena with specified size
Arena arena_create(size_t reserve);

// Destroy the arena, releasing the memory
void arena_destroy(Arena *a);

// Reset the arena (keep memory, just reset position)
void arena_clear(Arena *a);
```

#### Memory Allocation

```c
// Allocate memory with specific size and alignment
void *arena_push_size(Arena *a, size_t size, size_t align);

// Convenience macros for typed allocations
#define arena_push(arena,T) ((T*)arena_push_size((arena),sizeof(T),__alignof__(T)))
#define arena_push_array(arena,T,count) ((T*)arena_push_size((arena),sizeof(T)*(count),__alignof__(T)))
```

#### Position Management

```c
// Get current position (useful for later reverting)
size_t arena_get_mark(Arena *a);

// Set position (to discard allocations)
void arena_set_mark(Arena *a, size_t mark);
```

### 2.4 Benefits

1. **Performance**: Fewer system calls, better locality
2. **Simplicity**: No need to track individual allocations
3. **Safety**: No memory leaks or double-frees
4. **Efficiency**: Quick temp allocations with rollback

## 3. Language Packs and Tree-sitter Integration

LLM_CTX uses Tree-sitter to parse code and extract code structure (functions, classes, methods) for better context.

### 3.1 Language Pack Architecture

#### Core Components

Language packs are plugins that understand specific programming languages. They:
1. Parse source code into an abstract syntax tree (AST)
2. Extract code entities (functions, classes, methods)
3. Provide this information back to LLM_CTX

#### Pack Registry

```c
typedef struct {
    LanguagePack *packs;          // Array of language packs
    size_t pack_count;            // Number of loaded packs
    char **extension_map;         // Maps extensions to pack indices
    size_t extension_map_size;    // Size of extension map
} PackRegistry;
```

#### Language Pack Interface

```c
struct LanguagePack {
    char name[64];                // Language name (e.g. "javascript")
    char path[4096];              // Full path to the parser.so
    bool available;               // Whether the pack is actually available
    
    // Extension handling
    const char **extensions;      // File extensions (e.g. {".js", ".jsx", NULL})
    size_t extension_count;       // Number of supported extensions
    
    // Dynamic library handle
    void *handle;                 // Dynamic library handle (from dlopen)
    
    // Function pointers for language-specific operations
    bool (*initialize)(void);     // Initialize the language parser
    void (*cleanup)(void);        // Clean up language parser resources
    bool (*parse_file)(const char *path, const char *source, size_t source_len, 
                       CodemapFile *file, Arena *arena);  // Parse a file
};
```

### 3.2 Tree-sitter Integration

[Tree-sitter](https://tree-sitter.github.io/tree-sitter/) is a parser generator tool and incremental parsing library that can build a concrete syntax tree for source files.

#### Two-Part Architecture

Tree-sitter has a critical two-part architecture:

1. **Language Grammar**: Specific to each language (e.g., `libtree-sitter-javascript.a`)
   - Contains the parsing rules for a specific language
   - Defines the node types and structure of the syntax tree
   - Is compiled separately for each language

2. **Runtime Library**: Common across all languages (`libtree-sitter`)
   - Provides the parsing engine and API functions
   - Handles memory management and tree traversal
   - Must be linked separately from the grammar

#### Integration in JavaScript Pack

As shown in the JavaScript pack example:

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

### 3.3 Codemap Generation

The codemap is a structured representation of code entities in the processed files.

#### Data Structures

```c
typedef enum { 
    CM_FUNCTION, 
    CM_CLASS, 
    CM_METHOD, 
    CM_TYPE 
} CMKind;

typedef struct {
    char  name[128];          // Identifier
    char  signature[256];     // Params incl. "(...)"
    char  return_type[64];    // "void" default for unknown
    char  container[128];     // Class name for methods, empty otherwise
    CMKind kind;
} CodemapEntry;

typedef struct {
    char           path[MAX_PATH];
    CodemapEntry  *entries;   // arena array
    size_t         entry_count;
} CodemapFile;

typedef struct {
    CodemapFile *files;       // arena array, 1:1 with processed files
    size_t       file_count;
} Codemap;
```

#### Processing Flow

1. File extensions are mapped to language packs
2. For each file, the appropriate language pack's `parse_file` function is called
3. The parse function builds an AST and extracts code entities
4. Entities are added to the codemap structure
5. The codemap is formatted and added to the output

### 3.4 Creating a Language Pack

To create a new language pack, follow these steps:

1. **Set up the directory structure**:
   ```
   packs/your-language/
   ├── Makefile           # Build instructions
   ├── your_lang_pack.c   # Implementation
   ├── tree-sitter.h      # Tree-sitter API declarations
   └── README.md          # Documentation
   ```

2. **Implement the required interface functions**:
   ```c
   bool initialize(void);
   void cleanup(void);
   const char **get_extensions(size_t *count);
   bool parse_file(const char *path, const char *source, size_t source_len, 
                  CodemapFile *file, Arena *arena);
   ```

3. **Create a Makefile** that links both the language grammar and the runtime library:
   ```makefile
   TS_LANG_LIB = ../../tree-sitter-your-language/libtree-sitter-your-language.a
   
   parser.so: your_lang_pack.c
       $(CC) $(CFLAGS) -o $@ $< $(TS_LANG_LIB) -L/opt/homebrew/lib -ltree-sitter
   ```

4. **Process the AST** to extract code entities:
   ```c
   static void process_node(TSNode node, const char *source, CodemapFile *file, 
                           Arena *arena, const char *container) {
       // Extract functions, classes, methods based on node types
       // Add them to the codemap
   }
   ```

## 4. Integration Points and Data Flow

### 4.1 Overall Flow

1. `main.c` initializes the arena allocator and pack registry
2. Files are processed based on command-line patterns
3. For each file:
   - If extension matches a language pack, process with code mapping
   - Otherwise, process as regular text
4. After processing, generate the directory tree and codemap
5. Output the formatted content

### 4.2 Memory Management

- The global arena allocator is used for all allocations
- Language packs receive a pointer to the arena for their allocations
- This ensures all memory is freed at once when the program exits

### 4.3 Extension to New Languages

The plugin architecture allows easy extension to new languages:

1. Create a new subdirectory in `packs/`
2. Implement the required interface
3. Build the language pack
4. LLM_CTX automatically discovers and loads it

## 5. Key Design Principles

The LLM_CTX project follows several key design principles:

1. **Single Allocation Strategy**: The arena allocator simplifies memory management by allocating all memory upfront and freeing it at once.

2. **Plugin Architecture**: Language packs are loaded dynamically, allowing for easy extension without modifying the core code.

3. **Unix Philosophy**: The tool is designed to do one thing well and integrate with other tools via pipes.

4. **Robustness**: Extensive error checking and resource management ensure the tool works reliably in various environments.

5. **Efficiency**: By using Tree-sitter for parsing and the arena allocator for memory management, the tool maintains high performance even with large codebases.

## 6. Conclusion

LLM_CTX is a well-structured utility that combines efficient memory management, file processing, and code structure extraction to provide rich context for LLM interactions. The integration with Tree-sitter through language packs enables accurate code parsing across multiple languages, while the arena allocator ensures efficient memory use.

Its plugin architecture makes it extensible to support additional languages, and its focus on providing structured code context makes it particularly valuable for code-related LLM tasks.