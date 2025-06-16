#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test that file tree is included when FileRank budget selection triggers */

int main(void) {
    printf("Testing file tree inclusion with FileRank budget selection\n");
    printf("=======================================================\n\n");
    
    /* Create test directory and files */
    system("rm -rf test_filerank_tree && mkdir test_filerank_tree");
    system("echo 'test content' > test_filerank_tree/file1.txt");
    system("echo 'test content' > test_filerank_tree/file2.txt");
    
    /* Create large files to exceed budget */
    system("dd if=/dev/zero bs=1024 count=50 2>/dev/null | tr '\\0' 'x' > test_filerank_tree/large1.txt");
    system("echo 'This file has test content' >> test_filerank_tree/large1.txt");
    system("dd if=/dev/zero bs=1024 count=50 2>/dev/null | tr '\\0' 'y' > test_filerank_tree/large2.txt");
    
    printf("Test 1: Without file tree (-f only)\n");
    printf("-----------------------------------\n");
    system("./llm_ctx -c 'test' -f test_filerank_tree/file1.txt test_filerank_tree/file2.txt -o 2>&1 | grep -E '(file_tree|<file_context>|FileRank)' | head -10");
    
    printf("\n\nTest 2: With file tree (-t flag)\n");
    printf("---------------------------------\n");
    system("./llm_ctx -t -c 'test' -f test_filerank_tree/file1.txt test_filerank_tree/file2.txt -o 2>&1 | grep -E '(file_tree|test_filerank_tree)' | head -10");
    
    printf("\n\nTest 3: With budget limit to trigger FileRank (if tokenizer available)\n");
    printf("----------------------------------------------------------------------\n");
    system("./llm_ctx -t -c 'test' -f test_filerank_tree/large1.txt test_filerank_tree/large2.txt -b 50 --filerank-debug -o 2>&1 | grep -E '(file_tree|Budget|FileRank|test_filerank_tree)' | head -20");
    
    /* Cleanup */
    system("rm -rf test_filerank_tree");
    
    printf("\n\nTest complete!\n");
    return 0;
}