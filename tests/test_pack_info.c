#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/**
 * Test the --pack-info command line option
 * 
 * This test verifies that the --pack-info option correctly displays
 * information about a specified language pack.
 */
static void test_pack_info_command(void) {
    printf("Testing --pack-info command...\n");
    
    // Test 1: Valid language pack (JavaScript)
    printf("Test 1: --pack-info for JavaScript pack... ");
    
    pid_t child_pid = fork();
    if (child_pid == 0) {
        // Child process
        // Redirect stdout to /dev/null to avoid flooding test output
        freopen("/dev/null", "w", stdout);
        
        // Execute the llm_ctx command with --pack-info
        execl("./llm_ctx", "./llm_ctx", "--pack-info", "javascript", NULL);
        
        // If execl returns, it failed
        perror("execl failed");
        exit(EXIT_FAILURE);
    } else if (child_pid > 0) {
        // Parent process
        int status;
        waitpid(child_pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("PASS (Command executed successfully)\n");
        } else {
            printf("FAIL (Command failed with status %d)\n", WEXITSTATUS(status));
        }
    } else {
        // Fork failed
        perror("fork failed");
        printf("FAIL (Could not fork process)\n");
    }
    
    // Test 2: Invalid language pack
    printf("Test 2: --pack-info for non-existent pack... ");
    
    child_pid = fork();
    if (child_pid == 0) {
        // Child process
        // Redirect stdout to /dev/null to avoid flooding test output
        freopen("/dev/null", "w", stdout);
        
        // Execute the llm_ctx command with --pack-info for non-existent pack
        execl("./llm_ctx", "./llm_ctx", "--pack-info", "nonexistent_pack", NULL);
        
        // If execl returns, it failed
        perror("execl failed");
        exit(EXIT_FAILURE);
    } else if (child_pid > 0) {
        // Parent process
        int status;
        waitpid(child_pid, &status, 0);
        
        // The command should still exit with 0 even if the pack is not found
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("PASS (Command executed successfully with non-existent pack)\n");
        } else {
            printf("FAIL (Command failed with status %d)\n", WEXITSTATUS(status));
        }
    } else {
        // Fork failed
        perror("fork failed");
        printf("FAIL (Could not fork process)\n");
    }
    
    // Test 3: --pack-info without argument
    printf("Test 3: --pack-info without argument... ");
    
    child_pid = fork();
    if (child_pid == 0) {
        // Child process
        // Redirect stdout and stderr to /dev/null to avoid flooding test output
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        
        // Execute the llm_ctx command with --pack-info without argument
        execl("./llm_ctx", "./llm_ctx", "--pack-info", NULL);
        
        // If execl returns, it failed
        perror("execl failed");
        exit(EXIT_FAILURE);
    } else if (child_pid > 0) {
        // Parent process
        int status;
        waitpid(child_pid, &status, 0);
        
        // The command should fail with non-zero exit code when no argument is provided
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            printf("PASS (Command correctly failed when no argument provided)\n");
        } else {
            printf("FAIL (Command unexpectedly succeeded with no argument)\n");
        }
    } else {
        // Fork failed
        perror("fork failed");
        printf("FAIL (Could not fork process)\n");
    }
}

/**
 * Test the --list-packs command line option
 * 
 * This test verifies that the --list-packs option correctly lists
 * all available language packs.
 */
static void test_list_packs_command(void) {
    printf("Testing --list-packs command...\n");
    
    pid_t child_pid = fork();
    if (child_pid == 0) {
        // Child process
        // Redirect stdout to /dev/null to avoid flooding test output
        freopen("/dev/null", "w", stdout);
        
        // Execute the llm_ctx command with --list-packs
        execl("./llm_ctx", "./llm_ctx", "--list-packs", NULL);
        
        // If execl returns, it failed
        perror("execl failed");
        exit(EXIT_FAILURE);
    } else if (child_pid > 0) {
        // Parent process
        int status;
        waitpid(child_pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("PASS (Command executed successfully)\n");
        } else {
            printf("FAIL (Command failed with status %d)\n", WEXITSTATUS(status));
        }
    } else {
        // Fork failed
        perror("fork failed");
        printf("FAIL (Could not fork process)\n");
    }
}

/**
 * Main function - run tests
 */
int main(void) {
    printf("Running pack info CLI tests...\n");
    
    // Run tests
    test_pack_info_command();
    test_list_packs_command();
    
    printf("Pack info CLI tests completed.\n");
    return 0;
}