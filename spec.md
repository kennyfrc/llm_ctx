# Dynamic Language Pack Support for Codemap

## Problem Statement

Currently, the codemap feature in `llm_ctx` is specifically designed for JavaScript/TypeScript files only. This limits its usefulness in polyglot codebases. We need to extend the system to dynamically support multiple languages through a flexible plugin architecture, where adding a new language pack immediately enables codemap generation for files of that language without requiring recompilation or code changes to the main program.

## Usage Code / Examples

```bash
# Generate codemap for the entire codebase
llm_ctx -m

# Generate codemap for specific language files
llm_ctx -m "src/**/*.js"

# Generate codemap for multiple file patterns
llm_ctx -m "src/**/*.js,lib/**/*.rb"

# Generate codemap while including file content in context
llm_ctx -f "src/**/*.js" -m

# Generate codemap for entire codebase, ignoring gitignore patterns
llm_ctx -m --no-gitignore

# List installed language packs
llm_ctx --list-packs

# Get information about language support
llm_ctx --pack-info python
```

Example output with multiple languages:

```xml
<code_map>
[src/main.js]
Classes:
  UserManager:
    methods:
      - constructor(config)
      - createUser(userData)
      - deleteUser(id)

Functions:
  authenticate           (username, password) -> boolean
  formatName             (firstName, lastName) -> string

[lib/user.rb]
Classes:
  User:
    methods:
      - initialize(attributes)
      - save
      - validate
      - to_json

Modules:
  Authentication:
    methods:
      - included(base)
      - authenticate(login, password)

[app/models/account.py]
Classes:
  Account:
    methods:
      - __init__(self, user_id, balance)
      - deposit(self, amount)
      - withdraw(self, amount)
      
Functions:
  create_account         (user_id) -> Account
  delete_account         (account_id) -> bool
</code_map>
```

## Functional Requirements

1. **Plugin Architecture**
   * Support language packs as dynamically loaded libraries (.so/.dll/.dylib)
   * Organize packs in a standardized directory structure: `./packs/<language_name>/parser.so`
   * Enable symlinked packs to work regardless of execution directory
   * Provide a consistent interface for all language packs

2. **Language Pack Discovery**
   * Automatically detect installed language packs at runtime
   * Map file extensions to appropriate language parsers
   * Fallback gracefully when a language pack is missing

3. **Unified Codemap Generation**
   * Generate a consistent codemap structure regardless of language
   * Normalize language-specific constructs to common concepts (functions, classes, etc.)
   * Handle mixed-language projects cleanly
   * Traverse the entire codebase by default when codemap is enabled
   * Respect gitignore patterns unless explicitly overridden
   * Allow specific file/directory patterns to limit codemap generation scope
   * Operate independently of file content selection (-f flag)

4. **Pack Management**
   * Add `make pack <language>` command to download and build language packs
   * Provide command-line options to list/query available packs
   * Support pack versioning for tracking compatibility

5. **Error Handling**
   * Fail-soft when encountering unsupported file types
   * Clear error messages when language packs are missing or incompatible
   * Detailed logs for troubleshooting parser failures

## Data Model

```c
/* Language Pack Interface */
typedef struct {
    const char *name;              /* Language name (e.g. "javascript") */
    const char **extensions;       /* File extensions (e.g. {".js", ".jsx", NULL}) */
    size_t extension_count;        /* Number of supported extensions */
    void *handle;                  /* Dynamic library handle */
    
    /* Function pointers for language-specific operations */
    bool (*initialize)(void);      /* Initialize the language parser */
    void (*cleanup)(void);         /* Clean up language parser resources */
    bool (*parse_file)(const char *path, const char *source, size_t source_len, 
                       CodemapFile *file, Arena *arena);  /* Parse a file */
} LanguagePack;

/* Registry of installed language packs */
typedef struct {
    LanguagePack *packs;           /* Array of language packs */
    size_t pack_count;             /* Number of loaded packs */
    char **extension_map;          /* Maps extensions to pack indices */
    size_t extension_map_size;     /* Size of extension map */
} PackRegistry;

/* Extended Codemap Entry with language-specific types */
typedef struct {
    char  name[128];               /* Identifier */
    char  signature[256];          /* Params incl. "(...)" */
    char  return_type[64];         /* "void" default for unknown */
    char  container[128];          /* Class/namespace for methods, empty otherwise */
    CMKind kind;                   /* Function, class, method, type, etc. */
    char  language[32];            /* Source language */
    char  kind_name[32];           /* Language-specific kind name (e.g. "Module" in Ruby) */
} ExtendedCodemapEntry;
```

## Algorithm Design

The design eliminates special cases by using a unified interface for all language packs, regardless of their specific parsing approach:

1. **Language Pack Registration & Discovery**
   * All packs conform to the same `LanguagePack` interface
   * Discovery scans the packs directory once at startup
   * Extensions are registered in a lookup table for constant-time mapping
   * Zero-initialized packs represent "none" which simplifies conditional logic

2. **Unified File Processing**
   * All files pass through a single processing pipeline
   * File extension determines the language pack to use
   * If no language pack is available, file is processed normally (content only, no codemap)
   * The process is data-driven, not control-flow driven
   * Codemap generation operates independently from file content processing
   * By default, codemap scans the entire codebase for supported file types
   * When pattern arguments are provided to the codemap flag, only files matching those patterns are processed
   * Gitignore patterns are respected during codemap file scanning

3. **Normalized Codemap Generation**
   * Each language maps its native constructs to our common model (functions, classes, etc.)
   * Language-specific constructs get normalized kind_names but still appear in the output
   * Empty/Error states are represented as valid empty structs, not special branches

4. **Special Case Handling**
   * Files without extensions use content-based detection instead of special logic
   * Parser failures are captured within the pack's parse_file function, not in the calling code
   * Missing language packs result in empty codemaps, not errors

This approach eliminates edge cases by:
1. Using a consistent interface for all languages
2. Representing "missing" or "unsupported" as valid empty states  
3. Pushing language-specific complexity inside the pack implementation
4. Making the main path uniform regardless of language

## Vertical Slice Plan

### 1. Walking Skeleton: Basic Pack Discovery
- Implement basic pack registry that scans packs/ directory
- Add test that verifies existing JavaScript pack is found
- Support running `llm_ctx -m --list-packs` to show available packs
- This slice connects user input to pack discovery and output

### 2. Language Pack Interface Definition
- Define consistent C interface that all language packs must implement
- Create a mock "test" language pack that follows this interface
- Extend main.c to dynamically load this pack
- Verify test pack is discovered and loaded correctly

### 3. File-to-Language Mapping
- Implement extension-to-language mapping logic
- Enhance codemap.c to use the appropriate pack based on file extension
- Make JavaScript parser conform to the new interface
- Test with existing JS files to ensure no regression

### 4. Generic Codemap Generation
- Extend codemap generation to handle multiple languages
- Make codemap output show language-specific constructs
- Update test_cli.c to test multi-language support
- Test with JavaScript files plus mock language files

### 5. Ruby Language Pack
- Implement Ruby language pack as proof of concept
- Add Tree-sitter Ruby grammar support
- Create make target for downloading/building Ruby pack
- Test end-to-end with Ruby files

### 6. Pack Management & Documentation
- Finalize make pack <language> implementation
- Add documentation for creating custom language packs
- Implement --pack-info command for detailed language info
- Complete test suite for pack-related functionality

### 7. Enhanced Codemap Independence
- Decouple codemap generation from file content selection
- Implement whole codebase traversal by default when -m is used
- Add support for pattern arguments to -m flag
- Ensure proper gitignore pattern respect in codemap file scanning
- Update CLI help text to document new codemap functionality
- Update relevant tests to verify codemap enhancements

Each slice delivers a functional improvement that can be tested independently, building on previous work while providing value at each step.
