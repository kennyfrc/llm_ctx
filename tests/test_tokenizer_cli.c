#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdbool.h>
#include "test_framework.h"

/* Helper to run llm_ctx and capture output */
int run_llm_ctx(const char *args, char *stdout_buf, size_t stdout_size) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx %s 2>&1", getenv("PWD"), args);
    
    
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

TEST(test_token_budget_exceeded) {
    /* Create a test file with content in current directory */
    FILE *test_file = fopen("test_long.txt", "w");
    ASSERT("Failed to create test file", test_file != NULL);
    
    /* Write some content that will exceed a small token budget */
    for (int i = 0; i < 100; i++) {
        fprintf(test_file, "This is a long line of text that will consume many tokens. ");
    }
    fclose(test_file);
    
    char stdout_buf[4096] = {0};
    
    /* Run with a very small token budget with full path and output to stdout */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "-o --no-gitignore -b 10 -f %s/test_long.txt", getenv("PWD"));
    int exit_code = run_llm_ctx(cmd, stdout_buf, sizeof(stdout_buf));
    
    /* When tokenizer is not available, the tool should succeed but warn */
    if (strstr(stdout_buf, "tokenizer library not found") != NULL) {
        /* Tokenizer not available - should succeed with warning */
        ASSERT("Should succeed when tokenizer not available", exit_code == 0);
        ASSERT("Should contain tokenizer warning", 
               strstr(stdout_buf, "tokenizer library not found") != NULL);
    } else {
        /* Tokenizer available - should exit with error for exceeded budget */
        ASSERT("Expected non-zero exit code for exceeded budget", exit_code != 0);
        ASSERT("Should contain token budget error message", 
               strstr(stdout_buf, "exceeds") != NULL || strstr(stdout_buf, "budget") != NULL);
    }
    
    /* Clean up */
    unlink("test_long.txt");
}

TEST(test_token_budget_within_limit) {
    /* Create a test file with content */
    FILE *test_file = fopen("test_short.txt", "w");
    ASSERT("Failed to create test file", test_file != NULL);
    
    /* Write minimal content */
    fprintf(test_file, "Hello world");
    fclose(test_file);
    
    char stdout_buf[4096] = {0};
    
    /* Run with a large token budget with full path and output to stdout */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "-o --no-gitignore -b 10000 -f %s/test_short.txt", getenv("PWD"));
    int exit_code = run_llm_ctx(cmd, stdout_buf, sizeof(stdout_buf));
    
    /* Should succeed */
    ASSERT("Expected exit code 0", exit_code == 0);
    
    /* Should contain the file content */
    ASSERT("Should contain file content", strstr(stdout_buf, "Hello world") != NULL);
    
    /* Should NOT contain error about token budget */
    ASSERT("Should not contain token budget error", strstr(stdout_buf, "exceeds") == NULL);
    
    /* Clean up */
    unlink("test_short.txt");
}

TEST(test_token_diagnostics_output) {
    /* Create test files */
    FILE *f1 = fopen("test1.txt", "w");
    fprintf(f1, "First file content");
    fclose(f1);
    
    FILE *f2 = fopen("test2.txt", "w");
    fprintf(f2, "Second file with more content here");
    fclose(f2);
    
    char stdout_buf[8192] = {0};
    
    /* Run with multiple files - diagnostics shown by default with full paths and output to stdout */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "-o --no-gitignore -f %s/test1.txt -f %s/test2.txt", getenv("PWD"), getenv("PWD"));
    int exit_code = run_llm_ctx(cmd, stdout_buf, sizeof(stdout_buf));
    
    ASSERT("Expected exit code 0", exit_code == 0);
    
    /* Check for output - token diagnostics only appear when tokenizer is available */
    ASSERT("Should contain test1.txt", strstr(stdout_buf, "test1.txt") != NULL);
    ASSERT("Should contain test2.txt", strstr(stdout_buf, "test2.txt") != NULL);
    
    /* Only check for token diagnostics if tokenizer is available */
    if (strstr(stdout_buf, "tokenizer library not found") == NULL) {
        /* Tokenizer available - check for diagnostic headers */
        ASSERT("Should contain Tokens header", strstr(stdout_buf, "Tokens") != NULL);
        ASSERT("Should contain File header for diagnostics", strstr(stdout_buf, "File") != NULL);
        ASSERT("Should contain Total", strstr(stdout_buf, "Total") != NULL);
    } else {
        /* Tokenizer not available - just check files were processed */
        /* Files are shown with full paths, so just check for the filename part */
        ASSERT("Should contain test1.txt in file header", strstr(stdout_buf, "/test1.txt") != NULL);
        ASSERT("Should contain test2.txt in file header", strstr(stdout_buf, "/test2.txt") != NULL);
    }
    
    /* Clean up */
    unlink("test1.txt");
    unlink("test2.txt");
}

TEST(test_token_model_option) {
    /* Create a test file */
    FILE *test_file = fopen("test_model.txt", "w");
    fprintf(test_file, "Test content for model");
    fclose(test_file);
    
    char stdout_buf[4096] = {0};
    
    /* Run with explicit model with full path and output to stdout */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "-o --no-gitignore --token-model gpt-3.5-turbo -f %s/test_model.txt", getenv("PWD"));
    int exit_code = run_llm_ctx(cmd, stdout_buf, sizeof(stdout_buf));
    
    ASSERT("Expected exit code 0", exit_code == 0);
    
    /* Clean up */
    unlink("test_model.txt");
}

TEST(test_missing_tokenizer_library) {
    /* This test checks behavior when tokenizer library is not available */
    /* The actual behavior depends on whether the library is built */
    
    /* Create a minimal test file */
    FILE *test_file = fopen("test_minimal.txt", "w");
    fprintf(test_file, "Hi");
    fclose(test_file);
    
    char stdout_buf[4096] = {0};
    
    /* Run the command with full path and output to stdout */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "-o --no-gitignore -f %s/test_minimal.txt", getenv("PWD"));
    int exit_code = run_llm_ctx(cmd, stdout_buf, sizeof(stdout_buf));
    
    /* Should still work (exit code 0) even without tokenizer */
    ASSERT("Should exit successfully", exit_code == 0);
    
    /* Check if tokenizer warning appears (only if library missing) */
    bool has_warning = strstr(stdout_buf, "Warning: Tokenizer library not available") != NULL;
    printf("Tokenizer warning present: %s\n", has_warning ? "yes" : "no");
    
    /* Clean up */
    unlink("test_minimal.txt");
}

int main(void) {
    printf("Running tokenizer CLI integration tests\n");
    printf("=======================================\n");
    
    /* Check if llm_ctx executable exists */
    char llm_ctx_path[1024];
    snprintf(llm_ctx_path, sizeof(llm_ctx_path), "%s/llm_ctx", getenv("PWD"));
    if (access(llm_ctx_path, X_OK) != 0) {
        fprintf(stderr, "Error: %s not found or not executable\n", llm_ctx_path);
        fprintf(stderr, "Make sure the llm_ctx executable is built in the project directory\n");
        return 1;
    }
    
    RUN_TEST(test_token_budget_exceeded);
    RUN_TEST(test_token_budget_within_limit);
    RUN_TEST(test_token_diagnostics_output);
    RUN_TEST(test_token_model_option);
    RUN_TEST(test_missing_tokenizer_library);
    
    printf("\n");
    PRINT_TEST_SUMMARY();
}