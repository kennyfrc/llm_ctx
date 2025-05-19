# Language Pack Template

This is a template for creating language packs for LLM_CTX. Use this as a starting point to add support for new programming languages.

## Getting Started

1. **Copy this template directory** for your language:
   ```bash
   cp -r template your-language
   cd your-language
   ```

2. **Rename the template files**:
   ```bash
   mv template_pack.c your_lang_pack.c
   ```

3. **Install Tree-sitter for your language**:
   ```bash
   # Clone the grammar repository
   git clone https://github.com/tree-sitter/tree-sitter-your-language ../tree-sitter-your-language
   
   # Build the grammar
   cd ../tree-sitter-your-language
   npm install  # If using Node.js build system
   # or manual compilation if needed
   ```

4. **Update the files**:
   - Edit `Makefile` to point to your language's Tree-sitter grammar
   - Modify `tree-sitter.h` to include your language's function declaration
   - Implement `your_lang_pack.c` with language-specific parsing logic

## Required Components

### 1. Interface Functions

Your language pack must implement these functions:

```c
// Initialize resources
bool initialize(void);

// Free resources when unloaded
void cleanup(void);

// Return supported file extensions
const char **get_extensions(size_t *count);

// Parse a file and extract code entities
bool parse_file(const char *path, const char *source, size_t source_len, 
               CodemapFile *file, Arena *arena);
```

### 2. Tree-sitter Integration

For Tree-sitter integration, you need to:

1. Update the `tree_sitter_your_language()` function declaration
2. Implement node traversal to extract code entities
3. Map language-specific constructs to LLM_CTX's code map structure

### 3. File Extensions

Define the file extensions your language pack will handle:

```c
static const char *lang_extensions[] = {".ext1", ".ext2", NULL};
static size_t lang_extension_count = 2;
```

## Building Your Language Pack

After implementing your language pack, build it with:

```bash
make
```

This will create `parser.so` which LLM_CTX will automatically discover and load.

## Testing

Create a test file with sample code in your language and test the parser:

```bash
make test
```

## Implementation Tips

1. **Node Types**: Each language's Tree-sitter grammar has specific node types. Print them during development to understand the structure.

2. **Memory Management**: Use the provided Arena allocator for all memory allocations.

3. **Error Handling**: Implement robust error handling to prevent crashes.

4. **Documentation**: Update this README.md with language-specific information.

## Example: Code Entity Extraction

```c
// Example function detection pattern
if (strcmp(node_type, "function_declaration") == 0) {
    // Extract function name and details
    // Add to codemap entries
}
```

Refer to the JavaScript language pack for a complete implementation example.