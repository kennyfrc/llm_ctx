# llm_ctx

`llm_ctx` is a command-line utility that formats code or text from files and standard input (stdin) into structured context for LLMs (such as Claude.ai, ChatGPT, Gemini AI Studio). It supports Unix-style composition, allowing integration with tools like `grep`, `find`, and `git diff` via pipes (`|`). Key features include file tree generation, respecting `.gitignore` rules, skipping binary files (see Limitations), and automatic clipboard copying by default.

**Quick Examples:**

1.  **Review the last commit** (automatically copied to clipboard):
    ```bash
    git diff HEAD~1 | llm_ctx -c "Review the changes in this commit"
    ```

2.  **Paste text directly** for analysis (automatically copied to clipboard):
    ```bash
    llm_ctx -C
    # --> Paste your text here...
    # --> Press Ctrl+D when finished
    ```
    *(This reads instructions directly from your terminal input until EOF)*

3.  **Analyze specific React component files** in the `src` directory (automatically copied to clipboard):
    ```bash
    llm_ctx -f 'src/components/Button.{js,jsx,css}' -c "Analyze this React Button component and its styles."
    ```
    *(Note: `{}` expansion might depend on your shell)*

4.  **Provide context for the entire project** (respecting `.gitignore`, automatically copied to clipboard):
    ```bash
    # Use a recursive glob. Assuming that .gitignore has node_modules/, build/, *.log etc., these will be skipped.
    llm_ctx -f '**/*' -c "Please conduct a code review of this project, and find any potential bugs."
    ```
    *(Warning: This can generate a lot of context!)*
    
5.  **Generate a code map with function/class/type information** for your project (automatically copied to clipboard):
    ```bash
    llm_ctx -m -f 'src/**/*.{js,c,h}' -c "Explain how the key components in this project interact."
    ```
    *(Uses pattern matching by default, enhanced with Tree-sitter when available - see [Code Map Feature](#code-map-feature))*

6.  **Ensure your context fits within model limits** (automatically copied to clipboard):
    ```bash
    # Uses default 96k token budget; shows token usage automatically
    llm_ctx -f 'src/**/*.py' -c "Review this Python codebase"
    # Shows: Token usage: 45231 / 96000 (47% of budget)
    
    # Set a custom budget (diagnostics shown automatically)
    llm_ctx -f 'src/**/*.py' -b 100000 -c "Review this Python codebase"
    # If over budget, exits with code 3 before copying
    ```
    *(Token counting built with `make all` - see [Token Counting](#token-counting-and-budget-management))*

7.  **View project structure with full directory tree** (automatically copied to clipboard):
    ```bash
    llm_ctx -t -f 'src/**/*.py' -c "Explain the architecture of this Python project"
    # Shows complete directory tree plus content of selected Python files
    ```

8.  **Get a quick overview of project structure** without file content:
    ```bash
    llm_ctx -O -t -o
    # Shows only the directory tree structure to stdout (not clipboard)
    ```

9.  **Control tree depth** for large projects:
    ```bash
    llm_ctx -t -L 2 -f 'src/**/*.js' -c "Review this JavaScript project"
    # Shows only 2 levels deep in the tree, preventing overwhelming output
    ```

*(To output to stdout instead of clipboard, use the `-o` flag)*

**Quick Links:**

*   [Installation](#installation)
*   [Tutorials](#tutorials)
*   [How-To Guides](#how-to-guides)
*   [Reference](#reference)
*   [Limitations](#limitations)
*   [Design Decisions](#design-decisions)
*   [Testing](#testing)
*   [License](#license)

## Installation

### Prerequisites

- C compiler (gcc/clang)
- make
- POSIX-compliant system

### Compatibility

- macOS (M1/Apple Silicon) - Fully tested
- Linux - Should work, not fully tested
- Windows (WSL) - Should work under WSL, not fully tested

### Getting Started (Build & Use)

Follow these steps to get the code, build it, and make it easily accessible:

1.  **Clone the Repository:**
    Get the source code from GitHub:
    ```bash
    git clone https://github.com/kennyfrc/llm_ctx.git
    cd llm_ctx
    ```

2.  **Build the Executable:**
    Compile the source code using `make all`. This builds both `llm_ctx` and the tokenizer for automatic token counting.
    ```bash
    make all
    ```
    You can run the tool directly from this directory: `./llm_ctx --help`.

3.  **Add `llm_ctx` to your PATH:**
    Use `make symlink` (recommended) or `make install` to run `llm_ctx` from anywhere.

    ```bash
    # Recommended: Create symlink in /usr/local/bin (default)
    # Use 'make symlink PREFIX=~/bin' for a custom location (ensure it's in PATH)
    make symlink

    # Alternative: Install system-wide (usually requires sudo)
    # sudo make install
    ```
    Now run: `llm_ctx --help`

## Tutorials

This section guides you through the basic usage of `llm_ctx`.

### Getting Started: Piping vs. File Arguments

`llm_ctx` can receive input in two main ways:

1.  **Piping from stdin:** Use the output of another command (like `git diff`, `cat`, `grep`) as input. This is the default behavior.
    ```bash
    git diff | llm_ctx
    ```
2.  **File arguments with `-f`:** Specify one or more files or glob patterns directly.
    ```bash
    llm_ctx -f main.c utils.h 'src/**/*.js'
    ```

### Analyzing Your First File

Let's analyze a single file:

1.  **Create a test file:**
    ```bash
    echo "Hello, llm_ctx!" > test.txt
    ```
2.  **Run `llm_ctx`:**
    ```bash
    llm_ctx -f test.txt
    ```
3.  **Observe the output:** You'll see a structure like this, showing the file tree and the file's content within fenced blocks:
    ```
    <file_tree>
    .
    └── test.txt
    </file_tree>

    <file_context>

    File: test.txt

    Hello, llm_ctx!

    ----------------------------------------

    </file_context>
    ```

### Analyzing Git Changes

A common use case is reviewing code changes:

1.  **Make a change** in a file within a Git repository.
2.  **Pipe `git diff` to `llm_ctx`:**
    ```bash
    git diff | llm_ctx
    ```
3.  **Add instructions** for the LLM using the `-c` flag:
    ```bash
    git diff | llm_ctx -c "Explain these changes and suggest improvements."
    ```
    The output will now include a `<user_instructions>` block.

### Automatic Clipboard Copying

`llm_ctx` automatically copies its output to your system clipboard, making it easy to paste into LLM web UIs. If you need the output on stdout instead (for piping to other commands or saving to a file), use the `-o` flag:

```bash
# Default: copies to clipboard
git diff HEAD~1 | llm_ctx -c "Review this commit"

# With -o: outputs to stdout
git diff HEAD~1 | llm_ctx -o -c "Review this commit" > review.txt

# Save to a file directly
git diff HEAD~1 | llm_ctx -o@review.txt -c "Review this commit"
```

## How-To Guides

This section provides goal-oriented steps for common tasks.

### How to Request PR-Style Review Comments

Use the `-e` (or `--editor-comments`) flag along with `-c` to instruct the LLM to format part of its response like a pull request review, with specific suggestions tied to the code.

```bash
# Analyze main.c and ask for review comments (automatically copied to clipboard)
llm_ctx -f main.c -c "Review this function for thread safety" -e
```
This adds specific instructions within the `<response_guide>` block in the output.

### How to Analyze Specific Files

Use the `-f` flag followed by the paths to the files:
```bash
llm_ctx -f path/to/file1.c path/to/another/file2.js
```

### How to Analyze Files Using Patterns

Use glob patterns with the `-f` flag. Remember to quote patterns to prevent your shell from expanding them prematurely.

*   **Simple Glob (current directory):**
    ```bash
    llm_ctx -f 'src/*.c'
    ```
*   **Recursive Glob (current directory and subdirectories):**
    ```bash
    llm_ctx -f 'src/**/*.js'
    ```

### How to Analyze Content Piped from Other Commands

Pipe the output of any command directly into `llm_ctx`.

*   **From `cat`:**
    ```bash
    cat report.json | llm_ctx -c "Summarize this JSON report"
    ```
*   **From `git show` (specific file version):**
    ```bash
    git show HEAD:src/main.c | llm_ctx -c "Explain this version of main.c"
    ```
*   **From `find` and `xargs`:**
    ```bash
    find src -name "*.py" | xargs llm_ctx -f -c "Review these Python files"
    ```

### How to Analyze a React Component and its Related Files

You can use glob patterns or list files explicitly. Brace expansion `{}` can be useful but depends on your shell (like bash or zsh).

*   **Using Brace Expansion (if your shell supports it):**
    ```bash
    llm_ctx -f 'src/components/UserProfile.{jsx,module.css,test.js}' -c "Review the UserProfile component, its styles, and tests."
    ```
*   **Listing Files Explicitly:**
    ```bash
    llm_ctx -f src/components/UserProfile.jsx src/components/UserProfile.module.css src/components/UserProfile.test.js -c "Review the UserProfile component, its styles, and tests."
    ```
*   **Using `find` (more robust):**
    ```bash
    find src/components -name 'UserProfile*' | xargs llm_ctx -f -c "Review the UserProfile component and related files."
    ```

### How to Provide Context for the Entire Project

Use a recursive glob pattern (`**/*`). `llm_ctx` will automatically use your `.gitignore` file to exclude ignored files and directories like `node_modules/`, `build/`, `.env`, etc.

```bash
# Capture all non-ignored files recursively from the current directory
llm_ctx -f '**/*' -c "Analyze the overall structure and key parts of this project."
```
**Warning:** This can generate a very large amount of text, potentially exceeding LLM context limits. Use with caution.

### How to Exclude Files/Directories (Using `.gitignore`)

`llm_ctx` automatically respects `.gitignore` rules found in the current directory and parent directories. This is the primary way to exclude files.

1.  **Ensure a `.gitignore` file exists** in your project structure (or create one).
2.  **Add patterns** for files or directories you want to ignore. Common examples for web development include:
    ```gitignore
    # Dependencies
    node_modules/

    # Build artifacts
    build/
    dist/

    # Logs
    *.log
    npm-debug.log*

    # Environment variables
    .env
    .env.local
    ```
3.  **Run `llm_ctx`** with a broad pattern (like `**/*`) or specific files. Files matching `.gitignore` patterns will be automatically skipped.
    ```bash
    # This includes all non-ignored files
    llm_ctx -f '**/*'

    # This includes *.js files but skips those in node_modules/, build/, etc.
    llm_ctx -f '**/*.js'
    ```

### How to Include Files That Are Normally Ignored (Bypass `.gitignore`)

Use the `--no-gitignore` flag to disable `.gitignore` processing for a specific run:
```bash
# Include config.log even if *.log is in .gitignore
llm_ctx -f --no-gitignore config.log 'src/**/*.c'
```

### How to Add Instructions for the LLM

Use the `-c` flag to provide instructions. There are several ways:

1.  **Inline Text:** Provide the instructions directly on the command line (remember to quote if necessary):
    ```bash
    llm_ctx -f main.c -c "Focus on the main function and look for potential bugs."
    # Or using the equals form:
    llm_ctx -f main.c -c="Focus on the main function..."
    ```

2.  **From a File (`-c @file`):** Read instructions from a specified file. This is useful for complex or reusable prompts.
    ```bash
    # Create a file with your instructions
    echo "Review this code for style consistency and potential errors." > /tmp/review_prompt.txt

    # Use the file with -c @
    llm_ctx -f src/*.c -c @/tmp/review_prompt.txt
    ```

3.  **From Standard Input (`-c @-`):** Read instructions from stdin until EOF (Ctrl+D). This is great for multi-line instructions directly in the terminal or via heredocs in scripts.
    ```bash
    # Type instructions directly (automatically copied to clipboard)
    llm_ctx -f main.c -c @-
    # --> Enter instructions here...
    # --> Press Ctrl+D

    # Use a heredoc in a script or shell
    llm_ctx -c @- -f src/utils.c <<'EOF'
    Please perform the following actions:
    1. Identify potential memory leaks.
    2. Suggest improvements for error handling.
    EOF
    ```
    *(Output is automatically copied to clipboard)*

All these methods add a `<user_instructions>` block to the output.

### How to Control Output Destination

By default, `llm_ctx` automatically copies its output to your system clipboard. You can control this behavior with the `-o` flag:

*   **Default (clipboard):**
    ```bash
    git diff | llm_ctx -c "Review this"
    ```

*   **Output to stdout (for piping or redirection):**
    ```bash
    git diff | llm_ctx -o -c "Review this" | grep "error"
    git diff | llm_ctx -o -c "Review this" > review.txt
    ```

*   **Output directly to a file:**
    ```bash
    git diff | llm_ctx -o@review.txt -c "Review this"
    ```

### How to Combine Different Sources (Advanced)

Use shell command grouping `{ ...; }` or subshells `(...)` to combine outputs before piping to `llm_ctx`:
```bash
# Combine git diff output and the content of all Python files
{ git diff HEAD~1; find . -name "*.py" -exec cat {} +; } | llm_ctx -c "Review the diff and all Python files together."
```

## Reference

This section provides detailed technical information about `llm_ctx`.

### Command-Line Options

```
Usage: llm_ctx [OPTIONS] [FILE...]

Options:
  -c TEXT        Add instruction text wrapped in <user_instructions> tags.
                 Example: -c "Explain this code."
                 Example: -c="Explain this code."

  -c @FILE       Read instruction text from FILE. The file content is used
                 as the instruction text. Useful for multi-line prompts.
                 Example: -c @/path/to/prompt.txt

  -c @-          Read instruction text from standard input until EOF (Ctrl+D).
                 Useful for multi-line instructions or heredocs.
                 Example: echo "Instructions" | llm_ctx -c @- -f file.c

  -C             Shortcut for `-c @-`. Reads user instruction text from
                 standard input until EOF (Ctrl+D).

  -s             (Bare flag) No system prompt is added by default.

  -s TEXT        Add system prompt text wrapped in <system_instructions> tags.
                 Appears before user instructions.
                 Example: -s "You are a helpful assistant."
                 Example: -s="You are a helpful assistant."

  -s@FILE        Read system prompt text from FILE (no space after -s).
                 Overrides any inline text provided with -s TEXT.
                 Example: -s@/path/to/system_prompt.txt

  -s@-           Read system prompt text from stdin until EOF (no space after -s).
                 Example: echo "Be concise" | llm_ctx -s@- -f file.c

  -e, --editor-comments
                 Instruct the LLM to append PR-style review comments to its
                 response. Adds specific instructions to the <response_guide>.

  -r             Raw mode. Omits system instructions and the response guide.

  -o             Output to stdout instead of clipboard (opt-out from default
                 clipboard behavior).

  -o@FILE        Write output directly to FILE instead of clipboard.
                 Example: -o@output.txt

  -m, --codemap  Generate a code map that shows functions, classes, methods, and
                 types. Uses pattern matching by default, Tree-sitter when available.

  -b N, --token-budget=N
                 Set a token budget limit (default: 96000). If the generated context
                 exceeds this token count (using OpenAI tokenization), the output is
                 rejected and the program exits with code 3. Token usage and detailed
                 diagnostics are displayed automatically when the tokenizer is available.
                 Example: -b 128000 (for GPT-4's context limit)

  --token-model=MODEL
                 Set the OpenAI model for token counting. Different models
                 have different tokenization rules. Default: gpt-4o
                 Example: --token-model=gpt-3.5-turbo

  -d, --debug    Debug mode. Shows additional information about file processing,
                 parsing decisions, and errors.

  -f [FILE...]   Process specified files or glob patterns instead of stdin.
                 Must be followed by one or more file paths or patterns.
                 Example: -f main.c 'src/**/*.js'

  -t, --tree     Generate complete directory tree (global tree) in addition to
                 file content. Shows all files in the project directory.
                 
  -T, --filtered-tree
                 Generate file tree only for specified files in addition to
                 file content. Shows tree structure of files passed with -f.
                 
  -O, --tree-only
                 Generate tree output only, without any file content.
                 Useful for getting a quick overview of project structure.
                 
  -L N, --level=N
                 Limit the depth of the tree display to N levels (default: 4).
                 This helps manage output size for deep directory structures.
                 Example: -L 2 shows only 2 levels deep

  -h, --help     Show this help message and exit.

  --command=TEXT Alias for -c=TEXT.
  --system[=TEXT] Alias for -s[=TEXT]. Optional argument form.
  --files        Alias for -f.

  --no-gitignore Ignore .gitignore files. Process all files matched by
                 arguments or patterns, even if they are listed in .gitignore.
```

### Clipboard Behavior

`llm_ctx` copies its output to the system clipboard by default, making it easy to paste into LLM web interfaces. This behavior can be controlled with the `-o` flag:

*   **Default:** Output is automatically copied to the system clipboard (using `pbcopy` on macOS, `wl-copy`/`xclip` on Linux, `clip.exe` on Windows)
*   **`-o` flag:** Output to stdout instead of clipboard
*   **`-o@filename` flag:** Write output directly to a file

If clipboard copying fails (e.g., clipboard utility not available), `llm_ctx` will automatically fall back to stdout with a warning message.

### Input Methods
`llm_ctx` determines its input source automatically:

1.  **File Arguments (`-f`):** If the `-f` flag is present, all subsequent arguments are treated as file paths or glob patterns to be processed.
2.  **Piped Content (stdin):** If `-f` is *not* present and stdin is *not* connected to a terminal (i.e., it's receiving piped data), `llm_ctx` reads the entire stdin stream as a single block of content (e.g., from `git diff`, `cat`). It attempts to detect the content type (like `diff`, `json`, `xml`) for appropriate fencing.
3.  **Terminal Input (Error/Help):** If `-f` is *not* present and stdin *is* connected to a terminal (i.e., you just run `llm_ctx` interactively), it prints the help message, as it expects input via pipes or the `-f` flag.

### Output Format

The output is structured using simple XML-like tags for clarity:

*   **`<user_instructions>` (Optional):** Contains the text provided via the `-c` flag. Appears first if present.
*   **`<system_instructions>` (Optional):** Contains the text provided via the `-s` flag (either the default or custom from `@FILE`/`@-`). Appears after user instructions if both are present.
*   **`<response_guide>` (Optional):** Appears if `-c` was used. Contains guidance for the LLM on how to structure its response. Includes an initial comment instructing the LLM to follow the guide. Appears after system instructions.
    *   **`<problem_statement>`:** Contains a fixed instruction for the LLM to summarize the user's request based on the overall context provided (including `<user_instructions>` and file content). This ensures the LLM actively processes the request context.
    *   **`<reply_format>`:** Instructions for the LLM's reply structure. If the `-e` or `--editor-comments` flag was used, this section explicitly asks for PR-style code review comments (e.g., using GitHub inline diff syntax) in addition to the main solution/explanation. Otherwise, it indicates that no code review block is needed.
*   **`<file_tree>`:** Shows a tree structure representing the relative paths of the files included in the context. The root of the tree is the common parent directory.
*   **`<file_context>`:** Wraps the content of all processed files.
    *   **`File: <filepath>`:** A header indicating the start of a file's content. The `<filepath>` is relative to the common parent directory identified for the tree.
    *   **```` ```[type] ````:** Standard Markdown fenced code blocks containing the file content. `[type]` is automatically detected for stdin content (e.g., `diff`, `json`) if possible, otherwise it's empty.
    *   **`----------------------------------------`:** A separator line between files within the `<file_context>`.

**Example Structure (with `-c`, `-s` and `-e`):**

```
<user_instructions>
Review this C code for potential memory leaks and suggest improvements.
</user_instructions>

<system_instructions>
You are a senior programmer.
</system_instructions>

<response_guide>
<!-- LLM: Follow the instructions within this response guide -->
  <problem_statement>
Summarize the user's request or problem based on the overall context provided.
  </problem_statement>
  <reply_format>
    1. Provide a clear, step-by-step solution or explanation.
    2. Return **PR-style code review comments**: use GitHub inline-diff syntax, group notes per file, justify each change, and suggest concrete refactors.
  </reply_format>
</response_guide>

<file_tree>
project_root
├── src
│   └── main.c
└── include
    └── utils.h
</file_tree>

<file_context>

File: src/main.c

#include <stdio.h>
#include "utils.h"

int main() {
    printf("Hello!\n");
    print_util();
    return 0;
}

----------------------------------------

File: include/utils.h

#ifndef UTILS_H
#define UTILS_H

void print_util();

#endif

----------------------------------------

</file_context>
```

### Glob Pattern Support

When using the `-f` flag, `llm_ctx` supports standard glob patterns:

*   `*`: Matches any sequence of characters (except `/`).
*   `?`: Matches any single character (except `/`).
*   `[]`: Matches any one character within the brackets. Ranges (`[a-z]`) and negation (`[!0-9]`) are supported.
*   `**`: Matches zero or more directories recursively. This is handled by custom logic in `llm_ctx`. Example: `src/**/*.js` matches all `.js` files in `src` and its subdirectories.
*   `{}`: Brace expansion (e.g., `*.{c,h}`) might work depending on your shell or the system's `glob()` implementation (`GLOB_BRACE`). It's often safer to rely on shell expansion or list patterns separately.

**Note:** It's generally recommended to enclose glob patterns in single quotes (`'`) to prevent your shell from expanding them before `llm_ctx` receives them, especially for patterns involving `*` or `**`.

### `.gitignore` Integration

*   **Automatic Loading:** By default, `llm_ctx` searches for `.gitignore` files in the current directory and all parent directories up to the root.
*   **Standard Rules:** It respects standard `.gitignore` syntax, including:
    *   Blank lines are ignored.
    *   Lines starting with `#` are comments.
    *   Trailing spaces are ignored unless quoted with backslash (`\ `).
    *   Patterns (`*.log`, `build/`).
    *   Negation patterns (`!important.log`) - these override ignore rules.
    *   Directory patterns (ending with `/`).
*   **Precedence:**
    *   Patterns read from files in deeper directories take precedence over those in parent directories.
    *   Later patterns within the same file take precedence over earlier ones.
    *   Negation patterns (`!`) always override ignore patterns for a matching file.
*   **Disabling:** Use the `--no-gitignore` flag to completely skip loading and checking `.gitignore` files.

### Code Map Feature

The `-m` or `--codemap` flag enables code mapping, which extracts and displays functions, classes, methods, and types from source files in a structured format. This provides a high-level overview of the codebase's structure for the LLM.

*   **What it shows:** The code map displays:
    *   **Functions** with their parameter signatures and return types
    *   **Classes** with their methods
    *   **Types/Interfaces**

*   **Implementation Methods:**
    *   **Pattern-based:** By default, `llm_ctx` uses simple regex patterns to extract code structure, which works across various languages.
    *   **Tree-sitter enhanced:** When Tree-sitter language packs are available, `llm_ctx` uses precise AST parsing for superior results.

*   **Example:**
    ```bash
    llm_ctx -m -f 'src/**/*.{js,ts,c,h,rb}'
    ```

*   **Output Format:**
    The output includes a `<code_map>` block between the `<file_tree>` and `<file_context>` sections:

    ```
    <code_map>
    [path/to/file.js]
    Classes:
      ClassName:
        methods:
        - constructor(params)
        - methodName(params) -> returnType
    
    Functions:
      functionName           (params) -> returnType
      anotherFunction        (params)
    
    Types:
      TypeName
    </code_map>
    ```

*   **Debug Mode:**
    Use `-d` or `--debug` with `-m` to see detailed information about the code mapping process:
    ```bash
    llm_ctx -m -d -f 'src/**/*.js'
    ```
    Debug mode shows:
    * Which parser was used (pattern-based or Tree-sitter)
    * Issues with file processing
    * Parsing decisions and errors

*   **Tree-sitter Requirements (Optional but Recommended):**
    To fully utilize the Tree-sitter enhanced parsing:
    1.  The Tree-sitter library (`brew install tree-sitter` on macOS)
    2.  Language packs for the languages you want to analyze

*   **Installing Language Packs:**
    ```bash
    # Install JavaScript language pack
    make pack LANG=javascript
    
    # Install Ruby language pack
    make pack LANG=ruby
    
    # Install all supported language packs
    make packs
    ```

*   **Notes:**
    *   Without Tree-sitter language packs, code mapping still works using pattern-based extraction
    *   Tree-sitter provides superior results but isn't required
    *   Performance limits: Files > 5MB are skipped to avoid excessive memory usage
    *   No strict timeout limits are enforced during parsing

### Token Counting and Budget Management

`llm_ctx` includes automatic token counting to help manage LLM context window limits. This uses OpenAI's tokenization rules (via `tiktoken`) to accurately count tokens before sending to an LLM.

#### Automatic Token Usage Display

When the tokenizer is available, `llm_ctx` automatically displays token usage after every run:

```bash
# Shows token usage with default 96k budget
llm_ctx -f main.c
# Output: Token usage: 27029 / 96000 (28% of budget)
```

#### Building the Tokenizer

The tokenizer is built automatically with `make all`:

```bash
# Build both llm_ctx and the tokenizer
make all

# Or build just the tokenizer separately
make tokenizer

# The tokenizer is optional - llm_ctx works without it
# Token features are gracefully disabled if the library is missing
```

#### Token Budget (`-b`, `--token-budget`)

Set a limit on the number of tokens in the generated context (default: 96000):

```bash
# Use default 96k token limit
llm_ctx -f 'src/**/*.js'

# Set custom limit for GPT-4's 128k context
llm_ctx -f 'src/**/*.js' -b 128000

# If the context exceeds the budget:
# - Token usage shown with percentage over 100%
# - Error message printed to stderr  
# - Program exits with code 3
# - No output is generated (clipboard/stdout/file)
```

Exit codes:
- `0`: Success (within budget)
- `3`: Token budget exceeded
- Other: Standard errors

#### Token Diagnostics (Automatic)

Token diagnostics are now displayed automatically whenever the tokenizer is available, showing a breakdown of token counts per file:

```bash
# Diagnostics shown automatically to stderr
llm_ctx -f 'src/**/*.js'

# Example output:
# Token usage: 14982 / 96000 (15% of budget)
#   Tokens   Category
#   -------  ------------------------
#     6980  <file_tree>
#       12  <user_instructions>
#        6  <system_instructions>
#       98  <response_guide>
#     1254  src/main.js
#      842  src/utils.js
#     6541  <other>
#   -------  ------------------------
#    14982  Total
```

This helps identify:
- Which sections consume the most tokens (file tree, instructions, files)
- Which specific files are largest
- Whether your instructions are too verbose
- How to optimize context usage

#### Token Model Selection (`--token-model`)

Different OpenAI models use different tokenization rules:

```bash
# Use GPT-3.5-turbo tokenization (default: gpt-4o)
llm_ctx -f file.txt --token-model=gpt-3.5-turbo -D

# Supported models include:
# - gpt-4o (default)
# - gpt-4
# - gpt-3.5-turbo
# - text-davinci-003
# And others supported by tiktoken
```

#### Use Cases

1. **Prevent context overflow:**
   ```bash
   # Ensure output fits in Claude's context window
   llm_ctx -f '**/*.py' -b 200000 -c "Review this Python project"
   ```

2. **Optimize file selection:**
   ```bash
   # See which files are largest (diagnostics shown automatically)
   llm_ctx -f 'src/**/*' -o | head -20
   
   # Then exclude large files
   llm_ctx -f 'src/**/*.{js,jsx}' -f '!src/generated/*'
   ```

3. **Monitor prompt token usage:**
   ```bash
   # Check how many tokens your instructions use
   echo "Complex multi-paragraph instructions..." | llm_ctx -C -D
   ```

#### Implementation Notes

- Token counting uses OpenAI's official `tiktoken` library via a C wrapper
- The tokenizer library is loaded dynamically at runtime
- If the library is missing, token features are disabled with a warning
- Token counts are exact, not estimates
- Performance impact is minimal (< 100ms for most contexts)

## Limitations

### Binary File Detection

`llm_ctx` includes a simple heuristic to detect binary files. It checks the beginning of each file for:
1.  Null bytes (`\0`).
2.  Certain non-whitespace control characters (ASCII 0x01-0x1F, excluding tab, newline, carriage return).
 
If either is found, the file is considered binary, and its content is replaced with the placeholder `[Binary file content skipped]` in the output. This prevents large amounts of non-textual data (e.g., images like PNG/JPEG, executables, archives) from cluttering the LLM context.
 
### Text Encoding Handling (UTF-16/UTF-32)

A consequence of the null byte check is that text files encoded in **UTF-16** or **UTF-32** (which often contain null bytes as part of their character representation) are usually **detected as binary** and skipped.

`llm_ctx` is primarily designed for UTF-8 and plain ASCII text files, which are most common in source code repositories.

**Workaround:** If you need to include content from a file encoded in UTF-16, UTF-32, or another encoding that gets incorrectly flagged as binary, you can convert it to UTF-8 *before* piping it to `llm_ctx` using tools like `iconv`:

```bash
# Example: Convert a UTF-16LE log file to UTF-8 before processing
iconv -f UTF-16LE -t UTF-8 important_log.txt | llm_ctx -c "Analyze this log file"
```

### No Explicit Include/Exclude Flags

File inclusion is solely based on the files/patterns provided via `-f` or stdin. Exclusion is handled only via `.gitignore` rules (or the lack thereof if `--no-gitignore` is used). There are no separate `--include` or `--exclude` flags.

## Design Decisions

This section provides context and clarifies design choices.

### Project Philosophy

We designed `llm_ctx` following the Unix philosophy: do one thing well. Its sole focus is gathering and formatting context for LLMs, acting as a composable component in command-line workflows. We chose to use existing tools like `git`, `find`, and `.gitignore` rather than reimplementing their logic within `llm_ctx`.

### Debug Mode and Transparency

The `-d` or `--debug` flag enhances transparency by providing insights into how `llm_ctx` processes files and generates code maps:

* **File Processing:** See which files are included/excluded and why
* **Parser Selection:** Learn whether pattern-based parsing or Tree-sitter AST parsing is being used for each file
* **Error Reporting:** Get detailed information about parsing failures or timeouts
* **Performance Metrics:** View parsing time for large files

This is especially useful when:
* Troubleshooting unexpected file inclusion/exclusion
* Developing new language packs
* Understanding why certain code elements appear (or don't appear) in the code map

### File Selection and Filtering

Unlike some tools with explicit `--include` and `--exclude` flags (like `code2prompt`), `llm_ctx` uses a simpler approach:

*   **Inclusion:** Determined *only* by the file paths and glob patterns provided via the `-f` flag, or the content piped via stdin.
*   **Exclusion:** Determined *only* by `.gitignore` rules (unless disabled by `--no-gitignore`).

### Understanding the Output Structure

The XML-like tags (`<file_tree>`, `<file_context>`, `<code_map>`, etc.) and Markdown fences are chosen for:

*   **LLM Clarity:** Provides clear delimiters for different types of information (instructions, file structure, code map, file content).
*   **Context Preservation:** The file tree and code map help the LLM understand the relationships and structure of files and code entities.
*   **Robustness:** Less likely to be confused with code content compared to using only Markdown.
*   **Easy Parsing:** While designed for LLMs, the structure is simple enough for basic parsing if needed.
*   **Binary File Handling:** If a file is detected as binary (see Limitations), its content is replaced with a placeholder `[Binary file content skipped]` instead of being included within code fences.
*   **Debug Information:** When `-d` is used, additional diagnostic information is included to help identify issues with file processing and code mapping.

### Tips for Effective LLM Context

*   **Be Selective:** Only include files relevant to your query. Use `.gitignore` effectively or provide specific file paths/patterns.
*   **Use Instructions (`-c`):** Clearly state what you want the LLM to do with the provided context.
*   **Combine Sources:** Use shell techniques (see How-To Guides) to combine `git diff` output with specific file contents when needed.
*   **Consider Token Limits:** `llm_ctx` does not manage token limits. Be mindful of how much context you are generating, especially when using broad patterns like `**/*`.


## Testing

Run the test suite using Make:
```bash
make test
```

## License

See the [LICENSE](LICENSE) file for license rights and limitations.
