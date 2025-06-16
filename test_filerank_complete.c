#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test FileRank integration with budget handling
 * This creates several files and tests that FileRank properly selects
 * the most relevant files when budget is limited.
 */

void create_test_files(void) {
    FILE *f;
    
    /* High relevance file - many query matches */
    f = fopen("high_relevance.c", "w");
    if (f) {
        fprintf(f, "/* File with main function implementation */\n");
        fprintf(f, "int main(int argc, char *argv[]) {\n");
        fprintf(f, "    // Main function body\n");
        fprintf(f, "    printf(\"Main function running\\n\");\n");
        fprintf(f, "    return 0;\n");
        fprintf(f, "}\n");
        fclose(f);
    }
    
    /* Medium relevance file */
    f = fopen("medium_relevance.c", "w");
    if (f) {
        fprintf(f, "/* Helper functions */\n");
        fprintf(f, "void helper_main(void) {\n");
        fprintf(f, "    // Not the main function\n");
        fprintf(f, "}\n");
        fclose(f);
    }
    
    /* Low relevance file - no matches */
    f = fopen("low_relevance.c", "w");
    if (f) {
        fprintf(f, "/* Unrelated utilities */\n");
        fprintf(f, "void utility(void) {\n");
        fprintf(f, "    // No relevance to query\n");
        fprintf(f, "}\n");
        fclose(f);
    }
}

int main(void) {
    printf("Testing FileRank with budget constraints\n");
    
    create_test_files();
    
    /* Test 1: Run with debug to see ranking */
    printf("\n=== Test 1: FileRank scoring ===\n");
    system("./llm_ctx --filerank-debug -c \"main function\" -f high_relevance.c medium_relevance.c low_relevance.c -o 2>&1 | grep -A 5 FileRank");
    
    /* Test 2: Run with very small budget */
    printf("\n=== Test 2: Small budget (should select only top files) ===\n");
    system("./llm_ctx -c \"main function\" -f high_relevance.c medium_relevance.c low_relevance.c -b 50 -o 2>&1 | head -30");
    
    /* Cleanup */
    system("rm -f high_relevance.c medium_relevance.c low_relevance.c");
    
    return 0;
}