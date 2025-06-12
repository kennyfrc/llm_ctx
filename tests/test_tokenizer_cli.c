#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "test_framework.h"

#define LLM_CTX_PATH "./llm_ctx"

/* Helper to run llm_ctx and capture output */
int run_llm_ctx(const char *args, char *stdout_buf, size_t stdout_size, 
                char *stderr_buf, size_t stderr_size) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s %s 2>&1", LLM_CTX_PATH, args);
    
    FILE *pipe = popen(cmd, "r");
    if (!pipe) return -1;
    
    /* Read combined output */
    size_t total = 0;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) && total < stdout_size - 1) {
        size_t len = strlen(buffer);
        if (total + len < stdout_size - 1) {
            strcat(stdout_buf, buffer);
            total += len;
        }
    }
    
    int status = pclose(pipe);
    return WEXITSTATUS(status);
}

void test_token_budget_exceeded() {
    TEST_START("test_token_budget_exceeded");
    
    /* Create a test file with content */
    FILE *test_file = fopen("test_long.txt", "w");
    TEST_ASSERT(test_file != NULL, "Failed to create test file");
    
    /* Write some content that will exceed a small token budget */
    for (int i = 0; i < 100; i++) {
        fprintf(test_file, "This is a long line of text that will consume many tokens. ");
    }
    fclose(test_file);
    
    char stdout_buf[4096] = {0};
    char stderr_buf[4096] = {0};
    
    /* Run with a very small token budget */
    int exit_code = run_llm_ctx("-f test_long.txt --token-budget=10 -o",
                               stdout_buf, sizeof(stdout_buf),
                               stderr_buf, sizeof(stderr_buf));
    
    /* Should exit with code 3 for budget exceeded */
    TEST_ASSERT(exit_code == 3, "Expected exit code 3 for budget exceeded, got %d", exit_code);
    
    /* Check for error message */
    TEST_ASSERT(strstr(stdout_buf, "error: context uses") != NULL ||
                strstr(stdout_buf, "budget") != NULL,
                "Expected budget exceeded error message");
    
    unlink("test_long.txt");
    TEST_PASS();
}

void test_token_budget_within_limit() {
    TEST_START("test_token_budget_within_limit");
    
    /* Create a small test file */
    FILE *test_file = fopen("test_small.txt", "w");
    TEST_ASSERT(test_file != NULL, "Failed to create test file");
    fprintf(test_file, "Hello world");
    fclose(test_file);
    
    char stdout_buf[4096] = {0};
    char stderr_buf[4096] = {0};
    
    /* Run with a large token budget */
    int exit_code = run_llm_ctx("-f test_small.txt --token-budget=10000 -o",
                               stdout_buf, sizeof(stdout_buf),
                               stderr_buf, sizeof(stderr_buf));
    
    /* Should succeed */
    TEST_ASSERT(exit_code == 0, "Expected exit code 0, got %d", exit_code);
    
    /* Check for within budget message */
    TEST_ASSERT(strstr(stdout_buf, "within limit") != NULL ||
                strstr(stdout_buf, "Hello world") != NULL,
                "Expected success output");
    
    unlink("test_small.txt");
    TEST_PASS();
}

void test_token_diagnostics_output() {
    TEST_START("test_token_diagnostics_output");
    
    /* Create test files */
    FILE *f1 = fopen("test1.c", "w");
    fprintf(f1, "int main() { return 0; }");
    fclose(f1);
    
    FILE *f2 = fopen("test2.c", "w");
    fprintf(f2, "void helper() { printf(\"hello\"); }");
    fclose(f2);
    
    char stdout_buf[4096] = {0};
    char stderr_buf[4096] = {0};
    
    /* Run with diagnostics to stderr */
    int exit_code = run_llm_ctx("-f test1.c test2.c -D -o",
                               stdout_buf, sizeof(stdout_buf),
                               stderr_buf, sizeof(stderr_buf));
    
    /* Should succeed */
    TEST_ASSERT(exit_code == 0, "Expected exit code 0, got %d", exit_code);
    
    /* Check for diagnostics table */
    TEST_ASSERT(strstr(stdout_buf, "Tokens") != NULL ||
                strstr(stdout_buf, "File") != NULL ||
                strstr(stdout_buf, "Total") != NULL,
                "Expected diagnostics table in output");
    
    unlink("test1.c");
    unlink("test2.c");
    TEST_PASS();
}

void test_token_model_option() {
    TEST_START("test_token_model_option");
    
    /* Create a test file */
    FILE *test_file = fopen("test_model.txt", "w");
    fprintf(test_file, "Test content");
    fclose(test_file);
    
    char stdout_buf[4096] = {0};
    char stderr_buf[4096] = {0};
    
    /* Run with different model */
    int exit_code = run_llm_ctx("-f test_model.txt --token-model=gpt-3.5-turbo -D -o",
                               stdout_buf, sizeof(stdout_buf),
                               stderr_buf, sizeof(stderr_buf));
    
    /* Should succeed (even if tokenizer not available) */
    TEST_ASSERT(exit_code == 0, "Expected exit code 0, got %d", exit_code);
    
    unlink("test_model.txt");
    TEST_PASS();
}

void test_missing_tokenizer_library() {
    TEST_START("test_missing_tokenizer_library");
    
    /* Temporarily rename the tokenizer library if it exists */
    int renamed = 0;
    if (access("tokenizer/libtiktoken.so", F_OK) == 0) {
        rename("tokenizer/libtiktoken.so", "tokenizer/libtiktoken.so.bak");
        renamed = 1;
    } else if (access("tokenizer/libtiktoken.dylib", F_OK) == 0) {
        rename("tokenizer/libtiktoken.dylib", "tokenizer/libtiktoken.dylib.bak");
        renamed = 1;
    }
    
    /* Create a test file */
    FILE *test_file = fopen("test_missing.txt", "w");
    fprintf(test_file, "Test content");
    fclose(test_file);
    
    char stdout_buf[4096] = {0};
    char stderr_buf[4096] = {0};
    
    /* Run with token budget when library missing */
    int exit_code = run_llm_ctx("-f test_missing.txt --token-budget=1000 -o",
                               stdout_buf, sizeof(stdout_buf),
                               stderr_buf, sizeof(stderr_buf));
    
    /* Should succeed but warn */
    TEST_ASSERT(exit_code == 0, "Should succeed even without tokenizer");
    TEST_ASSERT(strstr(stdout_buf, "warning: token counting unavailable") != NULL ||
                strstr(stdout_buf, "Test content") != NULL,
                "Expected warning or content output");
    
    /* Restore library */
    if (renamed) {
        if (access("tokenizer/libtiktoken.so.bak", F_OK) == 0) {
            rename("tokenizer/libtiktoken.so.bak", "tokenizer/libtiktoken.so");
        } else if (access("tokenizer/libtiktoken.dylib.bak", F_OK) == 0) {
            rename("tokenizer/libtiktoken.dylib.bak", "tokenizer/libtiktoken.dylib");
        }
    }
    
    unlink("test_missing.txt");
    TEST_PASS();
}

int main() {
    printf("=== Tokenizer CLI Integration Tests ===\n\n");
    
    /* Check if llm_ctx exists */
    if (access(LLM_CTX_PATH, X_OK) != 0) {
        printf("Error: %s not found or not executable\n", LLM_CTX_PATH);
        printf("Please build llm_ctx first with 'make'\n");
        return 1;
    }
    
    test_token_budget_exceeded();
    test_token_budget_within_limit();
    test_token_diagnostics_output();
    test_token_model_option();
    test_missing_tokenizer_library();
    
    printf("\nAll tokenizer CLI tests completed.\n");
    return 0;
}