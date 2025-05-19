#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/**
 * Run a command and check its exit status
 * 
 * Returns true if the command executed successfully, false otherwise
 */
static bool run_command(const char *command) {
    printf("Running command: %s\n", command);
    int result = system(command);
    
    if (result == -1) {
        perror("Failed to execute command");
        return false;
    }
    
    // WEXITSTATUS returns the exit status of the command
    int exit_status = WEXITSTATUS(result);
    if (exit_status != 0) {
        fprintf(stderr, "Command failed with exit status %d\n", exit_status);
        return false;
    }
    
    return true;
}

/**
 * Test that the 'make pack' command can be run
 */
static void test_make_pack_command(void) {
    printf("Testing that 'make pack' command exists... ");
    
    // Check if the make command is available
    if (!run_command("make -n pack LANG=test_lang > /dev/null 2>&1")) {
        printf("FAIL (make command failed)\n");
        return;
    }
    
    printf("PASS (make pack command exists)\n");
}

/**
 * Main function - run tests
 */
int main(void) {
    printf("Running make pack tests...\n");
    
    // Run the test
    test_make_pack_command();
    
    printf("make pack tests completed.\n");
    return 0;
}