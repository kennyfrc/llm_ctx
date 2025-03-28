#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include "test_framework.h"

/**
 * Stdin pipe integration tests for llm_ctx
 * 
 * These tests focus on:
 * - Testing stdin content handling
 * - Testing content type detection
 * - Testing proper formatting of content
 */

/* Test directory for creating test files (prefixed) */
#define TEST_DIR "/tmp/__llm_ctx_stdin_test"
#define LARGE_CONTENT_SIZE (1024 * 1024 + 100) /* Slightly over 1MB */
#define LARGE_CONTENT_FILE TEST_DIR "/__large_content.txt" // Prefixed
#define TRUNCATION_MARKER "---END_OF_LARGE_INPUT---"

/* Set up the test environment */
void setup_test_env(void) {
    /* Create test directory */
    mkdir(TEST_DIR, 0755);

    /* Create test files of different types (prefixed) */
    FILE *f;

    /* JSON file */
    f = fopen(TEST_DIR "/__sample.json", "w");
    if (f) {
        fprintf(f, "{\n  \"name\": \"test\",\n  \"version\": \"1.0.0\"\n}\n");
        fclose(f);
    }

    /* Markdown file */
    f = fopen(TEST_DIR "/__sample.md", "w");
    if (f) {
        fprintf(f, "# Test Markdown\n\nThis is a test markdown file.\n\n```\nCode block\n```\n");
        fclose(f);
    }

    /* XML file */
    f = fopen(TEST_DIR "/__sample.xml", "w");
    if (f) {
        fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root>\n  <element>content</element>\n</root>\n");
        fclose(f);
    }

    /* Diff file */
    f = fopen(TEST_DIR "/__sample.diff", "w");
    if (f) {
        fprintf(f, "diff --git a/__file.txt b/__file.txt\n"); // Prefixed paths in diff
        fprintf(f, "index 1234567..7654321 100644\n");
        fprintf(f, "--- a/__file.txt\n"); // Prefixed paths in diff
        fprintf(f, "+++ b/__file.txt\n"); // Prefixed paths in diff
        fprintf(f, "@@ -1,5 +1,5 @@\n");
        fprintf(f, " line 1\n");
        fprintf(f, "-line 2\n");
        fprintf(f, "+new line 2\n");
        fprintf(f, " line 3\n");
        fclose(f);
    }

    /* File list (not used directly by tests anymore, but keep for potential future use) */
    f = fopen(TEST_DIR "/__file_list.txt", "w");
    if (f) {
        fprintf(f, "%s/__sample.json\n", TEST_DIR); // Prefixed
        fprintf(f, "%s/__sample.md\n", TEST_DIR);   // Prefixed
        fprintf(f, "%s/__sample.xml\n", TEST_DIR);   // Prefixed
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

/* Run a command with stdin coming from a pipe */
char *run_command_with_stdin(const char *input_cmd, const char *cmd) {
    /* Increased buffer size to handle potentially large output (e.g., from large stdin test) */
    static char buffer[2 * 1024 * 1024]; /* 2MB */
    buffer[0] = '\0';
    size_t buffer_capacity = sizeof(buffer);
    size_t buffer_len = 0;
    
    /* Create a temporary script for running the commands */
    char script_file[1024];
    snprintf(script_file, sizeof(script_file), "/tmp/llm_ctx_script.XXXXXX");
    int fd = mkstemp(script_file);
    if (fd == -1) {
        return buffer;
    }
    close(fd);
    
    /* Write the script file */
    FILE *script = fopen(script_file, "w");
    if (!script) {
        unlink(script_file);
        return buffer;
    }
    
    fprintf(script, "#!/bin/sh\n");
    fprintf(script, "%s | %s\n", input_cmd, cmd);
    fclose(script);
    
    /* Make the script executable */
    chmod(script_file, 0755);
    
    /* Run the script */
    FILE *pipe = popen(script_file, "r");
    if (!pipe) {
        unlink(script_file);
        return buffer;
    }
    
    char tmp[4096]; /* Read in larger chunks */
    while (fgets(tmp, sizeof(tmp), pipe) != NULL) {
        size_t tmp_len = strlen(tmp);
        if (buffer_len + tmp_len < buffer_capacity) {
            /* Using strcat for simplicity; consider safer alternatives if issues arise */
            strcat(buffer, tmp); 
            buffer_len += tmp_len;
        } else {
            /* Buffer full - stop reading to avoid overflow */
            fprintf(stderr, "\nWarning: Test output buffer overflow in run_command_with_stdin!\n");
            break; 
        }
    }
    
    pclose(pipe);
    unlink(script_file);
    return buffer;
}

/* Test JSON content detection */
TEST(test_stdin_json_detection) {
    char cmd[1024];
    char input_cmd[1024];

    /* Create input command to cat the JSON file (prefixed) */
    snprintf(input_cmd, sizeof(input_cmd), "cat %s/__sample.json", TEST_DIR);

    /* Create llm_ctx command */
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx", getenv("PWD"));
    
    char *output = run_command_with_stdin(input_cmd, cmd);
    
    /* Check for proper formatting (simplified assertions) */
    ASSERT("Output contains stdin_content header", string_contains(output, "File: stdin_content"));
    ASSERT("Output contains the JSON content", string_contains(output, "\"name\": \"test\""));
}

/* Test markdown content detection */
TEST(test_stdin_markdown_detection) {
    char cmd[1024];
    char input_cmd[1024];

    /* Create input command to cat the markdown file (prefixed) */
    snprintf(input_cmd, sizeof(input_cmd), "cat %s/__sample.md", TEST_DIR);

    /* Create llm_ctx command */
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx", getenv("PWD"));
    
    char *output = run_command_with_stdin(input_cmd, cmd);
    
    /* Check for proper formatting (simplified assertions) */
    ASSERT("Output contains stdin_content header", string_contains(output, "File: stdin_content"));
    ASSERT("Output contains the markdown content", string_contains(output, "# Test Markdown"));
}

/* Test XML content detection */
TEST(test_stdin_xml_detection) {
    char cmd[1024];
    char input_cmd[1024];

    /* Create input command to cat the XML file (prefixed) */
    snprintf(input_cmd, sizeof(input_cmd), "cat %s/__sample.xml", TEST_DIR);

    /* Create llm_ctx command */
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx", getenv("PWD"));
    
    char *output = run_command_with_stdin(input_cmd, cmd);
    
    /* Check for proper formatting (simplified assertions) */
    ASSERT("Output contains stdin_content header", string_contains(output, "File: stdin_content"));
    ASSERT("Output contains the XML content", string_contains(output, "<root>"));
}

/* Test diff content detection */
TEST(test_stdin_diff_detection) {
    char cmd[1024];
    char input_cmd[1024];

    /* Create input command to cat the diff file (prefixed) */
    snprintf(input_cmd, sizeof(input_cmd), "cat %s/__sample.diff", TEST_DIR);

    /* Create llm_ctx command */
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx", getenv("PWD"));
    
    char *output = run_command_with_stdin(input_cmd, cmd);
    
    /* Check for proper formatting (simplified assertions) */
    ASSERT("Output contains stdin_content header", string_contains(output, "File: stdin_content"));
    ASSERT("Output contains the diff content", string_contains(output, "diff --git"));
}

/* Test file list input (using direct paths for robust testing) */
TEST(test_stdin_file_list) {
    char output_buffer[16384] = {0};

    /* Use direct llm_ctx calls for files (prefixed) */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -f %s/__sample.json %s/__sample.md %s/__sample.xml",
             getenv("PWD"), TEST_DIR, TEST_DIR, TEST_DIR);

    /* Run the command and capture output */
    FILE *pipe = popen(cmd, "r");
    if (pipe) {
        char tmp[1024];
        while (fgets(tmp, sizeof(tmp), pipe) != NULL) {
            strcat(output_buffer, tmp);
        }
        pclose(pipe);
    }

    /* Verify that it processes files correctly (prefixed) */
    ASSERT("Output contains __sample.json", string_contains(output_buffer, "__sample.json"));
    ASSERT("Output contains __sample.md", string_contains(output_buffer, "__sample.md"));
    ASSERT("Output contains __sample.xml", string_contains(output_buffer, "__sample.xml"));
}

/* Test with instruction flag (prefixed) */
TEST(test_stdin_with_instructions) {
    char cmd[1024];
    char input_cmd[1024];

    /* Create input command to cat the JSON file (prefixed) */
    snprintf(input_cmd, sizeof(input_cmd), "cat %s/__sample.json", TEST_DIR);

    /* Create llm_ctx command with instructions */
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -c \"Test instruction\"", getenv("PWD"));
    
    char *output = run_command_with_stdin(input_cmd, cmd);
    
    /* Check for proper formatting */
    ASSERT("Output contains user instructions", string_contains(output, "<user_instructions>"));
    ASSERT("Output contains the instruction", string_contains(output, "Test instruction"));
    ASSERT("Output contains stdin_content header", string_contains(output, "File: stdin_content"));
}

/* Test stdin processing with input larger than the internal buffer */
TEST(test_stdin_large_content_truncation) {
    /* 1. Create the large file */
    FILE *large_file = fopen(LARGE_CONTENT_FILE, "w");
    ASSERT("Failed to create large content file", large_file != NULL);
    if (!large_file) return;

    /* Write repeating pattern */
    size_t bytes_to_write = LARGE_CONTENT_SIZE - strlen(TRUNCATION_MARKER);
    const char pattern[] = "0123456789ABCDEF";
    size_t pattern_len = strlen(pattern);
    for (size_t i = 0; i < bytes_to_write; ++i) {
        fputc(pattern[i % pattern_len], large_file);
    }
    /* Write the marker at the very end */
    fprintf(large_file, "%s", TRUNCATION_MARKER);
    fclose(large_file);

    /* 2. Run llm_ctx with the large file piped to stdin */
    char cmd[1024];
    char input_cmd[1024];
    snprintf(input_cmd, sizeof(input_cmd), "cat %s", LARGE_CONTENT_FILE);
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx", getenv("PWD"));
    char *output = run_command_with_stdin(input_cmd, cmd);

    /* 3. Assertions */
    ASSERT("Output contains stdin_content header", string_contains(output, "File: stdin_content"));
    ASSERT("Output contains start of large content", string_contains(output, "0123456789ABCDEF0123"));
    ASSERT("Output does NOT contain the end marker (truncated)", !string_contains(output, TRUNCATION_MARKER));
    ASSERT("Output contains closing fence", string_contains(output, "```\n----------------------------------------"));
}


int main(void) {
    printf("Running llm_ctx stdin pipe integration tests\n");
    printf("===========================================\n");
    
    /* Set up the test environment */
    setup_test_env();
    
    /* Run tests */
    RUN_TEST(test_stdin_json_detection);
    RUN_TEST(test_stdin_markdown_detection);
    RUN_TEST(test_stdin_xml_detection);
    RUN_TEST(test_stdin_diff_detection);
    RUN_TEST(test_stdin_file_list);
    RUN_TEST(test_stdin_with_instructions);
    RUN_TEST(test_stdin_large_content_truncation);
    
    /* Clean up */
    teardown_test_env();
    
    PRINT_TEST_SUMMARY();
}
