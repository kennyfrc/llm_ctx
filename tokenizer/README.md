# Tokenizer for llm_ctx

This directory contains the tokenizer implementation using tiktoken-c.

## Dependencies

- Rust ≥ 1.77
- cargo
- cmake
- python3

## Building

The tokenizer is built using:
```bash
make tokenizer
```

This will:
1. Build tiktoken-c from the submodule
2. Copy the resulting library and header files to this directory

## tiktoken-c source

The tiktoken-c library is included as a git submodule.
Repository: https://github.com/kojix2/tiktoken-c
Commit: Latest (use `git submodule update --init` to fetch)

## Files produced

After building:
- `libtiktoken.so` (Linux)
- `libtiktoken.dylib` (macOS)  
- `libtiktoken.dll` (Windows)
- `tiktoken.h` (C header)

## Usage

The tokenizer is dynamically loaded at runtime. If the library is not found,
token counting features will be gracefully disabled with a warning.