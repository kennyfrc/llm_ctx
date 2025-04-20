# llm_ctx

`llm_ctx` is a command-line utility that formats code or text from files and standard input (stdin) into structured context for LLMs (such as Claude.ai, ChatGPT, Gemini AI Studio). It supports Unix-style composition, allowing integration with tools like `grep`, `find`, and `git diff` via pipes (`|`). Key features include file tree generation, respecting `.gitignore` rules, and skipping binary files (see Limitations).

**Quick Examples:**

1.  **Review the last commit** and copy to clipboard (macOS):
    ```bash
    git diff HEAD~1 | llm_ctx -c "Review the changes in this commit" | pbcopy
    ```

2.  **Analyze specific React component files** in the `src` directory and copy (macOS):
    ```bash
    llm_ctx -f 'src/components/Button.{js,jsx,css}' -c "Analyze this React Button component and its styles." | pbcopy
    ```
    *(Note: `{}` expansion might depend on your shell)*

3.  **Provide context for the entire project** (respecting `.gitignore`) and copy (macOS):
    ```bash
    # Use a recursive glob. Assuming that .gitignore has node_modules/, build/, *.log etc., these will be skipped.
    llm_ctx -f '**/*' -c "Please conduct a code review of this project, and find any potential bugs." | pbcopy
    ```
    *(Warning: This can generate a lot of context!)*

*(Clipboard commands: Use `| xclip -selection clipboard` on Linux (X11) or `| clip` on Windows instead of `| pbcopy`)*

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
    Compile the source code using `make`. This creates the `llm_ctx` executable in the current directory.
    ```bash
    make
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

### Piping to Clipboard

To easily paste the context into an LLM web UI, pipe the output to your system's clipboard command:

*   **macOS:** `... | pbcopy`
*   **Linux (X11):** `... | xclip -selection clipboard`
*   **Windows:** `... | clip`

Example:
```bash
git diff HEAD~1 | llm_ctx -c "Review this commit" | pbcopy
```

## How-To Guides

This section provides goal-oriented steps for common tasks.

### How to Request PR-Style Review Comments

Use the `-e` (or `--editor-comments`) flag along with `-c` to instruct the LLM to format part of its response like a pull request review, with specific suggestions tied to the code.

```bash
# Analyze main.c and ask for review comments
llm_ctx -f main.c -c "Review this function for thread safety" -e | pbcopy
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
    # Type instructions directly, then pipe output to clipboard (macOS example)
    llm_ctx -f main.c -c @- | pbcopy
    # --> Enter instructions here...
    # --> Press Ctrl+D (output goes to clipboard)

    # Use a heredoc in a script or shell and pipe to clipboard
    ./llm_ctx -c @- -f src/utils.c <<'EOF' | pbcopy
    Please perform the following actions:
    1. Identify potential memory leaks.
    2. Suggest improvements for error handling.
    EOF
    # (Output goes to clipboard)
    ```
    *(See [Piping to Clipboard](#piping-to-clipboard) for Linux/Windows commands)*

All these methods add a `<user_instructions>` block to the output.

### How to Send Output to Clipboard

Pipe the output of `llm_ctx` to your system's clipboard utility:

*   **macOS:**
    ```bash
    git diff | llm_ctx | pbcopy
    ```
*   **Linux (X11):**
    ```bash
    git diff | llm_ctx | xclip -selection clipboard
    ```
*   **Windows:**
    ```bash
    git diff | llm_ctx | clip
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

  -e, --editor-comments
                 Instruct the LLM to append PR-style review comments to its
                 response. Adds specific instructions to the <response_guide>.

  -f [FILE...]   Process specified files or glob patterns instead of stdin.
                 Must be followed by one or more file paths or patterns.
                 Example: -f main.c 'src/**/*.js'

  -h, --help     Show this help message and exit.

  --no-gitignore Ignore .gitignore files. Process all files matched by
                 arguments or patterns, even if they are listed in .gitignore.
```

### Input Methods

`llm_ctx` determines its input source automatically:

1.  **File Arguments (`-f`):** If the `-f` flag is present, all subsequent arguments are treated as file paths or glob patterns to be processed.
2.  **Piped Content (stdin):** If `-f` is *not* present and stdin is *not* connected to a terminal (i.e., it's receiving piped data), `llm_ctx` reads the entire stdin stream as a single block of content (e.g., from `git diff`, `cat`). It attempts to detect the content type (like `diff`, `json`, `xml`) for appropriate fencing.
3.  **Terminal Input (Error/Help):** If `-f` is *not* present and stdin *is* connected to a terminal (i.e., you just run `llm_ctx` interactively), it prints the help message, as it expects input via pipes or the `-f` flag.

### Output Format

The output is structured using simple XML-like tags for clarity:

*   **`<user_instructions>` (Optional):** Contains the text provided via the `-c` flag. Appears first if present.
*   **`<response_guide>` (Optional):** Appears if `-c` was used. Contains guidance for the LLM on how to structure its response.
    *   **`<problem_statement>`:** Contains a fixed instruction for the LLM to summarize the user's request (found in `<user_instructions>`) in its own words. This ensures the LLM actively processes the request context.
    *   **`<reply_format>`:** Instructions for the LLM's reply structure. If the `-e` or `--editor-comments` flag was used, this section explicitly asks for PR-style code review comments (e.g., using GitHub inline diff syntax) in addition to the main solution/explanation. Otherwise, it indicates that no code review block is needed.
*   **`<file_tree>`:** Shows a tree structure representing the relative paths of the files included in the context. The root of the tree is the common parent directory.
*   **`<file_context>`:** Wraps the content of all processed files.
    *   **`File: <filepath>`:** A header indicating the start of a file's content. The `<filepath>` is relative to the common parent directory identified for the tree.
    *   **```` ```[type] ````:** Standard Markdown fenced code blocks containing the file content. `[type]` is automatically detected for stdin content (e.g., `diff`, `json`) if possible, otherwise it's empty.
    *   **`----------------------------------------`:** A separator line between files within the `<file_context>`.

**Example Structure (with `-c` and `-e`):**

```
<user_instructions>
Review this C code for potential memory leaks and suggest improvements.
</user_instructions>

<response_guide>
  <problem_statement>
Summarize the user's request or problem based on the <user_instructions> block and the provided context.
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

### File Selection and Filtering

Unlike some tools with explicit `--include` and `--exclude` flags (like `code2prompt`), `llm_ctx` uses a simpler approach:

*   **Inclusion:** Determined *only* by the file paths and glob patterns provided via the `-f` flag, or the content piped via stdin.
*   **Exclusion:** Determined *only* by `.gitignore` rules (unless disabled by `--no-gitignore`).

### Understanding the Output Structure

The XML-like tags (`<file_tree>`, `<file_context>`, etc.) and Markdown fences are chosen for:

*   **LLM Clarity:** Provides clear delimiters for different types of information (instructions, file structure, file content).
*   **Context Preservation:** The file tree helps the LLM understand the relationships between files.
*   **Robustness:** Less likely to be confused with code content compared to using only Markdown.
*   **Easy Parsing:** While designed for LLMs, the structure is simple enough for basic parsing if needed.
*   **Binary File Handling:** If a file is detected as binary (see Limitations), its content is replaced with a placeholder `[Binary file content skipped]` instead of being included within code fences.

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

MIT
