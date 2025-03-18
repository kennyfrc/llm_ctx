# llm_ctx

Command-line utility for extracting file content with structure for LLM context.

```bash
# Simple usage: Analyze a single file
llm_ctx -f main.c -c "Explain this file"

# Review code changes with git diff
git diff | llm_ctx -c "Explain these changes"

# Send output directly to clipboard then paste into your LLM's Web UI (claude.ai, chatgpt.com, etc.)
# macOS: 
llm_ctx -f main.c -c "Explain this file" | pbcopy
# Linux:
llm_ctx -f main.c -c "Explain this file" | xclip -selection clipboard
# Windows:
llm_ctx -f main.c -c "Explain this file" | clip

# Process files found with specific criteria and paste into your LLM web UI
find src -name "*.js" -mtime -7 | xargs llm_ctx -f -c "Review recent JS changes" > for_llm.txt
```

## Overview

`llm_ctx` formats content for LLM analysis, whether from stdin or files. It respects `.gitignore` rules, generates file trees, and outputs structured content for LLM interfaces.

## Features

- By default, reads content from stdin (git diff, cat, etc.)
- Formats files with fenced code blocks using `-f` flag
- Respects `.gitignore` rules
- Generates file trees
- Supports glob patterns
- Works with Unix pipes (process git diff, pass to pbcopy, etc.)

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

# Create symlink
make symlink

# Custom location
make symlink PREFIX=~/bin
```

## Basic Usage

```bash
# Process content from stdin (default behavior)
git diff | llm_ctx

# Process content from a file via stdin
cat complex_file.json | llm_ctx -c "Explain this JSON structure"

# Process specific files (using -f flag)
llm_ctx -f src/main.c include/header.h

# Process files from find command
find src -name "*.c" | xargs llm_ctx -f

# Add instructions for the LLM
llm_ctx -c "Please explain this code" -f src/*.c

# Pipe to clipboard
git diff | llm_ctx -c "Review these changes" | pbcopy

# Combine multiple sources (git diff and files) in one command
{ git diff; find . -name "*.c" | xargs cat; } | llm_ctx -c "Review both changes and code" | pbcopy
```

## Output Format

```
<user_instructions>
// when `-c` is used, instructions show up here
</user_instructions>

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
# Process git changes (default behavior)
git diff | llm_ctx -c "Review these changes"

# Process specific JSON file via stdin
cat package.json | llm_ctx -c "Explain this package config"

# Select specific files (with -f flag)
llm_ctx -f src/main.c include/important.h

# Use find to locate and process files
find . -name "*.c" | xargs grep -l "process_data" | xargs llm_ctx -f

# Bypass gitignore rules with files
llm_ctx -f --no-gitignore build/config.js

# Copy to clipboard (macOS)
git diff | llm_ctx -c "Review these changes" | pbcopy

# Combine git changes and files in a single command
{ git diff; find . -name "*.c" | xargs cat; } | llm_ctx -c "Review both changes and code" | pbcopy
```

## Context Tips

- Select relevant files only
- Use `-c` flag for instructions
- Send output to clipboard or save to a file, then paste into your LLM's web interface:
  - macOS: `... | pbcopy` then paste into Claude.ai, ChatGPT, etc.
  - Linux: `... | xclip -selection clipboard` then paste
  - Windows: `... | clip` then paste
  - Or redirect to a file: `... > context.txt` then upload or copy/paste

## Reference

### Command-line Options

```
Usage: llm_ctx [OPTIONS] [FILE...]
Format files for LLM code analysis with appropriate tags.

Options:
  -c TEXT        Add user instructions wrapped in <user_instructions> tags
  -f [FILE...]   Process files instead of stdin content
  -h             Show this help message
  --no-gitignore Ignore .gitignore files when collecting files

By default, llm_ctx reads content from stdin.
Use -f flag to indicate file arguments are provided.
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
