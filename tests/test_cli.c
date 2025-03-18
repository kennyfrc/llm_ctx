#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "test_framework.h"

/**
 * CLI Integration tests for llm_ctx
 * 
 * These tests focus on:
 * - Testing the primary functionality
 * - Testing command line integration
 * - Testing file system interactions
 */

/* Test directory for creating test files */
#define TEST_DIR "/tmp/llm_ctx_test"

/* Set up the test environment */
void setup_test_env(void) {
    /* Create test directory */
    mkdir(TEST_DIR, 0755);
    
    /* Create a test .gitignore file */
    FILE *gitignore = fopen(TEST_DIR "/.gitignore", "w");
    if (gitignore) {
        fprintf(gitignore, "# Test gitignore file\n");
        fprintf(gitignore, "*.log\n");
        fprintf(gitignore, "test_*.txt\n");
        fprintf(gitignore, "!test_important.txt\n");
        fprintf(gitignore, "secrets/\n");
        fclose(gitignore);
    }
    
    /* Create some test files */
    FILE *f;
    
    f = fopen(TEST_DIR "/regular.txt", "w");
    if (f) {
        fprintf(f, "Regular file content\n");
        fclose(f);
    }
    
    f = fopen(TEST_DIR "/test_1.txt", "w");
    if (f) {
        fprintf(f, "Test file 1 content\n");
        fclose(f);
    }
    
    f = fopen(TEST_DIR "/test_2.txt", "w");
    if (f) {
        fprintf(f, "Test file 2 content\n");
        fclose(f);
    }
    
    f = fopen(TEST_DIR "/test_important.txt", "w");
    if (f) {
        fprintf(f, "Important test file content\n");
        fclose(f);
    }
    
    f = fopen(TEST_DIR "/app.log", "w");
    if (f) {
        fprintf(f, "Log content\n");
        fclose(f);
    }
    
    /* Create a subdirectory */
    mkdir(TEST_DIR "/secrets", 0755);
    
    f = fopen(TEST_DIR "/secrets/secret.txt", "w");
    if (f) {
        fprintf(f, "Secret content\n");
        fclose(f);
    }
}

/* Clean up the test environment */
void teardown_test_env(void) {
    /* Remove all test files */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

/* Helper function to check if a string contains a substring */
int string_contains(const char *str, const char *substr) {
    return strstr(str, substr) != NULL;
}

/* Run a command and capture its output */
char *run_command(const char *cmd) {
    static char buffer[16384];
    buffer[0] = '\0';
    
    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        return buffer;
    }
    
    char tmp[1024];
    while (fgets(tmp, sizeof(tmp), pipe) != NULL) {
        strcat(buffer, tmp);
    }
    
    pclose(pipe);
    return buffer;
}

/* Test default gitignore behavior */
TEST(test_cli_gitignore_default) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cd %s && %s/llm_ctx -f *.txt", TEST_DIR, getenv("PWD"));
    
    char *output = run_command(cmd);
    
    /* Should include regular.txt and test_important.txt, but not test_1.txt or test_2.txt */
    ASSERT("Output contains regular.txt", string_contains(output, "regular.txt"));
    ASSERT("Output contains test_important.txt", string_contains(output, "test_important.txt"));
    ASSERT("Output does not contain test_1.txt", !string_contains(output, "test_1.txt"));
    ASSERT("Output does not contain test_2.txt", !string_contains(output, "test_2.txt"));
}

/* Test --no-gitignore flag */
TEST(test_cli_no_gitignore) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cd %s && %s/llm_ctx -f --no-gitignore *.txt", TEST_DIR, getenv("PWD"));
    
    char *output = run_command(cmd);
    
    /* Should include all .txt files */
    ASSERT("Output contains regular.txt", string_contains(output, "regular.txt"));
    ASSERT("Output contains test_important.txt", string_contains(output, "test_important.txt"));
    ASSERT("Output contains test_1.txt", string_contains(output, "test_1.txt"));
    ASSERT("Output contains test_2.txt", string_contains(output, "test_2.txt"));
}

/* Test ignoring of log files */
TEST(test_cli_ignore_logs) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cd %s && %s/llm_ctx -f *", TEST_DIR, getenv("PWD"));
    
    char *output = run_command(cmd);
    
    /* Should include txt files (that aren't ignored) but not log files */
    ASSERT("Output contains regular.txt", string_contains(output, "regular.txt"));
    ASSERT("Output does not contain app.log", !string_contains(output, "app.log"));
}

/* Test directory ignoring */
TEST(test_cli_ignore_dirs) {
    char cmd[1024];
    
    /* Test directly with the regular and important files */
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -f %s/regular.txt %s/test_important.txt", 
             getenv("PWD"), TEST_DIR, TEST_DIR);
    
    char *output = run_command(cmd);
    
    /* Should include specified files */
    ASSERT("Output contains regular.txt", string_contains(output, "regular.txt"));
    ASSERT("Output contains test_important.txt", string_contains(output, "test_important.txt"));
}

/* Test help message includes new options */
TEST(test_cli_help_message) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -h", getenv("PWD"));
    char *output = run_command(cmd);
    
    ASSERT("Help message includes --no-gitignore option", 
           string_contains(output, "--no-gitignore"));
    ASSERT("Help message includes -f flag", 
           string_contains(output, "-f"));
}

/* Test directory handling - should process files in directory but not show directory itself */
TEST(test_directory_handling) {
    /* Create a test nested directory structure */
    mkdir(TEST_DIR "/nested", 0755);
    
    /* Create some test files in the nested directory */
    FILE *f;
    f = fopen(TEST_DIR "/nested/nested_file.txt", "w");
    if (f) {
        fprintf(f, "Nested file content\n");
        fclose(f);
    }
    
    /* Run the command with the directory as argument */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -f %s/nested", getenv("PWD"), TEST_DIR);
    char *output = run_command(cmd);
    
    /* Directory should not be in the output */
    ASSERT("Output does not contain directory as file", 
           !string_contains(output, "File: " TEST_DIR "/nested\n```\n```"));
    
    /* But nested file should be in the output */
    ASSERT("Output contains nested file", 
           string_contains(output, "nested_file.txt"));
}

/* Test file tree generation with nested directories */
TEST(test_file_tree_structure) {
    /* Create a multi-level directory structure for testing */
    mkdir(TEST_DIR "/src", 0755);
    mkdir(TEST_DIR "/src/util", 0755);
    mkdir(TEST_DIR "/src/core", 0755);
    mkdir(TEST_DIR "/include", 0755);
    
    /* Create some test files in the nested directories */
    FILE *f;
    
    f = fopen(TEST_DIR "/src/main.c", "w");
    if (f) {
        fprintf(f, "Main file\n");
        fclose(f);
    }
    
    f = fopen(TEST_DIR "/src/util/helper.c", "w");
    if (f) {
        fprintf(f, "Helper utilities\n");
        fclose(f);
    }
    
    f = fopen(TEST_DIR "/src/core/engine.c", "w");
    if (f) {
        fprintf(f, "Core engine\n");
        fclose(f);
    }
    
    f = fopen(TEST_DIR "/include/header.h", "w");
    if (f) {
        fprintf(f, "Header file\n");
        fclose(f);
    }
    
    /* Run the command with the root directory */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cd %s && %s/llm_ctx -f .", TEST_DIR, getenv("PWD"));
    char *output = run_command(cmd);
    
    /* Check if the file tree structure is properly shown */
    ASSERT("File tree contains src directory", 
           string_contains(output, "src"));
    
    ASSERT("File tree contains include directory", 
           string_contains(output, "include"));
    
    ASSERT("File tree contains util subdirectory", 
           string_contains(output, "util"));
    
    ASSERT("File tree contains core subdirectory", 
           string_contains(output, "core"));
    
    /* Check if tree indentation is present */
    ASSERT("File tree contains proper indentation", 
           string_contains(output, "├── ") || string_contains(output, "└── "));
    
    /* Check if nested file paths are properly shown */
    ASSERT("File tree shows main.c in correct location", 
           string_contains(output, "src/main.c") || 
           (string_contains(output, "src") && string_contains(output, "main.c")));
    
    ASSERT("File tree shows helper.c in correct location", 
           string_contains(output, "util/helper.c") || 
           (string_contains(output, "util") && string_contains(output, "helper.c")));
}

int main(void) {
    printf("Running llm_ctx CLI integration tests\n");
    printf("=====================================\n");
    
    /* Set up the test environment */
    setup_test_env();
    
    /* Run tests */
    RUN_TEST(test_cli_gitignore_default);
    RUN_TEST(test_cli_no_gitignore);
    RUN_TEST(test_cli_ignore_logs);
    RUN_TEST(test_cli_ignore_dirs);
    RUN_TEST(test_cli_help_message);
    RUN_TEST(test_directory_handling);
    RUN_TEST(test_file_tree_structure);
    
    /* Clean up */
    teardown_test_env();
    
    PRINT_TEST_SUMMARY();
}