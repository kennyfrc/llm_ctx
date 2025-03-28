#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../gitignore.h"
#include "test_framework.h"

/**
 * Test suite for gitignore pattern matching functionality
 */

TEST(test_add_ignore_pattern_basic) {
    reset_gitignore_patterns();
    
    char pattern[] = "*.txt";
    add_ignore_pattern(pattern);
    
    ASSERT_EQUALS(1, num_ignore_patterns);
    ASSERT_STR_EQUALS("*.txt", ignore_patterns[0].pattern);
    ASSERT_EQUALS(0, ignore_patterns[0].is_negation);
    ASSERT_EQUALS(0, ignore_patterns[0].match_only_dir);
}

TEST(test_add_ignore_pattern_negation) {
    reset_gitignore_patterns();
    
    char pattern[] = "!important.txt";
    add_ignore_pattern(pattern);
    
    ASSERT_EQUALS(1, num_ignore_patterns);
    ASSERT_STR_EQUALS("important.txt", ignore_patterns[0].pattern);
    ASSERT_EQUALS(1, ignore_patterns[0].is_negation);
    ASSERT_EQUALS(0, ignore_patterns[0].match_only_dir);
}

TEST(test_add_ignore_pattern_directory) {
    reset_gitignore_patterns();
    
    char pattern[] = "temp/";
    add_ignore_pattern(pattern);
    
    ASSERT_EQUALS(1, num_ignore_patterns);
    ASSERT_STR_EQUALS("temp", ignore_patterns[0].pattern);
    ASSERT_EQUALS(0, ignore_patterns[0].is_negation);
    ASSERT_EQUALS(1, ignore_patterns[0].match_only_dir);
}

TEST(test_add_ignore_pattern_whitespace) {
    reset_gitignore_patterns();
    
    char pattern[] = "  *.log  ";
    add_ignore_pattern(pattern);
    
    ASSERT_EQUALS(1, num_ignore_patterns);
    ASSERT_STR_EQUALS("*.log", ignore_patterns[0].pattern);
}

TEST(test_add_ignore_pattern_comment_line) {
    reset_gitignore_patterns();
    
    char pattern[] = "# This is a comment";
    add_ignore_pattern(pattern);
    
    ASSERT_EQUALS(0, num_ignore_patterns);
}

TEST(test_add_ignore_pattern_empty_line) {
    reset_gitignore_patterns();
    
    char pattern[] = "";
    add_ignore_pattern(pattern);
    
    ASSERT_EQUALS(0, num_ignore_patterns);
}

TEST(test_should_ignore_path_basic) {
    reset_gitignore_patterns();
    
    char pattern1[] = "*.txt";
    add_ignore_pattern(pattern1);
    
    ASSERT_EQUALS(1, should_ignore_path("file.txt"));
    ASSERT_EQUALS(0, should_ignore_path("file.md"));
    ASSERT_EQUALS(1, should_ignore_path("/home/user/file.txt"));
}

TEST(test_should_ignore_path_negation) {
    reset_gitignore_patterns();
    
    char pattern1[] = "*.txt";
    char pattern2[] = "!important.txt";
    add_ignore_pattern(pattern1);
    add_ignore_pattern(pattern2);
    
    ASSERT_EQUALS(1, should_ignore_path("file.txt"));
    ASSERT_EQUALS(0, should_ignore_path("important.txt"));
}

TEST(test_should_ignore_path_directory_only) {
    reset_gitignore_patterns();
    
    char pattern[] = "temp/";
    add_ignore_pattern(pattern);
    
    /* Can't really test directory matching in a unit test without file system access */
    /* We'd need a mock file system or integration test for this */
    /* Just test that non-directories aren't matched */
    ASSERT_EQUALS(0, should_ignore_path("temp.txt"));
}

TEST(test_respect_gitignore_flag) {
    reset_gitignore_patterns();
    
    char pattern[] = "*.txt";
    add_ignore_pattern(pattern);
    
    ASSERT_EQUALS(1, should_ignore_path("file.txt"));
    
    respect_gitignore = 0;
    ASSERT_EQUALS(0, should_ignore_path("file.txt"));
    
    respect_gitignore = 1;
}

TEST(test_pattern_precedence) {
    reset_gitignore_patterns();
    
    /* Patterns listed later should take precedence */
    char pattern1[] = "*.txt";
    char pattern2[] = "!*.txt";
    add_ignore_pattern(pattern1);
    add_ignore_pattern(pattern2);
    
    ASSERT_EQUALS(0, should_ignore_path("file.txt"));
    
    /* Reset and try in the opposite order */
    reset_gitignore_patterns();
    add_ignore_pattern(pattern2);
    add_ignore_pattern(pattern1);
    
    ASSERT_EQUALS(1, should_ignore_path("file.txt"));
}

TEST(test_negation_override) {
    reset_gitignore_patterns();
    
    char pattern1[] = "*.log";
    char pattern2[] = "!debug.log";
    char pattern3[] = "debug.log";
    
    add_ignore_pattern(pattern1);
    add_ignore_pattern(pattern2);
    add_ignore_pattern(pattern3);
    
    /* Current incorrect logic returns 0 because it stops at !debug.log */
    /* Correct logic should return 1 because the last match (debug.log) is an ignore rule */
    ASSERT_EQUALS(1, should_ignore_path("debug.log")); 
    
    /* Other .log files should still be ignored by the first rule */
    ASSERT_EQUALS(1, should_ignore_path("trace.log"));
    
    /* Non-log files should not be ignored */
    ASSERT_EQUALS(0, should_ignore_path("config.ini"));
}

/* Test that a later negation pattern correctly overrides an earlier ignore pattern */
TEST(test_negation_precedence_over_ignore) {
    reset_gitignore_patterns();

    char pattern1[] = "*.log";      // Ignore all .log files
    char pattern2[] = "!debug.log"; // BUT, do not ignore debug.log

    add_ignore_pattern(pattern1);
    add_ignore_pattern(pattern2);

    /* Expected: debug.log should NOT be ignored because the last matching pattern is a negation */
    /* Current buggy behavior: Returns 1 (ignored) because it stops at *.log */
    ASSERT_EQUALS(0, should_ignore_path("debug.log"));

    /* Other .log files should still be ignored */
    ASSERT_EQUALS(1, should_ignore_path("trace.log"));
}

/* Test that a later ignore pattern correctly overrides an earlier negation pattern */
TEST(test_ignore_precedence_over_negation) {
    reset_gitignore_patterns();

    char pattern1[] = "!important.txt"; // Do not ignore important.txt
    char pattern2[] = "*.txt";         // BUT, ignore all .txt files (takes precedence)

    add_ignore_pattern(pattern1);
    add_ignore_pattern(pattern2);

    /* Expected: important.txt SHOULD be ignored because the last matching pattern (*.txt) is an ignore rule */
    /* Current buggy behavior: Returns 0 (not ignored) because it stops at !important.txt */
    ASSERT_EQUALS(1, should_ignore_path("important.txt"));

    /* Other .txt files should also be ignored */
    ASSERT_EQUALS(1, should_ignore_path("another.txt"));

    /* Non-.txt files should not be ignored */
    ASSERT_EQUALS(0, should_ignore_path("config.ini"));
}


int main(void) {
    printf("Running gitignore pattern tests\n");
    printf("===============================\n");
    
    RUN_TEST(test_add_ignore_pattern_basic);
    RUN_TEST(test_add_ignore_pattern_negation);
    RUN_TEST(test_add_ignore_pattern_directory);
    RUN_TEST(test_add_ignore_pattern_whitespace);
    RUN_TEST(test_add_ignore_pattern_comment_line);
    RUN_TEST(test_add_ignore_pattern_empty_line);
    RUN_TEST(test_should_ignore_path_basic);
    RUN_TEST(test_should_ignore_path_negation);
    RUN_TEST(test_should_ignore_path_directory_only);
    RUN_TEST(test_respect_gitignore_flag);
    RUN_TEST(test_pattern_precedence);
    RUN_TEST(test_negation_override); // This test might be redundant or overlap now
    RUN_TEST(test_negation_precedence_over_ignore); // New test
    RUN_TEST(test_ignore_precedence_over_negation); // New test

    PRINT_TEST_SUMMARY();
}
