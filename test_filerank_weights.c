#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Test --filerank-weight CLI option
void test_filerank_weight_cli() {
    printf("Testing --filerank-weight CLI option...\n");
    
    // Create test files
    system("mkdir -p test_weight_dir");
    system("echo 'This file contains search algorithm code' > test_weight_dir/search.c");
    system("echo 'This file contains utility functions' > test_weight_dir/utils.c");
    system("echo 'Large file with lots of content' > test_weight_dir/large.txt");
    for (int i = 0; i < 100; i++) {
        system("echo 'More content to make this file larger' >> test_weight_dir/large.txt");
    }
    
    // Test with custom weights (high path weight, low size penalty)
    FILE *fp = popen("./llm_ctx -f test_weight_dir/*.c test_weight_dir/*.txt -c 'search' -b 50 --filerank-weight path:10,content:1,size:0.01,tfidf:5 --filerank-debug 2>&1", "r");
    assert(fp != NULL);
    
    char buffer[4096];
    int found_debug = 0;
    int found_search_first = 0;
    char first_file[256] = {0};
    
    while (fgets(buffer, sizeof(buffer), fp)) {
        if (strstr(buffer, "FileRank (query:")) {
            found_debug = 1;
        }
        if (found_debug && !first_file[0] && strstr(buffer, "test_weight_dir/")) {
            // Extract filename from debug output
            char *start = strstr(buffer, "test_weight_dir/");
            if (start) {
                sscanf(start, "%255s", first_file);
                // Remove trailing characters
                char *end = strchr(first_file, ' ');
                if (end) *end = '\0';
                if (strstr(first_file, "search.c")) {
                    found_search_first = 1;
                }
            }
        }
    }
    
    pclose(fp);
    
    assert(found_debug && "FileRank debug output should be present");
    assert(found_search_first && "search.c should rank first with high path weight");
    
    // Clean up
    system("rm -rf test_weight_dir");
    
    printf("✓ --filerank-weight CLI option works correctly\n");
}

// Test config file FileRank weights
void test_filerank_weight_config() {
    printf("Testing FileRank weight configuration from config file...\n");
    
    // Create test config (without inline comments due to minimal TOML parser limitations)
    system("mkdir -p ~/.config/llm_ctx");
    FILE *config = fopen("/tmp/test_filerank_config_clean.toml", "w");
    assert(config != NULL);
    
    fprintf(config, "token_budget = 48000\n");
    fprintf(config, "filerank_weight_path_x100 = 1000\n");
    fprintf(config, "filerank_weight_content_x100 = 100\n");
    fprintf(config, "filerank_weight_size_x100 = 1\n");
    fprintf(config, "filerank_weight_tfidf_x100 = 500\n");
    fclose(config);
    
    // Create test files
    system("mkdir -p test_config_dir");
    system("echo 'This file contains algorithm code' > test_config_dir/algorithm.c");
    system("echo 'This file contains other code' > test_config_dir/other.c");
    
    // Test with config file (use the clean version without inline comments)
    FILE *fp = popen("LLM_CTX_CONFIG=/tmp/test_filerank_config_clean.toml ./llm_ctx -f test_config_dir/*.c -c 'algorithm' -b 50 --filerank-debug -d 2>&1", "r");
    assert(fp != NULL);
    
    char buffer[4096];
    int found_filerank_output = 0;
    int found_custom_weights = 0;
    
    while (fgets(buffer, sizeof(buffer), fp)) {
        if (strstr(buffer, "FileRank (query:")) {
            found_filerank_output = 1;
        }
        // Check if any weight is different from default (we know path weight should be 10.0)
        if (strstr(buffer, "filerank_weight_path: 10.00") || 
            strstr(buffer, "filerank_weight_tfidf: 5.00")) {
            found_custom_weights = 1;
        }
    }
    
    pclose(fp);
    
    assert(found_filerank_output && "FileRank should run with config");
    assert(found_custom_weights && "Custom weights should be loaded from config");
    
    // Clean up
    system("rm -rf test_config_dir");
    system("rm -f /tmp/test_filerank_config_clean.toml");
    
    printf("✓ FileRank weight config file loading works correctly\n");
}

int main() {
    printf("=== FileRank Weight Configuration Tests ===\n");
    
    test_filerank_weight_cli();
    test_filerank_weight_config();
    
    printf("\nAll tests passed! ✓\n");
    return 0;
}