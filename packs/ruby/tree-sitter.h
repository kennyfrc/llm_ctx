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

/* Ruby language's tree-sitter function */
extern const TSLanguage *tree_sitter_ruby(void);

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
extern TSNode ts_node_named_child(TSNode node, uint32_t index);
extern uint32_t ts_node_named_child_count(TSNode node);

/* Node field API */
extern TSNode ts_node_child_by_field_name(TSNode self, const char *field_name, uint32_t field_name_length);

/* Query API */
typedef struct TSQuery TSQuery;
typedef struct TSQueryCursor TSQueryCursor;

typedef enum {
    TSQueryErrorNone = 0,
    TSQueryErrorSyntax,
    TSQueryErrorNodeType,
    TSQueryErrorField,
    TSQueryErrorCapture,
    TSQueryErrorStructure,
    TSQueryErrorLanguage
} TSQueryError;

typedef struct {
    TSNode node;
    uint32_t index;
} TSQueryCapture;

typedef struct {
    uint32_t id;
    uint16_t pattern_index;
    uint16_t capture_count;
    const TSQueryCapture *captures;
} TSQueryMatch;

extern TSQuery *ts_query_new(const TSLanguage *language, const char *source, uint32_t source_len,
                            uint32_t *error_offset, TSQueryError *error_type);
extern void ts_query_delete(TSQuery *self);
extern uint32_t ts_query_pattern_count(const TSQuery *self);
extern uint32_t ts_query_capture_count(const TSQuery *self);
extern const char *ts_query_capture_name_for_id(const TSQuery *self, uint32_t index, uint32_t *length);
extern const char *ts_query_predicates_for_pattern(const TSQuery *self, uint32_t pattern_index, uint32_t *step_count);

extern TSQueryCursor *ts_query_cursor_new(void);
extern void ts_query_cursor_delete(TSQueryCursor *self);
extern void ts_query_cursor_exec(TSQueryCursor *self, const TSQuery *query, TSNode node);
extern void ts_query_cursor_set_byte_range(TSQueryCursor *self, uint32_t start, uint32_t end);
extern bool ts_query_cursor_next_match(TSQueryCursor *self, TSQueryMatch *match);
extern void ts_query_cursor_remove_match(TSQueryCursor *self, uint32_t match_id);
extern bool ts_query_cursor_next_capture(TSQueryCursor *self, TSQueryMatch *match, uint32_t *capture_index);

#endif /* TREE_SITTER_H */