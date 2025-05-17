#define _GNU_SOURCE
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

/* Test directory for creating test files (prefixed) */
#define TEST_DIR "/tmp/__llm_ctx_test"

/* Forward declarations for helper functions (copied from test_cli.c) */
void setup_test_env(void);
void teardown_test_env(void);
char *run_command(const char *cmd);

/* Helper function to run a command (copied from test_cli.c) */
char *run_command(const char *cmd) {
    // Increased buffer size to handle potentially large outputs + stderr
    static char buffer[32768];
    buffer[0] = '\0';
    char cmd_redir[2048]; // Buffer for command + redirection

    // Redirect stderr to stdout
    snprintf(cmd_redir, sizeof(cmd_redir), "%s 2>&1", cmd);

    FILE *pipe = popen(cmd_redir, "r");
    if (!pipe) {
        perror("popen failed");
        snprintf(buffer, sizeof(buffer), "Error: popen failed for command: %s", cmd);
        return buffer;
    }

    char tmp[1024];
    while (fgets(tmp, sizeof(tmp), pipe) != NULL) {
        /* Check buffer space before strcat */
        if (strlen(buffer) + strlen(tmp) < sizeof(buffer) - 1) {
            strcat(buffer, tmp);
        } else {
            /* Avoid buffer overflow */
            fprintf(stderr, "Warning: run_command buffer overflow prevented.\n");
            break;
        }
    }

    int status = pclose(pipe);
    if (status == -1) {
        perror("pclose failed");
    }

    return buffer;
}

/* Setup function (copied from test_cli.c - simplified for config tests) */
void setup_test_env(void) {
    mkdir(TEST_DIR, 0755);
    /* Clean any leftover test config from previous runs */
    char test_conf_path[1024];
    snprintf(test_conf_path, sizeof(test_conf_path), "%s/.llm_ctx.conf", TEST_DIR);
    unlink(test_conf_path);
    /* Create a dummy file needed by tests */
    char dummy_file_path[1024];
    snprintf(dummy_file_path, sizeof(dummy_file_path), "%s/__regular.txt", TEST_DIR);
    FILE *f = fopen(dummy_file_path, "w");
    if (f) { fprintf(f, "Dummy content\n"); fclose(f); }
}

/* Teardown function (copied from test_cli.c) */
void teardown_test_env(void) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

/* ---------- Test 1: blank‑line preservation ---------- */
TEST(test_config_multiline_blank_lines) {
    const char *conf = TEST_DIR"/.llm_ctx.conf";
    FILE *f = fopen(conf,"w");
    fprintf(f,
        "system_prompt=\n"
        "  Line A\n"
        "  \n"            /* blank line inside block */
        "  Line B\n");
    fclose(f);

    char cmd[256];
    snprintf(cmd,sizeof(cmd),"cd %s && %s/llm_ctx -f __regular.txt",TEST_DIR,getenv("PWD"));
    char *out = run_command(cmd);

    ASSERT("contains Line A", strstr(out,"Line A"));
    ASSERT("contains blank-line gap", strstr(out,"Line A\n\nLine B"));
    unlink(conf);
}

/* ---------- Test 2: Markdown headings ---------- */
TEST(test_config_multiline_headings) {
    const char *conf = TEST_DIR"/.llm_ctx.conf";
    FILE *f = fopen(conf,"w");
    fprintf(f,
        "system_prompt=\n"
        "  ## Heading\n"
        "  Body line\n");
    fclose(f);

    char cmd[256];
    snprintf(cmd,sizeof(cmd),"cd %s && %s/llm_ctx -f __regular.txt",TEST_DIR,getenv("PWD"));
    char *out = run_command(cmd);

    ASSERT("contains ## Heading", strstr(out,"## Heading"));
    unlink(conf);
}

/* ---------- Test 3: indented comment line ---------- */
TEST(test_config_multiline_indented_comment) {
    const char *conf = TEST_DIR"/.llm_ctx.conf";
    FILE *f = fopen(conf,"w");
    fprintf(f,
        "system_prompt=\n"
        "  Not a comment\n"
        "  # Still part of prompt\n");
    fclose(f);

    char cmd[256];
    snprintf(cmd,sizeof(cmd),"cd %s && %s/llm_ctx -f __regular.txt",TEST_DIR,getenv("PWD"));
    char *out = run_command(cmd);

    ASSERT("contains '# Still part of prompt'",
           strstr(out,"# Still part of prompt"));
    unlink(conf);
}

/* ---------- Test 4: minimal indent trim ---------- */
TEST(test_config_multiline_trim_indent) {
    const char *conf = TEST_DIR"/.llm_ctx.conf";
    FILE *f = fopen(conf,"w");
    fprintf(f,
        "system_prompt=\n"
        "      Zero\n"             /* Indent 6 */
        "        TwoSpaces\n");    /* Indent 8 */
    fclose(f);

    char cmd[256];
    snprintf(cmd,sizeof(cmd),"cd %s && %s/llm_ctx -f __regular.txt",TEST_DIR,getenv("PWD"));
    char *out = run_command(cmd);

    /* Expect "Zero\n  TwoSpaces\n" after removing common 6 spaces */
    ASSERT("contains 'Zero\\n  TwoSpaces'", strstr(out,"Zero\n  TwoSpaces"));
    unlink(conf);
}

/* ---------- Boilerplate ---------- */
int main(void) {
    printf("Running config-parser tests\n===========================\n");
    setup_test_env();     /* call the same helper from test_cli.c */
    RUN_TEST(test_config_multiline_blank_lines);
    RUN_TEST(test_config_multiline_headings);
    RUN_TEST(test_config_multiline_indented_comment);
    RUN_TEST(test_config_multiline_trim_indent);
    teardown_test_env();
    PRINT_TEST_SUMMARY();
}

