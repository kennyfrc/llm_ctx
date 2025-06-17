/**
 * Integration tests for the --keywords flag feature
 * Tests keyword boosting functionality across all slices
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "test_framework.h"

/* Helper to create test directory */
static void setup_test_dir(void) {
    system("rm -rf " TEST_DIR " 2>/dev/null");
    system("mkdir -p " TEST_DIR);
}

/* Slice 1: Walking skeleton tests */
static void test_keywords_flag_accepted(void) {
    setup_test_dir();
    
    /* Create test file */
    FILE *f = fopen(TEST_DIR "/file1.txt", "w");
    if (!f) {
        perror("fopen");
        return;
    }
    fprintf(f, "This contains TEST word");
    fclose(f);
    
    /* Test --keywords flag is accepted */
    int ret = system("./llm_ctx -f " TEST_DIR "/*.txt -c test --keywords dummy >/dev/null 2>&1");
    ASSERT("--keywords flag should be accepted", WEXITSTATUS(ret) == 0);
    
    /* Test -k short form is accepted */
    ret = system("./llm_ctx -f " TEST_DIR "/*.txt -c test -k dummy >/dev/null 2>&1");
    ASSERT("-k flag should be accepted", WEXITSTATUS(ret) == 0);
}

/* Slice 2: CLI parsing tests */
static void test_cli_parsing_single_keyword(void) {
    setup_test_dir();
    
    FILE *f = fopen(TEST_DIR "/test.txt", "w");
    if (!f) return;
    fprintf(f, "chat_input appears here");
    fclose(f);
    
    /* Test single keyword parsing */
    FILE *pipe = popen("./llm_ctx -f " TEST_DIR "/*.txt -c chat_input "
                      "--keywords chat_input --filerank-debug 2>&1 | grep Keywords:", "r");
    char line[256] = {0};
    if (pipe) {
        fgets(line, sizeof(line), pipe);
        pclose(pipe);
    }
    
    ASSERT("Should show parsed keyword", strstr(line, "chat_input:2.0") != NULL);
}

static void test_cli_parsing_multiple_keywords(void) {
    setup_test_dir();
    
    FILE *f = fopen(TEST_DIR "/test.txt", "w");
    if (!f) return;
    fprintf(f, "chat_input and prosemirror here");
    fclose(f);
    
    /* Test multiple keywords with custom weights */
    FILE *pipe = popen("./llm_ctx -f " TEST_DIR "/*.txt -c 'chat_input prosemirror' "
                      "--keywords 'chat_input:3,prosemirror:1.5' --filerank-debug 2>&1 | grep Keywords:", "r");
    char line[256] = {0};
    if (pipe) {
        fgets(line, sizeof(line), pipe);
        pclose(pipe);
    }
    
    ASSERT("Should parse multiple keywords", strstr(line, "chat_input:3.0") != NULL);
    ASSERT("Should parse second keyword", strstr(line, "prosemirror:1.5") != NULL);
}

static void test_case_insensitivity(void) {
    setup_test_dir();
    
    FILE *f = fopen(TEST_DIR "/test.txt", "w");
    if (!f) return;
    fprintf(f, "CHAT_INPUT in uppercase");
    fclose(f);
    
    /* Test case insensitive matching */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), 
             "./llm_ctx -f " TEST_DIR "/*.txt -c CHAT_INPUT "
             "--keywords chat_input:3 --filerank-weight tfidf:0.1,content:10.0 "
             "--filerank-debug 2>&1");
    
    FILE *pipe = popen(cmd, "r");
    char output[4096] = {0};
    if (pipe) {
        size_t total = 0;
        while (total < sizeof(output) - 1) {
            size_t n = fread(output + total, 1, sizeof(output) - total - 1, pipe);
            if (n == 0) break;
            total += n;
        }
        pclose(pipe);
    }
    
    /* Should contain keywords line and show boost applied */
    ASSERT("Should show keyword", strstr(output, "chat_input:3.0") != NULL);
    ASSERT("Should match case-insensitively", strstr(output, "test.txt") != NULL);
}

/* Slice 4: Edge validation tests */
static void test_invalid_weight_handling(void) {
    /* Test that invalid weights produce warnings and fallback to default */
    FILE *pipe = popen("./llm_ctx -f /dev/null -c test --keywords 'test:abc' 2>&1", "r");
    char output[512] = {0};
    if (pipe) {
        fread(output, 1, sizeof(output)-1, pipe);
        pclose(pipe);
    }
    
    ASSERT("Should warn about invalid weight", strstr(output, "Warning: Invalid factor") != NULL);
}

static void test_duplicate_keyword_handling(void) {
    /* Test that duplicate keywords update weight with warning */
    FILE *pipe = popen("./llm_ctx -f /dev/null -c test --keywords 'test:2,test:5' 2>&1", "r");
    char output[512] = {0};
    if (pipe) {
        fread(output, 1, sizeof(output)-1, pipe);
        pclose(pipe);
    }
    
    ASSERT("Should warn about duplicate", strstr(output, "Warning: Duplicate keyword") != NULL);
}

static void test_max_keywords_limit(void) {
    /* Test that >32 keywords produces warning */
    char cmd[2048] = "./llm_ctx -f /dev/null -c test --keywords '";
    for (int i = 1; i <= 35; i++) {
        if (i > 1) strcat(cmd, ",");
        char kw[32];
        snprintf(kw, sizeof(kw), "kw%d:%d", i, i);
        strcat(cmd, kw);
    }
    strcat(cmd, "' 2>&1");
    
    FILE *pipe = popen(cmd, "r");
    char output[1024] = {0};
    if (pipe) {
        fread(output, 1, sizeof(output)-1, pipe);
        pclose(pipe);
    }
    
    ASSERT("Should warn about max keywords", strstr(output, "Warning: Maximum") != NULL);
}

/* Slice 5: Documentation tests */
static void test_help_includes_keywords(void) {
    /* Test that help text includes --keywords documentation */
    FILE *pipe = popen("./llm_ctx --help 2>&1", "r");
    char output[8192] = {0};
    if (pipe) {
        fread(output, 1, sizeof(output)-1, pipe);
        pclose(pipe);
    }
    
    ASSERT("Help should mention --keywords", strstr(output, "--keywords") != NULL);
    ASSERT("Help should mention -k short form", strstr(output, "-k,") != NULL);
    ASSERT("Help should include format info", strstr(output, "token1:factor1") != NULL);
}

int main(void) {
    printf("Keywords Feature Tests\n");
    printf("======================\n");
    
    /* Slice 1 tests */
    printf("\nSlice 1: Walking Skeleton\n");
    RUN_TEST(test_keywords_flag_accepted);
    
    /* Slice 2 tests */  
    printf("\nSlice 2: CLI Parsing\n");
    RUN_TEST(test_cli_parsing_single_keyword);
    RUN_TEST(test_cli_parsing_multiple_keywords);
    RUN_TEST(test_case_insensitivity);
    
    /* Slice 3 skipped */
    printf("\nSlice 3: Config Integration (skipped)\n");
    
    /* Slice 4 tests */
    printf("\nSlice 4: Edge Validation\n");
    RUN_TEST(test_invalid_weight_handling);
    RUN_TEST(test_duplicate_keyword_handling);
    RUN_TEST(test_max_keywords_limit);
    
    /* Slice 5 tests */
    printf("\nSlice 5: Documentation\n");
    RUN_TEST(test_help_includes_keywords);
    
    PRINT_TEST_SUMMARY();
}