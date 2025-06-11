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

/* Define sizes relative to the 8MB buffer */
#define STDIN_BUFFER_SIZE_ACTUAL (8 * 1024 * 1024)
#define FITS_CONTENT_SIZE (STDIN_BUFFER_SIZE_ACTUAL - 1024) /* Slightly less than 8MB */

#define EXCEEDS_CONTENT_SIZE (STDIN_BUFFER_SIZE_ACTUAL + 100) /* Slightly more than 8MB */
#define FITS_CONTENT_FILE TEST_DIR "/__fits_content.txt" // Prefixed
#define EXCEEDS_CONTENT_FILE TEST_DIR "/__exceeds_content.txt" // Prefixed
#define TRUNCATION_MARKER "---END_OF_INPUT_MARKER---"
#define TRUNCATION_WARNING "Warning: Standard input exceeded buffer size"

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

    /* Files for binary detection tests (prefixed) */
    f = fopen(TEST_DIR "/__binary_null.bin", "wb"); // Use "wb" for binary write
    if (f) { fwrite("stdin\0test", 1, 10, f); fclose(f); } // Include null byte

    f = fopen(TEST_DIR "/__binary_control.bin", "w");
    if (f) { fprintf(f, "stdin\x01\x02\x03test"); fclose(f); } // Non-printable ASCII

    f = fopen(TEST_DIR "/__image.png", "wb"); // Use "wb"
    if (f) { fwrite("\x89PNG\r\n\x1a\n", 1, 8, f); fclose(f); } // PNG magic bytes

    f = fopen(TEST_DIR "/__empty.txt", "w"); // Empty file
    if (f) { fclose(f); }

    f = fopen(TEST_DIR "/__utf8.txt", "w"); // UTF-8 content
    if (f) { fprintf(f, "Stdin 你好 Test"); fclose(f); }
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
    /* Increased buffer size significantly to handle large output from llm_ctx (up to ~8MB + overhead) */
    static char buffer[10 * 1024 * 1024]; /* 10MB */
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
    /* Redirect stderr to stdout to capture warnings */
    fprintf(script, "%s | %s 2>&1\n", input_cmd, cmd);
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
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -o", getenv("PWD"));
    
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
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -o", getenv("PWD"));
    
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
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -o", getenv("PWD"));
    
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
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -o", getenv("PWD"));
    
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
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -o -f %s/__sample.json %s/__sample.md %s/__sample.xml",
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
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -o -c \"Test instruction\"", getenv("PWD"));
    
    char *output = run_command_with_stdin(input_cmd, cmd);
    
    /* Check for proper formatting */
    ASSERT("Output contains user instructions", string_contains(output, "<user_instructions>"));
    ASSERT("Output contains the instruction", string_contains(output, "Test instruction"));
    ASSERT("Output contains stdin_content header", string_contains(output, "File: stdin_content"));
}

/* Test stdin processing when input fits entirely within the buffer */
TEST(test_stdin_content_fits_in_buffer) {
    /* 1. Create the file that fits */
    FILE *fits_file = fopen(FITS_CONTENT_FILE, "w");
    ASSERT("Failed to create fits content file", fits_file != NULL);
    if (!fits_file) return;

    /* Write repeating pattern */
    size_t bytes_to_write = FITS_CONTENT_SIZE - strlen(TRUNCATION_MARKER);
    const char pattern[] = "0123456789ABCDEF";
    size_t pattern_len = strlen(pattern);
    for (size_t i = 0; i < bytes_to_write; ++i) {
        fputc(pattern[i % pattern_len], fits_file);
    }
    /* Write the marker at the very end */
    fprintf(fits_file, "%s", TRUNCATION_MARKER);
    fclose(fits_file);

    /* 2. Run llm_ctx with the file piped to stdin */
    char cmd[1024];
    char input_cmd[1024];
    snprintf(input_cmd, sizeof(input_cmd), "cat %s", FITS_CONTENT_FILE);
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -o", getenv("PWD"));
    char *output = run_command_with_stdin(input_cmd, cmd);

    /* 3. Assertions for content fitting */
    ASSERT("Output contains stdin_content header", string_contains(output, "File: stdin_content"));
    ASSERT("Output contains start of content", string_contains(output, "0123456789ABCDEF0123"));
    /* The marker SHOULD be present as the input fits */
    ASSERT("Output CONTAINS the end marker (fits)", string_contains(output, TRUNCATION_MARKER));
    ASSERT("Output contains closing fence", string_contains(output, "```\n----------------------------------------"));
    /* The warning message should NOT be present */
    ASSERT("Output does NOT contain truncation warning", !string_contains(output, TRUNCATION_WARNING));
}

/* Test stdin processing when input exceeds the buffer, causing truncation */
TEST(test_stdin_content_exceeds_buffer_truncation) {
    /* 1. Create the file that exceeds the buffer */
    FILE *exceeds_file = fopen(EXCEEDS_CONTENT_FILE, "w");
    ASSERT("Failed to create exceeds content file", exceeds_file != NULL);
    if (!exceeds_file) return;

    /* Write repeating pattern */
    size_t bytes_to_write = EXCEEDS_CONTENT_SIZE - strlen(TRUNCATION_MARKER);
    const char pattern[] = "FEDCBA9876543210";
    size_t pattern_len = strlen(pattern);
    for (size_t i = 0; i < bytes_to_write; ++i) {
        fputc(pattern[i % pattern_len], exceeds_file);
    }
    /* Write the marker at the very end */
    fprintf(exceeds_file, "%s", TRUNCATION_MARKER);
    fclose(exceeds_file);

    /* 2. Run llm_ctx with the large file piped to stdin */
    char cmd[1024];
    char input_cmd[1024];
    snprintf(input_cmd, sizeof(input_cmd), "cat %s", EXCEEDS_CONTENT_FILE);
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -o", getenv("PWD"));
    char *output = run_command_with_stdin(input_cmd, cmd);

    /* 3. Assertions for truncation */
    ASSERT("Output contains stdin_content header", string_contains(output, "File: stdin_content"));
    ASSERT("Output contains start of large content", string_contains(output, "FEDCBA9876543210FEDC"));
    /* The marker should NOT be present as the input was truncated */
    ASSERT("Output does NOT contain the end marker (truncated)", !string_contains(output, TRUNCATION_MARKER));
    ASSERT("Output contains closing fence", string_contains(output, "```\n----------------------------------------"));
    /* The warning message SHOULD be present */
    ASSERT("Output CONTAINS truncation warning", string_contains(output, TRUNCATION_WARNING));
}


/* Test stdin with null bytes (current behavior: include raw, potentially truncated) */
TEST(test_stdin_binary_null_byte) {
    char cmd[1024];
    char input_cmd[1024];
    snprintf(input_cmd, sizeof(input_cmd), "cat %s/__binary_null.bin", TEST_DIR);
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -o", getenv("PWD"));
    char *output = run_command_with_stdin(input_cmd, cmd);

    // Desired expectation: Header and placeholder, no raw content.
    ASSERT("Output contains stdin_content header", string_contains(output, "File: stdin_content"));
    ASSERT("Output contains binary skipped placeholder", string_contains(output, "[Binary file content skipped]"));
    /* Removed redundant/misleading check for raw null byte content, as string_contains(strstr) stops at null. */
    /* The presence of the placeholder and absence of fences are sufficient checks. */
    ASSERT("Output does NOT contain code fences for binary", !string_contains(output, "```"));
}

/* Test stdin with control characters (current behavior: include raw) */
TEST(test_stdin_binary_control_chars) {
    char cmd[1024];
    char input_cmd[1024];
    snprintf(input_cmd, sizeof(input_cmd), "cat %s/__binary_control.bin", TEST_DIR);
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -o", getenv("PWD"));
    char *output = run_command_with_stdin(input_cmd, cmd);

    // Desired expectation: Header and placeholder, no raw content.
    ASSERT("Output contains stdin_content header", string_contains(output, "File: stdin_content"));
    ASSERT("Output contains binary skipped placeholder", string_contains(output, "[Binary file content skipped]"));
    ASSERT("Output does NOT contain raw control char content", !string_contains(output, "stdin\x01\x02\x03test"));
    ASSERT("Output does NOT contain code fences for binary", !string_contains(output, "```"));
}

/* Test stdin with image magic bytes (current behavior: include raw) */
TEST(test_stdin_binary_image_magic) {
    char cmd[1024];
    char input_cmd[1024];
    snprintf(input_cmd, sizeof(input_cmd), "cat %s/__image.png", TEST_DIR);
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -o", getenv("PWD"));
    char *output = run_command_with_stdin(input_cmd, cmd);

    // Desired expectation: Header and placeholder, no raw content.
    ASSERT("Output contains stdin_content header", string_contains(output, "File: stdin_content"));
    ASSERT("Output contains binary skipped placeholder", string_contains(output, "[Binary file content skipped]"));
    ASSERT("Output does NOT contain raw PNG magic bytes", !string_contains(output, "\x89PNG\r\n\x1a\n"));
    ASSERT("Output does NOT contain code fences for binary", !string_contains(output, "```"));
}

/* Test stdin with empty input (current behavior: no output, error message) */
TEST(test_stdin_empty_file) {
    char cmd[1024];
    char input_cmd[1024];
    snprintf(input_cmd, sizeof(input_cmd), "cat %s/__empty.txt", TEST_DIR);
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -o", getenv("PWD"));
    char *output = run_command_with_stdin(input_cmd, cmd);

    // Desired expectation: Output the header and empty fences, similar to an empty file via -f.
    ASSERT("Expected 'File: stdin_content' header for empty input", string_contains(output, "File: stdin_content"));
    ASSERT("Expected empty code block '```\\n```' for empty input", string_contains(output, "```\n```"));
    ASSERT("Expected separator after empty block", string_contains(output, "```\n----------------------------------------"));
}

/* Test stdin with UTF-8 characters (current behavior: include raw) */
TEST(test_stdin_utf8_file) {
    char cmd[1024];
    char input_cmd[1024];
    snprintf(input_cmd, sizeof(input_cmd), "cat %s/__utf8.txt", TEST_DIR);
    snprintf(cmd, sizeof(cmd), "%s/llm_ctx -o", getenv("PWD"));
    char *output = run_command_with_stdin(input_cmd, cmd);

    ASSERT("Output contains stdin_content header", string_contains(output, "File: stdin_content"));
    // Current expectation: Raw UTF-8 content is included.
    ASSERT("Output contains UTF-8 characters from stdin", string_contains(output, "Stdin 你好 Test"));
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
    RUN_TEST(test_stdin_content_fits_in_buffer); /* Renamed test */
    RUN_TEST(test_stdin_content_exceeds_buffer_truncation); /* New test */
    RUN_TEST(test_stdin_binary_null_byte);
    RUN_TEST(test_stdin_binary_control_chars);
    RUN_TEST(test_stdin_binary_image_magic);
    RUN_TEST(test_stdin_empty_file);
    RUN_TEST(test_stdin_utf8_file);

    /* Clean up */
    teardown_test_env();
    
    PRINT_TEST_SUMMARY();
}
