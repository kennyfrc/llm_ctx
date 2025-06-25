/*
 * Minimal TOML parser implementation for llm_ctx config loading
 * This is a simplified implementation that supports only the features we need:
 * - String values
 * - Boolean values  
 * - Integer values
 * - No nested tables, arrays, or other complex types
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include "toml.h"

#define MAX_LINE 1024
#define MAX_KEY 256
#define MAX_VALUE 1024

struct toml_table_t {
    struct {
        char *key;
        char *value;
    } *entries;
    int count;
    int capacity;
};

static void skip_whitespace(const char **p) {
    while (**p && isspace(**p)) (*p)++;
}

static char *parse_string(const char *input, char *errbuf, int errbufsz) {
    const char *p = input;
    skip_whitespace(&p);
    
    if (*p != '"') {
        snprintf(errbuf, errbufsz, "Expected string to start with \"");
        return NULL;
    }
    p++; // skip opening quote
    
    const char *start = p;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p+1)) {
            p += 2; // skip escape sequence
        } else {
            p++;
        }
    }
    
    if (*p != '"') {
        snprintf(errbuf, errbufsz, "Unterminated string");
        return NULL;
    }
    
    size_t len = p - start;
    char *result = malloc(len + 1);
    if (!result) return NULL;
    
    // Simple string copy without escape processing for now
    memcpy(result, start, len);
    result[len] = '\0';
    
    return result;
}

toml_table_t *toml_parse_file(FILE *fp, char *errbuf, int errbufsz) {
    toml_table_t *tab = calloc(1, sizeof(*tab));
    if (!tab) {
        snprintf(errbuf, errbufsz, "Out of memory");
        return NULL;
    }
    
    tab->capacity = 16;
    tab->entries = calloc(tab->capacity, sizeof(tab->entries[0]));
    if (!tab->entries) {
        free(tab);
        snprintf(errbuf, errbufsz, "Out of memory");
        return NULL;
    }
    
    char line[MAX_LINE];
    int line_no = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        line_no++;
        
        // Remove newline
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        
        // Skip empty lines and comments
        const char *p = line;
        skip_whitespace(&p);
        if (!*p || *p == '#') continue;
        
        // Find equals sign
        char *eq = strchr(p, '=');
        if (!eq) {
            snprintf(errbuf, errbufsz, "Line %d: No '=' found", line_no);
            toml_free(tab);
            return NULL;
        }
        
        // Extract key
        char key[MAX_KEY];
        size_t key_len = eq - p;
        if (key_len >= MAX_KEY) {
            snprintf(errbuf, errbufsz, "Line %d: Key too long", line_no);
            toml_free(tab);
            return NULL;
        }
        memcpy(key, p, key_len);
        key[key_len] = '\0';
        
        // Trim key
        char *key_end = key + key_len - 1;
        while (key_end > key && isspace(*key_end)) key_end--;
        *(key_end + 1) = '\0';
        
        // Parse value
        p = eq + 1;
        skip_whitespace(&p);
        
        // Store key-value pair
        if (tab->count >= tab->capacity) {
            // Expand capacity
            int new_capacity = tab->capacity * 2;
            void *new_entries = realloc(tab->entries, new_capacity * sizeof(tab->entries[0]));
            if (!new_entries) {
                snprintf(errbuf, errbufsz, "Out of memory");
                toml_free(tab);
                return NULL;
            }
            tab->entries = new_entries;
            memset(&tab->entries[tab->capacity], 0, (new_capacity - tab->capacity) * sizeof(tab->entries[0]));
            tab->capacity = new_capacity;
        }
        
        tab->entries[tab->count].key = strdup(key);
        tab->entries[tab->count].value = strdup(p);
        if (!tab->entries[tab->count].key || !tab->entries[tab->count].value) {
            snprintf(errbuf, errbufsz, "Out of memory");
            toml_free(tab);
            return NULL;
        }
        tab->count++;
    }
    
    return tab;
}

void toml_free(toml_table_t *tab) {
    if (!tab) return;
    
    for (int i = 0; i < tab->count; i++) {
        free(tab->entries[i].key);
        free(tab->entries[i].value);
    }
    free(tab->entries);
    free(tab);
}

toml_datum_t toml_string_in(const toml_table_t *tab, const char *key) {
    toml_datum_t d = {0};
    
    for (int i = 0; i < tab->count; i++) {
        if (strcmp(tab->entries[i].key, key) == 0) {
            const char *val = tab->entries[i].value;
            
            // Check if it's a quoted string
            if (*val == '"') {
                char errbuf[256];
                char *str = parse_string(val, errbuf, sizeof(errbuf));
                if (str) {
                    d.ok = 1;
                    d.u.s = str;
                }
            }
            return d;
        }
    }
    
    return d;
}

toml_datum_t toml_bool_in(const toml_table_t *tab, const char *key) {
    toml_datum_t d = {0};
    
    for (int i = 0; i < tab->count; i++) {
        if (strcmp(tab->entries[i].key, key) == 0) {
            const char *val = tab->entries[i].value;
            
            if (strcmp(val, "true") == 0) {
                d.ok = 1;
                d.u.b = 1;
            } else if (strcmp(val, "false") == 0) {
                d.ok = 1;
                d.u.b = 0;
            }
            return d;
        }
    }
    
    return d;
}

toml_datum_t toml_int_in(const toml_table_t *tab, const char *key) {
    toml_datum_t d = {0};
    
    for (int i = 0; i < tab->count; i++) {
        if (strcmp(tab->entries[i].key, key) == 0) {
            const char *val = tab->entries[i].value;
            char *endptr;
            
            errno = 0;
            long long num = strtoll(val, &endptr, 10);
            
            if (errno == 0 && *endptr == '\0' && num >= LLONG_MIN && num <= LLONG_MAX) {
                d.ok = 1;
                d.u.i = num;
            }
            return d;
        }
    }
    
    return d;
}