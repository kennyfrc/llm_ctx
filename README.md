# llm_ctx

Command-line utility for extracting file content with structure for LLM context.

```bash
find src -name "*.c" | llm_ctx -c "Explain this code" | pbcopy
```

## Overview

`llm_ctx` formats source code files with tags and structure for LLM analysis. It respects `.gitignore` rules, generates file trees, and outputs structured content for LLM interfaces.

## Features

- Formats files with fenced code blocks
- Respects `.gitignore` rules 
- Generates file trees
- Supports glob patterns
- Works with Unix pipes (ingest from `find`, pass it to `pbcopy`, etc.)

## Installation

### Prerequisites

- C compiler (gcc/clang)
- make
- POSIX-compliant system

### Compatibility

- macOS (M1/Apple Silicon) - Fully tested
- Linux - Should work, not fully tested
- Windows (WSL) - Should work under WSL, not fully tested

### Building from Source

```bash
git clone https://github.com/kennyfrc/llm_ctx.git
cd llm_ctx
make
```

### Installing

```bash
# Install to system
sudo make install

# Create symlink (for development)
make symlink

# Custom location
make symlink PREFIX=~/bin
```

## Basic Usage

```bash
# Process specific files
llm_ctx src/main.c include/header.h

# Process files from stdin
find src -name "*.c" | llm_ctx

# Add instructions for the LLM
llm_ctx -c "Please explain this code" src/*.c

# Pipe to clipboard
llm_ctx src/*.c | pbcopy
```

## Output Format

```
<file_tree>
.
├── src
│   └── main.c
└── include
    └── header.h
</file_tree>

<file_context>

File: src/main.c

// Your code here

----------------------------------------

File: include/header.h

// Your header file content

----------------------------------------

</file_context>
```

## Examples

```bash
# Select specific files
llm_ctx src/main.c include/important.h

# Use find to locate files
find . -name "*.c" | xargs grep -l "process_data" | llm_ctx

# Bypass gitignore rules
llm_ctx --no-gitignore build/config.js

# Copy to clipboard (macOS)
llm_ctx src/*.c | pbcopy

# Process git changes
git diff | llm_ctx -c "Review these changes"

# Shell alias
alias llm='llm_ctx | pbcopy'
```

## Context Tips

- Select relevant files only
- Use `-c` flag for instructions
- Process output with other tools as needed (like `find`, `pbcopy`, etc.)

## Reference

### Command-line Options

```
Usage: llm_ctx [OPTIONS] [FILE...]
Format files for LLM code analysis with appropriate tags.

Options:
  -c TEXT        Add user instructions wrapped in <user_instructions> tags
  -h             Show this help message
  --no-gitignore Ignore .gitignore files when collecting files

If no FILE arguments are given, read from standard input.
This allows piping from commands like find, ls, etc.
```

### Output Format

XML-like structure with:
- `<file_tree>` - File structure visualization
- `<file_context>` - File contents
- `<user_instructions>` - Instructions (when provided)

### .gitignore Patterns

Supports:
- Standard patterns (`*.log`)
- Negation patterns (`!important.txt`)
- Directory patterns (`build/`)

## Testing

```bash
make test
```

## License

MIT
