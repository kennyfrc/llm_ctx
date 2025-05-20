#ifndef TREE_SITTER_H
#define TREE_SITTER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* Basic Tree-sitter types */
typedef struct TSParser TSParser;
typedef struct TSTree TSTree;
typedef struct TSLanguage TSLanguage;

typedef struct {
    const void *tree;
    const void *id;
    uint32_t context[4];
} TSNode;

/* Both JavaScript and TypeScript language entry points */
extern const TSLanguage *tree_sitter_javascript(void);
extern const TSLanguage *tree_sitter_typescript(void);
extern const TSLanguage *tree_sitter_tsx(void);

/* Tree-sitter API declarations */
extern TSParser *ts_parser_new(void);
extern void ts_parser_delete(TSParser *parser);
extern bool ts_parser_set_language(TSParser *parser, const TSLanguage *language);

extern TSTree *ts_parser_parse_string(TSParser *parser, const TSTree *old_tree,
                                      const char *string, uint32_t length);
extern void ts_tree_delete(TSTree *tree);
extern TSNode ts_tree_root_node(const TSTree *tree);

/* Node inspection functions */
extern uint32_t ts_node_child_count(TSNode node);
extern TSNode ts_node_child(TSNode node, uint32_t index);
extern const char *ts_node_type(TSNode node);
extern uint32_t ts_node_start_byte(TSNode node);
extern uint32_t ts_node_end_byte(TSNode node);
extern bool ts_node_is_null(TSNode node);

/* Field inspection functions */
extern TSNode ts_node_child_by_field_name(TSNode node, const char *field_name, uint32_t field_name_length);
extern TSNode ts_node_named_child(TSNode node, uint32_t index);
extern uint32_t ts_node_named_child_count(TSNode node);

#endif /* TREE_SITTER_H */