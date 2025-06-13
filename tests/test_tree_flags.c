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
    char full_cmd[2048];
    
    /* Get the correct path to llm_ctx based on PWD */
    const char *pwd = getenv("PWD");
    if (!pwd) pwd = ".";
    
    /* Replace relative path with absolute path */
    if (strncmp(cmd, "../llm_ctx", 10) == 0) {
        snprintf(full_cmd, sizeof(full_cmd), "%s/llm_ctx%s", pwd, cmd + 10);
    } else if (strncmp(cmd, "./llm_ctx", 9) == 0) {
        snprintf(full_cmd, sizeof(full_cmd), "%s/llm_ctx%s", pwd, cmd + 9);
    } else {
        snprintf(full_cmd, sizeof(full_cmd), "%s", cmd);
    }
    
    snprintf(cmd_redir, sizeof(cmd_redir), "%s 2>&1", full_cmd);
    
    FILE *pipe = popen(cmd_redir, "r");
    if (!pipe) {
        snprintf(buffer, sizeof(buffer), "Error: popen failed for command: %s", cmd_redir);
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

/* Test that -t shows complete directory tree (full tree) */
TEST(test_t_flag_full_tree) {
    setup_test_files();
    
    char *output = run_command("../llm_ctx -t -o --no-gitignore -f test_tree_dir/src/main.c test_tree_dir/lib/helper.c");
    
    /* Should contain the specified files */
    if (!string_contains(output, "main.c")) {
        printf("\nDEBUG: Output does not contain 'main.c'. Full output:\n%s\n", output);
        fflush(stdout);
    }
    ASSERT("Output should contain main.c", string_contains(output, "main.c"));
    ASSERT("Output should contain helper.c", string_contains(output, "helper.c"));
    
    /* Should ALSO contain files that weren't specified (full tree) */
    ASSERT("Output should contain utils.c", string_contains(output, "utils.c"));
    ASSERT("Output should contain README.md", string_contains(output, "README.md"));
    
    /* Should have the correct tree structure */
    ASSERT("Output should contain <file_tree>", string_contains(output, "<file_tree>"));
    ASSERT("Output should contain </file_tree>", string_contains(output, "</file_tree>"));
    
    cleanup_test_files();
}

/* Test that -T shows filtered tree (only specified files) */
TEST(test_T_flag_filtered_tree) {
    setup_test_files();
    
    /* Test with just one file specified */
    char *output = run_command("../llm_ctx -T -o -f test_tree_dir/src/main.c");
    
    /* Should contain specified file */
    if (!string_contains(output, "main.c")) {
        printf("\nDEBUG test_T_flag: Output does not contain 'main.c'. Full output:\n%s\n", output);
        fflush(stdout);
    }
    ASSERT("Output should contain main.c", string_contains(output, "main.c"));
    ASSERT("Output should NOT contain utils.c (not specified)", !string_contains(output, "utils.c"));
    
    /* Should have the correct tree structure */
    ASSERT("Output should contain <file_tree>", string_contains(output, "<file_tree>"));
    ASSERT("Output should contain </file_tree>", string_contains(output, "</file_tree>"));
    ASSERT("Output should contain test_tree_dir", string_contains(output, "test_tree_dir"));
    ASSERT("Output should contain src", string_contains(output, "src"));
    
    cleanup_test_files();
}

/* Test that -t with patterns shows full tree regardless of pattern */
TEST(test_t_flag_with_patterns) {
    setup_test_files();
    
    char *output = run_command("../llm_ctx -t -o --no-gitignore -f 'test_tree_dir/src/*.c'");
    
    /* Should contain .c files from src (matched) */
    if (!string_contains(output, "main.c")) {
        printf("\nDEBUG test_t_flag_with_patterns: Output does not contain 'main.c'. Full output:\n%s\n", output);
        fflush(stdout);
    }
    ASSERT("Output should contain main.c", string_contains(output, "main.c"));
    ASSERT("Output should contain utils.c", string_contains(output, "utils.c"));
    
    /* Should ALSO contain files from other directories (full tree) */
    ASSERT("Output should contain helper.c", string_contains(output, "helper.c"));
    ASSERT("Output should contain README.md", string_contains(output, "README.md"));
    
    cleanup_test_files();
}

/* Test that -T with patterns shows filtered tree (only matched files) */
TEST(test_T_flag_with_patterns) {
    setup_test_files();
    
    char *output = run_command("../llm_ctx -T -o -f 'test_tree_dir/src/*.c'");
    
    /* Should contain .c files from src (matched) */
    if (!string_contains(output, "main.c")) {
        printf("\nDEBUG test_T_flag_with_patterns: Output does not contain 'main.c'. Full output:\n%s\n", output);
        fflush(stdout);
    }
    ASSERT("Output should contain main.c", string_contains(output, "main.c"));
    ASSERT("Output should contain utils.c", string_contains(output, "utils.c"));
    
    /* Should NOT contain files that don't match pattern */
    ASSERT("Output should NOT contain helper.c", !string_contains(output, "helper.c"));
    ASSERT("Output should NOT contain README.md", !string_contains(output, "README.md"));
    
    cleanup_test_files();
}

/* Test that both flags produce only tree output (no file content) */
TEST(test_tree_flags_no_content) {
    setup_test_files();
    
    /* Test -t flag */
    char *output_t = run_command("../llm_ctx -t -o -f test_tree_dir/src/main.c");
    ASSERT("Output -t should NOT contain <file_context>", !string_contains(output_t, "<file_context>"));
    ASSERT("Output -t should NOT contain file content", !string_contains(output_t, "int main"));
    
    /* Test -T flag */
    char *output_T = run_command("../llm_ctx -T -o -f test_tree_dir/src/main.c");
    ASSERT("Output -T should NOT contain <file_context>", !string_contains(output_T, "<file_context>"));
    ASSERT("Output -T should NOT contain file content", !string_contains(output_T, "int main"));
    
    cleanup_test_files();
}

/* Test new behavior: normal mode does NOT show tree by default */
TEST(test_normal_mode_no_tree_by_default) {
    setup_test_files();
    
    /* Without -t or -T, should NOT show file tree, only content */
    char *output = run_command("../llm_ctx -o -f test_tree_dir/src/main.c");
    
    /* Should NOT contain the file tree */
    ASSERT("Output should NOT contain <file_tree>", !string_contains(output, "<file_tree>"));
    ASSERT("Output should NOT contain utils.c (not specified)", !string_contains(output, "utils.c"));
    
    /* Should still contain file content */
    ASSERT("Output should contain <file_context>", string_contains(output, "<file_context>"));
    ASSERT("Output should contain main.c in file context", string_contains(output, "main.c"));
    
    /* Debug output before final assertion */
    if (!string_contains(output, "int main")) {
        printf("\nDEBUG: Output does not contain 'int main'. Full output:\n%s\n", output);
        fflush(stdout);
    }
    
    ASSERT("Output should contain file content", string_contains(output, "int main"));
    
    cleanup_test_files();
}

int main(void) {
    printf("Running tree flags tests\n");
    printf("========================\n");
    
    RUN_TEST(test_t_flag_full_tree);
    RUN_TEST(test_T_flag_filtered_tree);
    RUN_TEST(test_t_flag_with_patterns);
    RUN_TEST(test_T_flag_with_patterns);
    RUN_TEST(test_tree_flags_no_content);
    RUN_TEST(test_normal_mode_no_tree_by_default);
    
    printf("\n");
    PRINT_TEST_SUMMARY();
}