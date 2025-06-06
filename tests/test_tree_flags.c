#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "test_framework.h"

/* Test the -t and -T flags behavior */

/* Helper to check if a string contains a substring */
static int string_contains(const char *str, const char *substr) {
    return strstr(str, substr) != NULL;
}

/* Run a command and return output */
static char *run_command(const char *cmd) {
    static char buffer[327680];  // 320KB 
    buffer[0] = '\0';
    char cmd_redir[2048];
    
    snprintf(cmd_redir, sizeof(cmd_redir), "%s 2>&1", cmd);
    
    FILE *pipe = popen(cmd_redir, "r");
    if (!pipe) {
        snprintf(buffer, sizeof(buffer), "Error: popen failed for command: %s", cmd);
        return buffer;
    }
    
    char tmp[1024];
    size_t buffer_len = strlen(buffer);
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

/* Helper to create test files */
static void setup_test_files(void) {
    /* Create test directory structure */
    mkdir("test_tree_dir", 0755);
    mkdir("test_tree_dir/src", 0755);
    mkdir("test_tree_dir/lib", 0755);
    mkdir("test_tree_dir/docs", 0755);
    
    /* Create test files */
    FILE *f;
    
    f = fopen("test_tree_dir/src/main.c", "w");
    if (f) {
        fprintf(f, "int main(void) { return 0; }\n");
        fclose(f);
    }
    
    f = fopen("test_tree_dir/src/utils.c", "w");
    if (f) {
        fprintf(f, "void util() {}\n");
        fclose(f);
    }
    
    f = fopen("test_tree_dir/lib/helper.c", "w");
    if (f) {
        fprintf(f, "void helper() {}\n");
        fclose(f);
    }
    
    f = fopen("test_tree_dir/docs/README.md", "w");
    if (f) {
        fprintf(f, "# Documentation\n");
        fclose(f);
    }
}

/* Helper to cleanup test files */
static void cleanup_test_files(void) {
    /* Remove all test files */
    unlink("test_tree_dir/src/main.c");
    unlink("test_tree_dir/src/utils.c");
    unlink("test_tree_dir/lib/helper.c");
    unlink("test_tree_dir/docs/README.md");
    
    /* Remove directories */
    rmdir("test_tree_dir/src");
    rmdir("test_tree_dir/lib");
    rmdir("test_tree_dir/docs");
    rmdir("test_tree_dir");
}

/* Test that -t shows only specified files */
TEST(test_t_flag_limited_tree) {
    setup_test_files();
    
    char *output = run_command("./llm_ctx -t -f test_tree_dir/src/main.c test_tree_dir/lib/helper.c");
    
    /* Should contain the specified files */
    ASSERT("Output should contain main.c", string_contains(output, "main.c"));
    ASSERT("Output should contain helper.c", string_contains(output, "helper.c"));
    
    /* Should NOT contain files that weren't specified */
    ASSERT("Output should NOT contain utils.c", !string_contains(output, "utils.c"));
    ASSERT("Output should NOT contain README.md", !string_contains(output, "README.md"));
    
    /* Should have the correct tree structure */
    ASSERT("Output should contain <file_tree>", string_contains(output, "<file_tree>"));
    ASSERT("Output should contain </file_tree>", string_contains(output, "</file_tree>"));
    
    cleanup_test_files();
}

/* Test that -T shows complete directory tree */
TEST(test_T_flag_global_tree) {
    setup_test_files();
    
    /* Test with just one file specified */
    char *output = run_command("./llm_ctx -T -f test_tree_dir/src/main.c");
    
    /* Should contain ALL files in the directory tree */
    ASSERT("Output should contain main.c", string_contains(output, "main.c"));
    ASSERT("Output should contain utils.c (not specified but in tree)", string_contains(output, "utils.c"));
    
    /* Should have the correct tree structure */
    ASSERT("Output should contain <file_tree>", string_contains(output, "<file_tree>"));
    ASSERT("Output should contain </file_tree>", string_contains(output, "</file_tree>"));
    ASSERT("Output should contain test_tree_dir", string_contains(output, "test_tree_dir"));
    ASSERT("Output should contain src", string_contains(output, "src"));
    
    cleanup_test_files();
}

/* Test that -t with patterns shows only matched files */
TEST(test_t_flag_with_patterns) {
    setup_test_files();
    
    char *output = run_command("./llm_ctx -t -f 'test_tree_dir/src/*.c'");
    
    /* Should contain only .c files from src */
    ASSERT("Output should contain main.c", string_contains(output, "main.c"));
    ASSERT("Output should contain utils.c", string_contains(output, "utils.c"));
    
    /* Should NOT contain files from other directories */
    ASSERT("Output should NOT contain helper.c", !string_contains(output, "helper.c"));
    ASSERT("Output should NOT contain README.md", !string_contains(output, "README.md"));
    
    cleanup_test_files();
}

/* Test that -T with patterns shows complete tree */
TEST(test_T_flag_with_patterns) {
    setup_test_files();
    
    char *output = run_command("./llm_ctx -T -f 'test_tree_dir/src/*.c'");
    
    /* Should contain all .c files from src (matched) */
    ASSERT("Output should contain main.c", string_contains(output, "main.c"));
    ASSERT("Output should contain utils.c", string_contains(output, "utils.c"));
    
    /* Should contain tree structure */
    ASSERT("Output should contain test_tree_dir", string_contains(output, "test_tree_dir"));
    ASSERT("Output should contain src", string_contains(output, "src"));
    
    cleanup_test_files();
}

/* Test that both flags produce only tree output (no file content) */
TEST(test_tree_flags_no_content) {
    setup_test_files();
    
    /* Test -t flag */
    char *output_t = run_command("./llm_ctx -t -f test_tree_dir/src/main.c");
    ASSERT("Output -t should NOT contain <file_context>", !string_contains(output_t, "<file_context>"));
    ASSERT("Output -t should NOT contain file content", !string_contains(output_t, "int main"));
    
    /* Test -T flag */
    char *output_T = run_command("./llm_ctx -T -f test_tree_dir/src/main.c");
    ASSERT("Output -T should NOT contain <file_context>", !string_contains(output_T, "<file_context>"));
    ASSERT("Output -T should NOT contain file content", !string_contains(output_T, "int main"));
    
    cleanup_test_files();
}

/* Test backward compatibility: ensure normal mode still shows full tree */
TEST(test_normal_mode_shows_full_tree) {
    setup_test_files();
    
    /* Without -t or -T, should show full tree and content */
    char *output = run_command("./llm_ctx -f test_tree_dir/src/main.c");
    
    /* Should contain the full tree (like -T) */
    ASSERT("Output should contain main.c", string_contains(output, "main.c"));
    ASSERT("Output should contain utils.c (not specified but in tree)", string_contains(output, "utils.c"));
    
    /* Should also contain file content */
    ASSERT("Output should contain <file_context>", string_contains(output, "<file_context>"));
    
    /* Debug output before final assertion */
    if (!string_contains(output, "int main")) {
        printf("\nDEBUG: Output does not contain 'int main'. Full output:\n%s\n", output);
        fflush(stdout);
    }
    
    ASSERT("Output should contain file content", string_contains(output, "int main"));
    
    cleanup_test_files();
}

int main(void) {
    printf("\n=== Tree Flags Tests ===\n");
    
    RUN_TEST(test_t_flag_limited_tree);
    RUN_TEST(test_T_flag_global_tree);
    RUN_TEST(test_t_flag_with_patterns);
    RUN_TEST(test_T_flag_with_patterns);
    RUN_TEST(test_tree_flags_no_content);
    RUN_TEST(test_normal_mode_shows_full_tree);
    
    PRINT_TEST_SUMMARY();
}