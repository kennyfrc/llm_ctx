#include <stdio.h>
typedef struct TSLanguage {
    const char *name;
    int node_count;
    void *state;
} TSLanguage;

const TSLanguage *tree_sitter_javascript(void) {
    static TSLanguage lang = {"javascript", 100, NULL};
    printf("Mock JavaScript grammar loaded\n");
    return &lang;
}
