#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stddef.h>
#include <stdio.h>

/* Forward declaration */
struct Arena;

// Count tokens using tiktoken library (lazy-loaded)
size_t llm_count_tokens(const char *text, const char *model);

// Check if tokenizer library is available
int llm_tokenizer_available(void);

// Set executable directory for library discovery
void llm_set_executable_dir(const char *dir);

// Generate token breakdown by file and section
void generate_token_diagnostics(const char *content, const char *model, FILE *out, struct Arena *arena);

#endif /* TOKENIZER_H */