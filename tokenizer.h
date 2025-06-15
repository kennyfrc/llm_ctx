#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stddef.h>
#include <stdio.h>

/* Forward declaration */
struct Arena;

/* Returns number of tokens for UTF-8 text under a given OpenAI model.
   Returns SIZE_MAX on error (library missing, bad model, or other failure).
   
   This function will lazily load the tiktoken library on first call.
   If the library cannot be loaded, all subsequent calls will return SIZE_MAX.
   
   Supported models include:
   - "gpt-4o"
   - "gpt-4"
   - "gpt-3.5-turbo"
   - "text-davinci-003"
   - etc.
*/
size_t llm_count_tokens(const char *text, const char *model);

/* Internal: check if tokenizer library is available.
   Returns 1 if available, 0 if not.
   This is mainly for testing purposes.
*/
int llm_tokenizer_available(void);

/* Set the directory where the executable is located.
   This helps the tokenizer find the library when the binary is symlinked.
   Should be called early in program initialization.
*/
void llm_set_executable_dir(const char *dir);

/* Generate token count diagnostics showing per-file breakdown.
   Parses the content to find file sections and counts tokens for each.
   Outputs a formatted table to the provided FILE stream.
*/
void generate_token_diagnostics(const char *content, const char *model, FILE *out, struct Arena *arena);

#endif /* TOKENIZER_H */