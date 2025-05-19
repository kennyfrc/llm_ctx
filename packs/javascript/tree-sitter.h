#ifndef TREE_SITTER_H_
#define TREE_SITTER_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/**
 * Minimal necessary Tree-sitter API definitions to use the tree-sitter-javascript library
 */

// Define as proper struct instead of opaque pointers
typedef struct TSParser TSParser;
typedef struct TSTree TSTree;

// Define TSNode as a struct
typedef struct {
    const void *tree;
    const void *id;
    uint32_t context[4];
} TSNode;

// Define TSTreeCursor
typedef struct {
    TSNode node;
    uint32_t context[2];
} TSTreeCursor;

// Language definition
typedef struct TSLanguage TSLanguage;

// Point type (row/column)
typedef struct {
    uint32_t row;
    uint32_t column;
} TSPoint;

// Function declarations for the Tree-sitter JavaScript library
extern const TSLanguage *tree_sitter_javascript(void);

// Parser API
extern TSParser *ts_parser_new(void);
extern void ts_parser_delete(TSParser *self);
extern bool ts_parser_set_language(TSParser *self, const TSLanguage *language);
extern TSTree *ts_parser_parse_string(TSParser *self, const TSTree *old_tree, const char *string, uint32_t length);

// Tree API
extern void ts_tree_delete(TSTree *self);
extern TSNode ts_tree_root_node(const TSTree *self);

// Node API
extern uint32_t ts_node_start_byte(TSNode self);
extern uint32_t ts_node_end_byte(TSNode self);
extern bool ts_node_is_null(TSNode self);
extern const char *ts_node_type(TSNode self);
extern uint32_t ts_node_child_count(TSNode self);
extern TSNode ts_node_child(TSNode self, uint32_t index);

// TreeCursor API
extern TSTreeCursor ts_tree_cursor_new(TSNode node);
extern void ts_tree_cursor_delete(TSTreeCursor *cursor);
extern TSNode ts_tree_cursor_current_node(const TSTreeCursor *cursor);
extern bool ts_tree_cursor_goto_first_child(TSTreeCursor *cursor);
extern bool ts_tree_cursor_goto_next_sibling(TSTreeCursor *cursor);
extern bool ts_tree_cursor_goto_parent(TSTreeCursor *cursor);

#endif /* TREE_SITTER_H_ */