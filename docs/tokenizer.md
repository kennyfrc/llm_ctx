# Tokenizer Documentation

This document describes the tokenizer feature in `llm_ctx`, which provides accurate token counting using OpenAI's tokenization rules.

## Overview

The tokenizer feature helps manage LLM context window limits by:
- Counting tokens before sending to an LLM
- Enforcing hard token budget limits
- Providing detailed per-file token diagnostics
- Supporting different OpenAI model tokenization rules

## Building the Tokenizer

### Prerequisites

- Rust ≥ 1.77
- cargo (Rust package manager)
- cmake
- python3

### Build Process

```bash
# Clone the repository with submodules
git clone https://github.com/kennyfrc/llm_ctx.git
cd llm_ctx
git submodule update --init

# Build the tokenizer library
make tokenizer

# This creates:
# - tokenizer/libtiktoken.so (Linux)
# - tokenizer/libtiktoken.dylib (macOS)
# - tokenizer/libtiktoken.dll (Windows)
```

The tokenizer is optional - `llm_ctx` works without it, but token features will be disabled.

## Usage

### Token Budget (`-b`, `--token-budget`)

Set a hard limit on context size:

```bash
# Basic usage
llm_ctx -f file.txt -b 4000

# With multiple files
llm_ctx -f 'src/**/*.js' -b 128000

# Exit codes:
# 0 = Success (within budget)
# 3 = Token budget exceeded
```

### Token Diagnostics (`-D`, `--token-diagnostics`)

Get detailed token counts:

```bash
# Output to stderr
llm_ctx -f src/*.c -D

# Output to file
llm_ctx -f src/*.c -Dtoken_report.txt

# Example output:
  Tokens   File
  -------  ------------------------
     842   src/main.c
      56   src/utils.c
   1,234   <user_instructions>
  -------  ------------------------
   2,132   Total
```

### Model Selection (`--token-model`)

Different models use different tokenization:

```bash
# Default: gpt-4o
llm_ctx -f file.txt -D

# Use GPT-3.5 tokenization
llm_ctx -f file.txt --token-model=gpt-3.5-turbo -D

# Supported models:
# - gpt-4o (default)
# - gpt-4
# - gpt-3.5-turbo
# - text-davinci-003
# - claude-3 (uses similar tokenization)
```

## Examples

### Ensuring Context Fits

```bash
# Check if files fit in GPT-4's context
llm_ctx -f 'docs/**/*.md' -b 128000 -c "Summarize these docs"

# If too large, use diagnostics to identify big files
llm_ctx -f 'docs/**/*.md' -D | sort -nr | head -10

# Then exclude large files
llm_ctx -f 'docs/**/*.md' -f '!docs/api-reference.md' -b 128000
```

### Optimizing Instructions

```bash
# See how many tokens your prompt uses
echo "Your long instructions here..." | llm_ctx -C -D

# Compare different prompts
echo "Be concise" | llm_ctx -C -D
echo "Please be as concise as possible in your response" | llm_ctx -C -D
```

### CI/CD Integration

```bash
#!/bin/bash
# Ensure PR context fits in model window
if ! llm_ctx -f $(git diff --name-only HEAD~1) -b 100000 -o >/dev/null; then
    echo "Error: PR changes exceed token limit"
    exit 1
fi
```

## Environment Variables

- `LLMCTX_TOKEN_MODEL`: Default model for token counting (overrides built-in default)
- `LD_LIBRARY_PATH` (Linux): Additional paths to search for libtiktoken.so
- `DYLD_LIBRARY_PATH` (macOS): Additional paths to search for libtiktoken.dylib

Example:
```bash
export LLMCTX_TOKEN_MODEL=gpt-3.5-turbo
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
```

## Troubleshooting

### Tokenizer not found

If you see "warning: tokenizer library not found":

1. Check the library exists:
   ```bash
   ls tokenizer/libtiktoken.*
   ```

2. Rebuild if missing:
   ```bash
   make clean-tokenizer
   make tokenizer
   ```

3. Check Rust is installed:
   ```bash
   rustc --version
   cargo --version
   ```

### Wrong token counts

If token counts seem incorrect:

1. Verify the model:
   ```bash
   llm_ctx -f test.txt --token-model=gpt-4o -D
   ```

2. Compare with OpenAI's tokenizer:
   - Visit: https://platform.openai.com/tokenizer
   - Paste the same text
   - Select the same model

### Build failures

Common issues:

1. **Missing cargo**: Install Rust from https://rustup.rs/
2. **cmake not found**: Install cmake for your platform
3. **Submodule issues**: Run `git submodule update --init --recursive`
4. **Architecture mismatch on macOS**: 
   - If you see "incompatible architecture" errors, the tokenizer and llm_ctx were built for different architectures
   - Running under Rosetta 2? Build tokenizer for x86_64: `cd tokenizer/tiktoken-c && cargo build --release --target x86_64-apple-darwin`
   - Native M1? Ensure both use ARM64: check with `file llm_ctx tokenizer/libtiktoken_c.dylib`

## Technical Details

### Implementation

- Uses `tiktoken-c`, a C wrapper around OpenAI's official Rust tokenizer
- Dynamically loads the library at runtime using dlopen/dlsym
- Gracefully degrades if library is missing
- Thread-safe after initial load

### Performance

- First call: ~50ms (library loading)
- Subsequent calls: <1ms per KB of text
- Memory usage: Minimal (tokenizer caches are shared)

### Accuracy

- Exact token counts matching OpenAI's API
- Includes special tokens for each model
- Handles all Unicode correctly
- Matches chat format tokens (like `<|im_start|>`)

## Source Code

The tokenizer implementation consists of:

- `tokenizer.h/c` - Dynamic library loading and API wrapper
- `tokenizer_diagnostics.c` - Per-file token analysis
- `tokenizer/tiktoken-c/` - Submodule with Rust tokenizer
- `tests/test_tokenizer.c` - Unit tests
- `tests/test_tokenizer_cli.c` - Integration tests