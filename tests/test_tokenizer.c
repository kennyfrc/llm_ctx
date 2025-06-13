#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include "../tokenizer.h"
#include "../arena.h"
#include "test_framework.h"

TEST(test_tokenizer_availability) {
    /* Test might pass or fail depending on whether library is built */
    int available = llm_tokenizer_available();
    printf("Tokenizer available: %s\n", available ? "yes" : "no");
}

TEST(test_token_counting) {
    /* Skip test if tokenizer not available */
    if (!llm_tokenizer_available()) {
        printf("Skipping: tokenizer library not available\n");
        return;
    }
    
    /* Test known token counts from OpenAI documentation */
    struct {
        const char *text;
        const char *model;
        size_t expected_min;  /* Minimum expected tokens */
        size_t expected_max;  /* Maximum expected tokens */
    } test_cases[] = {
        {"hello world", "gpt-4o", 2, 3},
        {"Hello, world!", "gpt-4o", 3, 4},
        {"The quick brown fox jumps over the lazy dog", "gpt-4o", 9, 10},
        {"", "gpt-4o", 0, 0},
        {NULL, NULL, 0, 0}
    };
    
    for (int i = 0; test_cases[i].text != NULL; i++) {
        size_t tokens = llm_count_tokens(test_cases[i].text, test_cases[i].model);
        if (tokens == SIZE_MAX) {
            printf("FAIL: Token counting failed for: '%s'\n", test_cases[i].text);
            ASSERT("Token counting should not fail", 0);
        }
        
        if (tokens < test_cases[i].expected_min || tokens > test_cases[i].expected_max) {
            printf("FAIL: Token count %zu outside range [%zu, %zu] for: '%s'\n",
                    tokens, test_cases[i].expected_min, test_cases[i].expected_max, test_cases[i].text);
            ASSERT("Token count out of range", 0);
        }
        
        printf("OK: '%s' -> %zu tokens\n", test_cases[i].text, tokens);
    }
}

TEST(test_invalid_inputs) {
    /* Test NULL inputs */
    size_t tokens = llm_count_tokens(NULL, "gpt-4o");
    ASSERT("NULL text should return SIZE_MAX", tokens == SIZE_MAX);
    
    tokens = llm_count_tokens("hello", NULL);
    ASSERT("NULL model should return SIZE_MAX", tokens == SIZE_MAX);
    
    /* Test invalid model (if tokenizer available) */
    if (llm_tokenizer_available()) {
        tokens = llm_count_tokens("hello", "invalid-model-xyz");
        /* This might or might not fail depending on tiktoken implementation */
        printf("Invalid model test: %s\n", 
               tokens == SIZE_MAX ? "rejected as expected" : "accepted (implementation dependent)");
    }
}

TEST(test_token_diagnostics) {
    /* Skip test if tokenizer not available */
    if (!llm_tokenizer_available()) {
        printf("Skipping: tokenizer library not available\n");
        return;
    }
    
    Arena arena = arena_create(1024 * 1024); /* 1MB */
    
    /* Test content with file sections */
    const char *test_content = 
        "<user_instructions>\n"
        "Please review this code\n"
        "</user_instructions>\n"
        "\n"
        "File: src/main.c\n"
        "```c\n"
        "int main() {\n"
        "    return 0;\n"
        "}\n"
        "```\n"
        "\n"
        "File: src/utils.h\n"
        "```c\n"
        "#ifndef UTILS_H\n"
        "#define UTILS_H\n"
        "void helper();\n"
        "#endif\n"
        "```\n";
    
    /* Create temp file for diagnostics output */
    char temp_path[] = "/tmp/test_diag_XXXXXX";
    int fd = mkstemp(temp_path);
    ASSERT("Failed to create temp file", fd != -1);
    
    FILE *diag_file = fdopen(fd, "w+");
    ASSERT("Failed to open temp file", diag_file != NULL);
    
    /* Generate diagnostics */
    generate_token_diagnostics(test_content, "gpt-4o", diag_file, &arena);
    
    /* Read back the output */
    fseek(diag_file, 0, SEEK_SET);
    char line[256];
    int line_count = 0;
    bool found_header = false;
    bool found_total = false;
    
    while (fgets(line, sizeof(line), diag_file)) {
        line_count++;
        if (strstr(line, "Tokens   File")) found_header = true;
        if (strstr(line, "Total")) found_total = true;
        printf("  %s", line); /* Show output for debugging */
    }
    
    ASSERT("Diagnostics should have header", found_header);
    ASSERT("Diagnostics should have total", found_total);
    ASSERT("Diagnostics should have multiple lines", line_count >= 5);
    
    fclose(diag_file);
    unlink(temp_path);
    arena_destroy(&arena);
}

int main(void) {
    printf("=== Tokenizer Tests ===\n\n");
    
    RUN_TEST(test_tokenizer_availability);
    RUN_TEST(test_token_counting);
    RUN_TEST(test_invalid_inputs);
    RUN_TEST(test_token_diagnostics);
    
    printf("\n");
    PRINT_TEST_SUMMARY();
}