# llm_ctx Tests

This directory contains tests for the llm_ctx utility.

## Test Structure

- `test_framework.h`: A minimal test framework with simple assertions
- `test_gitignore.c`: Unit tests for the gitignore pattern matching functionality
- `test_cli.c`: Integration tests for the CLI behavior

## Running Tests

To run all tests:

```
make test
```

To run a specific test:

```
make tests/test_gitignore
./tests/test_gitignore
```

## Key Test Areas

1. **Gitignore Pattern Handling**:
   - Pattern parsing
   - Negation patterns
   - Directory-only patterns
   - Pattern precedence

2. **CLI Behavior**:
   - Default gitignore behavior
   - Using the `--no-gitignore` flag
   - File type filtering
   - Command integration
