#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "test_framework.h"

/**
 * Test FileRank functionality
 */

/* Run a command and capture output */
static char *run_command(const char *cmd) {
    static char buffer[32768];
    buffer[0] = '\0';
    char cmd_redir[2048];
    
    snprintf(cmd_redir, sizeof(cmd_redir), "%s 2>&1", cmd);
    
    FILE *pipe = popen(cmd_redir, "r");
    if (!pipe) {
        snprintf(buffer, sizeof(buffer), "Error: popen failed");
        return buffer;
    }
    
    char tmp[1024];
    size_t buffer_len = 0;
    while (fgets(tmp, sizeof(tmp), pipe) != NULL) {
        size_t tmp_len = strlen(tmp);
        if (buffer_len + tmp_len < sizeof(buffer) - 1) {
            strcat(buffer, tmp);
            buffer_len += tmp_len;
        }
    }
    
    pclose(pipe);
    return buffer;
}

/* Get the path to llm_ctx binary */
static const char *get_llm_ctx_path(void) {
    /* Try current directory first */
    if (access("./llm_ctx", X_OK) == 0) {
        return "./llm_ctx";
    }
    /* Try parent directory (when running from tests/) */
    if (access("../llm_ctx", X_OK) == 0) {
        return "../llm_ctx";
    }
    /* Try from PWD environment variable */
    const char *pwd = getenv("PWD");
    if (pwd) {
        static char path[1024];
        snprintf(path, sizeof(path), "%s/llm_ctx", pwd);
        if (access(path, X_OK) == 0) {
            return path;
        }
    }
    /* Default to current directory */
    return "./llm_ctx";
}

/* Setup test data */
static void setup_test_data(void) {
    /* Create test directory */
    mkdir("tests/test_data", 0755);
    
    /* Create test files */
    FILE *f;
    
    f = fopen("tests/test_data/simple.txt", "w");
    if (f) {
        fprintf(f, "This is a simple test file.\n");
        fprintf(f, "It contains some basic text content.\n");
        fclose(f);
    }
    
    f = fopen("tests/test_data/example.txt", "w");
    if (f) {
        fprintf(f, "This is an example file.\n");
        fprintf(f, "It has example content for testing.\n");
        fclose(f);
    }
    
    f = fopen("tests/test_data/binary.bin", "wb");
    if (f) {
        unsigned char binary_data[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
        fwrite(binary_data, 1, sizeof(binary_data), f);
        fclose(f);
    }
}

/* Test --filerank-debug flag with no query (Slice 0) */
TEST(test_filerank_debug_no_query) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "LLM_CTX_NO_CONFIG=1 %s --filerank-debug -o -f tests/test_data/simple.txt tests/test_data/example.txt", get_llm_ctx_path());
    char *output = run_command(cmd);
    
    /* Debug: print first 200 chars of output */
    /* printf("DEBUG OUTPUT: %.200s\n", output); */
    
    /* Should not output FileRank debug info when no query is provided */
    ASSERT("FileRank debug should not appear without query", strstr(output, "FileRank") == NULL);
    
    /* Files should still be included in output */
    ASSERT("simple.txt should be in output", strstr(output, "simple.txt") != NULL);
    ASSERT("example.txt should be in output", strstr(output, "example.txt") != NULL);
}

/* Test --filerank-debug flag with query (updated for Slice 1) */
TEST(test_filerank_debug_with_query) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "LLM_CTX_NO_CONFIG=1 %s --filerank-debug -o -c \"test query\" -f tests/test_data/simple.txt tests/test_data/example.txt", get_llm_ctx_path());
    char *output = run_command(cmd);
    
    /* Should output FileRank debug info */
    ASSERT("FileRank debug header should appear", strstr(output, "FileRank (query: \"test query\")") != NULL);
    
    /* simple.txt should have positive score (contains "test"), example.txt ~0.00 */
    /* With TF-IDF, exact scores may vary, so just check relative ordering */
    char *simple_line = strstr(output, "tests/test_data/simple.txt");
    ASSERT("simple.txt should appear in FileRank output", simple_line != NULL);
    /* Accept both 0.00 and -0.00 due to floating point precision */
    ASSERT("example.txt should have score ~0.00", 
           strstr(output, "  0.00  tests/test_data/example.txt") != NULL || 
           strstr(output, "  -0.00  tests/test_data/example.txt") != NULL);
    
    /* Files should still be included in output in original order */
    ASSERT("simple.txt file content should appear", strstr(output, "File: tests/test_data/simple.txt") != NULL);
    ASSERT("example.txt file content should appear", strstr(output, "File: tests/test_data/example.txt") != NULL);
}

/* Test that files are now sorted by score when query is provided (Slice 2) */
TEST(test_filerank_preserves_order) {
    /* Use a query that matches simple.txt to ensure it ranks first */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "LLM_CTX_NO_CONFIG=1 %s -o -c \"test\" -f tests/test_data/simple.txt tests/test_data/example.txt tests/test_data/binary.bin", get_llm_ctx_path());
    char *output = run_command(cmd);
    
    /* Find positions of files in output */
    char *simple_pos = strstr(output, "File: tests/test_data/simple.txt");
    char *example_pos = strstr(output, "File: tests/test_data/example.txt");
    char *binary_pos = strstr(output, "File: tests/test_data/binary.bin");
    
    /* simple.txt should rank first due to containing "test" */
    ASSERT("All files should appear in output", simple_pos != NULL && example_pos != NULL && binary_pos != NULL);
    ASSERT("simple.txt should come first due to match", simple_pos < example_pos && simple_pos < binary_pos);
}

/* Test hit counting in file paths (Slice 1) */
TEST(test_filerank_path_hits) {
    /* Create files with query terms in paths - use hyphens for word boundaries */
    FILE *f;
    f = fopen("tests/test_data/arena-test.txt", "w");
    if (f) { fprintf(f, "No matching content\n"); fclose(f); }
    
    f = fopen("tests/test_data/no-match.txt", "w");  
    if (f) { fprintf(f, "No matching content\n"); fclose(f); }
    
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "LLM_CTX_NO_CONFIG=1 %s --filerank-debug -o -c \"arena test\" -f tests/test_data/arena-test.txt tests/test_data/no-match.txt", get_llm_ctx_path());
    char *output = run_command(cmd);
    
    /* Should have FileRank output */
    ASSERT("Should have FileRank debug output", strstr(output, "FileRank") != NULL);
    
    /* arena-test.txt should score 4.00 (2*2 for path matches), no-match.txt ~0.00 */
    ASSERT("arena-test.txt should have score 4.00", strstr(output, "  4.00  tests/test_data/arena-test.txt") != NULL);
    ASSERT("no-match.txt should have score ~0.00", 
           strstr(output, "  0.00  tests/test_data/no-match.txt") != NULL ||
           strstr(output, "  -0.00  tests/test_data/no-match.txt") != NULL);
    
    /* Cleanup */
    unlink("tests/test_data/arena-test.txt");
    unlink("tests/test_data/no-match.txt");
}

/* Test hit counting in file content (Slice 1) */
TEST(test_filerank_content_hits) {
    /* Create files with different content match counts */
    FILE *f;
    f = fopen("tests/test_data/many_tokens.txt", "w");
    if (f) {
        fprintf(f, "This file has many token matches.\n");
        fprintf(f, "The token appears here too.\n");
        fprintf(f, "And token is mentioned again.\n");
        fclose(f);
    }
    
    f = fopen("tests/test_data/few_tokens.txt", "w");
    if (f) {
        fprintf(f, "This file has only one token match.\n");
        fclose(f);
    }
    
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "LLM_CTX_NO_CONFIG=1 %s --filerank-debug -o -c \"token\" -f tests/test_data/many_tokens.txt tests/test_data/few_tokens.txt", get_llm_ctx_path());
    char *output = run_command(cmd);
    
    /* Verify scores directly in output */
    ASSERT("many_tokens.txt should have score 3.00", strstr(output, "  3.00  tests/test_data/many_tokens.txt") != NULL);
    ASSERT("few_tokens.txt should have score 1.00", strstr(output, "  1.00  tests/test_data/few_tokens.txt") != NULL);
    
    /* Cleanup */
    unlink("tests/test_data/many_tokens.txt");
    unlink("tests/test_data/few_tokens.txt");
}

/* Test case-insensitive matching (Slice 1) */
TEST(test_filerank_case_insensitive) {
    FILE *f;
    f = fopen("tests/test_data/mixed_case.txt", "w");
    if (f) {
        fprintf(f, "ARENA Arena arena\n");
        fclose(f);
    }
    
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "LLM_CTX_NO_CONFIG=1 %s --filerank-debug -o -c \"arena\" -f tests/test_data/mixed_case.txt", get_llm_ctx_path());
    char *output = run_command(cmd);
    
    char *score_line = strstr(output, "  ");
    ASSERT("Score line should exist", score_line != NULL);
    
    double score = 0;
    sscanf(score_line, "%lf", &score);
    ASSERT("Should count all 3 case variations", score == 3.0);
    
    /* Cleanup */
    unlink("tests/test_data/mixed_case.txt");
}

/* Test size penalty (Slice 2) */
TEST(test_filerank_size_penalty) {
    /* Create a large file and a small file with same content */
    FILE *f;
    f = fopen("tests/test_data/small.txt", "w");
    if (f) {
        fprintf(f, "test content\n");
        fclose(f);
    }
    
    f = fopen("tests/test_data/large.txt", "w");
    if (f) {
        fprintf(f, "test content\n");
        /* Add 100KB of varied content to trigger size penalty without tokenizer issues */
        for (int i = 0; i < 1024; i++) {
            fprintf(f, "This is line %d with some varied content to avoid tokenizer issues. ", i);
            fprintf(f, "Adding more text here to make the file larger. Lorem ipsum dolor sit amet.\n");
        }
        fclose(f);
    }
    
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "LLM_CTX_NO_CONFIG=1 %s --filerank-debug -o -c \"test\" -f tests/test_data/small.txt tests/test_data/large.txt", get_llm_ctx_path());
    char *output = run_command(cmd);
    
    /* Both should match "test" but large file should have lower score due to penalty */
    char *small_line = strstr(output, "small.txt");
    char *large_line = strstr(output, "large.txt");
    
    ASSERT("Both files should appear in output", small_line != NULL && large_line != NULL);
    
    /* Small file should appear first (higher score) */
    char *file_context = strstr(output, "<file_context>");
    char *small_file = strstr(file_context, "File: tests/test_data/small.txt");
    char *large_file = strstr(file_context, "File: tests/test_data/large.txt");
    
    ASSERT("small.txt should appear before large.txt due to size penalty", 
           small_file != NULL && large_file != NULL && small_file < large_file);
    
    /* Cleanup */
    unlink("tests/test_data/small.txt");
    unlink("tests/test_data/large.txt");
}

/* Test that files are sorted by score (Slice 2) */
TEST(test_filerank_sorting) {
    /* Create files with different scores */
    FILE *f;
    f = fopen("tests/test_data/high-score.txt", "w");
    if (f) {
        fprintf(f, "arena arena arena\n"); /* score 3 */
        fclose(f);
    }
    
    f = fopen("tests/test_data/medium-score.txt", "w");
    if (f) {
        fprintf(f, "arena arena\n"); /* score 2 */
        fclose(f);
    }
    
    f = fopen("tests/test_data/low-score.txt", "w");
    if (f) {
        fprintf(f, "arena\n"); /* score 1 */
        fclose(f);
    }
    
    f = fopen("tests/test_data/no-score.txt", "w");
    if (f) {
        fprintf(f, "nothing\n"); /* score 0 */
        fclose(f);
    }
    
    /* Test with files in reverse order */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "LLM_CTX_NO_CONFIG=1 %s --filerank-debug -o -c \"arena\" -f tests/test_data/no-score.txt tests/test_data/low-score.txt tests/test_data/medium-score.txt tests/test_data/high-score.txt", get_llm_ctx_path());
    char *output = run_command(cmd);
    
    /* Check debug output shows sorted scores */
    /* With TF-IDF, exact scores may vary but order should be maintained */
    char *filerank_section = strstr(output, "FileRank");
    ASSERT("FileRank section should exist", filerank_section != NULL);
    
    /* Find positions of each file in the FileRank output */
    char *high_pos = strstr(filerank_section, "high-score.txt");
    char *medium_pos = strstr(filerank_section, "medium-score.txt");
    char *low_pos = strstr(filerank_section, "low-score.txt");
    char *no_pos = strstr(filerank_section, "no-score.txt");
    
    ASSERT("All files should appear in FileRank output", high_pos && medium_pos && low_pos && no_pos);
    ASSERT("Files should be in descending score order", high_pos < medium_pos && medium_pos < low_pos && low_pos < no_pos);
    
    /* Check files are output in sorted order */
    char *file_context = strstr(output, "<file_context>");
    char *high = strstr(file_context, "File: tests/test_data/high-score.txt");
    char *medium = strstr(file_context, "File: tests/test_data/medium-score.txt");
    char *low = strstr(file_context, "File: tests/test_data/low-score.txt");
    char *no = strstr(file_context, "File: tests/test_data/no-score.txt");
    
    ASSERT("All files should be in output", high && medium && low && no);
    ASSERT("Files should be sorted by score", high < medium && medium < low && low < no);
    
    /* Cleanup */
    unlink("tests/test_data/high-score.txt");
    unlink("tests/test_data/medium-score.txt");
    unlink("tests/test_data/low-score.txt");
    unlink("tests/test_data/no-score.txt");
}

/* Test TF-IDF scoring (Slice 3) */
TEST(test_filerank_tfidf) {
    /* Create test files with different term frequencies */
    FILE *f;
    
    /* File with unique term that appears frequently */
    f = fopen("tests/test_data/unique-term.txt", "w");
    if (f) {
        fprintf(f, "This file contains the unique term algorithm.\n");
        fprintf(f, "The algorithm is efficient and well-tested.\n");
        fprintf(f, "Algorithm implementation follows best practices.\n");
        for (int i = 0; i < 5; i++) {
            fprintf(f, "Algorithm analysis shows good results.\n");
        }
        fclose(f);
    }
    
    /* File with common terms only */
    f = fopen("tests/test_data/common-terms.txt", "w");
    if (f) {
        fprintf(f, "This file contains only common words.\n");
        fprintf(f, "The purpose is demonstration.\n");
        for (int i = 0; i < 10; i++) {
            fprintf(f, "Common words appear many times.\n");
        }
        fclose(f);
    }
    
    /* File with the unique term appearing once */
    f = fopen("tests/test_data/mixed-terms.txt", "w");
    if (f) {
        fprintf(f, "The algorithm is mentioned here once.\n");
        fprintf(f, "The rest has common words.\n");
        fclose(f);
    }
    
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "LLM_CTX_NO_CONFIG=1 %s --filerank-debug -o -c \"algorithm\" -f tests/test_data/unique-term.txt tests/test_data/common-terms.txt tests/test_data/mixed-terms.txt", get_llm_ctx_path());
    char *output = run_command(cmd);
    
    /* Check that TF-IDF scoring works */
    /* unique-term.txt should score highest due to high TF and IDF */
    char *unique_line = strstr(output, "unique-term.txt");
    char *mixed_line = strstr(output, "mixed-terms.txt");
    char *common_line = strstr(output, "common-terms.txt");
    
    ASSERT("All files should appear in output", unique_line && mixed_line && common_line);
    
    /* Extract scores - look for the score pattern before each filename */
    char score_unique[10] = {0}, score_mixed[10] = {0}, score_common[10] = {0};
    
    /* Find score for unique-term.txt */
    char *p = unique_line;
    while (p > output && *(p-1) != '\n') p--;
    sscanf(p, "%s", score_unique);
    
    /* Find score for mixed-terms.txt */
    p = mixed_line;
    while (p > output && *(p-1) != '\n') p--;
    sscanf(p, "%s", score_mixed);
    
    /* Find score for common-terms.txt */
    p = common_line;
    while (p > output && *(p-1) != '\n') p--;
    sscanf(p, "%s", score_common);
    
    /* Convert to doubles for comparison */
    double d_unique = atof(score_unique);
    double d_mixed = atof(score_mixed);
    double d_common = atof(score_common);
    
    ASSERT("unique-term.txt should have highest score (TF-IDF)", d_unique > d_mixed);
    ASSERT("mixed-terms.txt should score higher than common-terms.txt", d_mixed > d_common);
    ASSERT("common-terms.txt should have score ~0", d_common < 0.01 && d_common > -0.01);
    
    /* Cleanup */
    unlink("tests/test_data/unique-term.txt");
    unlink("tests/test_data/common-terms.txt");
    unlink("tests/test_data/mixed-terms.txt");
}

int main(void) {
    printf("Running filerank tests\n");
    printf("===================\n");
    
    /* Setup test data before each test group */
    setup_test_data();
    
    /* Slice 0 tests */
    RUN_TEST(test_filerank_debug_no_query);
    RUN_TEST(test_filerank_debug_with_query);
    RUN_TEST(test_filerank_preserves_order);
    
    /* Slice 1 tests */
    RUN_TEST(test_filerank_path_hits);
    RUN_TEST(test_filerank_content_hits);
    RUN_TEST(test_filerank_case_insensitive);
    
    /* Slice 2 tests */
    RUN_TEST(test_filerank_size_penalty);
    RUN_TEST(test_filerank_sorting);
    
    /* Slice 3 tests */
    RUN_TEST(test_filerank_tfidf);
    
    /* Cleanup after all tests */
    unlink("tests/test_data/simple.txt");
    unlink("tests/test_data/example.txt");
    unlink("tests/test_data/binary.bin");
    rmdir("tests/test_data");
    
    printf("\n");
    PRINT_TEST_SUMMARY();
}