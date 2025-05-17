This file provides guidance when working with code in this repository.

## Pragmatic Programming Principles
**Implementation simplicity is our highest priority.** When analyzing or recommending code improvements, follow these principles:
- Minimize the number of possible execution paths. Reduce branching code that creates combinatorial explosions of codepaths. Design interfaces that provide consistent guarantees regardless of input state. Prefer single effective codepaths that produce reliable results over multiple specialized paths.
- Separate code clearly from data. Move program logic into data structures that can be modified without changing code. Let data drive control flow rather than hardcoding behavior. Create interfaces with multiple levels of access to provide both convenience and fine-grained control.
- Recognize upstream versus downstream systems. Design upstream systems (that manage state) differently from downstream systems (that produce effects). Push problems upstream where they can be solved once rather than patching downstream repeatedly. Improve your tools and data structures rather than adding complexity to consumer code.
- Normalize failure cases in your design. Treat errors as ordinary paths, not exceptional conditions. Make zero values valid and meaningful wherever possible. Use techniques like nil objects that guarantee valid reads even when representing absence. Design systems that gracefully handle imperfect inputs.
- Start small, concrete, and simple: solve necessary problems with an implementation that is fast, small, bug-free, and interoperable above all else. Sacrifice initial completeness, interface elegance, and consistency if needed for this implementation simplicity. Guarantee observable correctness for what is built. Resist premature abstraction: extract only minimal, justifiable patterns from multiple concrete examples if it genuinely simplifies without hindering implementation or obscuring necessary information; let patterns emerge. For downstream APIs (producing effects from state), favor 'immediate mode' designs deriving results functionally from inputs over 'retained mode' designs requiring callers to manage stateful object lifecycles; this simplifies usage code.

## Project Overview

`llm_ctx` is a command-line utility that formats code or text from files and standard input into structured context for LLMs. It supports Unix-style composition with tools like `grep`, `find`, and `git diff` via pipes. The tool respects `.gitignore` rules, generates file trees, and properly formats content with appropriate tags and fenced code blocks for LLMs.

## Code Structure

- **main.c**: Contains the main program logic, including:
  - Command-line argument parsing
  - File processing and content extraction
  - Input/output handling (stdin, temp files, clipboard)
  - Configuration file loading
  - File tree generation

- **gitignore.c/h**: Handles `.gitignore` pattern loading and matching
  - Pattern parsing
  - File path matching
  - Directory traversal

## Build Commands

```bash
# Build debug version (default)
make

# Build optimized release version
make release

# Run tests
make test

# Clean build artifacts
make clean

# Install to system (default: /usr/local/bin)
make install

# Create a symlink in PATH (recommended)
make symlink

# Clean, rebuild and run tests
make retest
```

## Test Commands

```bash
# Run all tests
make test

# Run specific test
./tests/test_gitignore
./tests/test_cli
./tests/test_stdin
./tests/test_config
```

The project uses a custom minimal test framework defined in `tests/test_framework.h` with simple assertions and test runners.

## Key Concepts

1. **Input Sources**:
   - Files specified with `-f` flag
   - Content piped from stdin
   - Glob patterns for file matching

2. **Output Format**:
   - `<user_instructions>` block from `-c` flag
   - `<system_instructions>` block from `-s` flag
   - `<response_guide>` block with LLM formatting instructions
   - `<file_tree>` showing directory structure
   - `<file_context>` containing all file contents with proper fencing

3. **Configuration**:
   - Uses `.llm_ctx.conf` for configuration
   - Supports copy-to-clipboard, editor comments, system prompts
   - Searches current dir upward, XDG config dirs, home dir

## Common Usage Patterns

- Process git diff: `git diff | llm_ctx -c "Review these changes"`
- Analyze specific files: `llm_ctx -f main.c gitignore.h -c "Explain these files"`
- Process recursive glob: `llm_ctx -f 'src/**/*.js' -c "Review JS files"`
- Pipe from stdin: `cat file.json | llm_ctx -c "Analyze this JSON"`
- Pipe to clipboard: `git diff | llm_ctx -c "Review changes" | pbcopy`

## Memory Management

The project uses careful memory management with explicit allocations/deallocations via `malloc`/`free` and includes guard code for detecting allocation failures. During development, keep the following in mind:

- Always check return values of memory allocations
- Use the existing `fatal` function to handle fatal errors
- Ensure resources are properly freed in the `cleanup` function
