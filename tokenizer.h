#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stddef.h>
#include <stdio.h>

/* Lazy-loaded tiktoken integration - tokens counted on demand */
struct Arena;

size_t llm_count_tokens(const char* text, const char* model);
int llm_tokenizer_available(void);
void llm_set_executable_dir(const char* dir);
void generate_token_diagnostics(const char* content, const char* model, FILE* out,
                                struct Arena* arena);

#endif /* TOKENIZER_H */