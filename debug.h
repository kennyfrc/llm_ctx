#ifndef DEBUG_H
#define DEBUG_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Global debug mode flag */
extern bool debug_mode;

/**
 * Debug print function that only prints if debug mode is enabled.
 * Prefixes all output with "[DEBUG] ".
 */
static inline void debug_printf(const char *fmt, ...) {
    /* These are messages we want to suppress in non-debug mode */
    const char *suppress_msgs[] = {
        "Initializing JavaScript language pack",
        "Initializing Ruby language pack",
        "Cleaning up JavaScript language pack",
        "Cleaning up Ruby language pack",
        "Parsing JavaScript/TypeScript file",
        "Parsing Ruby file",
        "Successfully extracted",
        "Codemap option enabled",
        "Generating codemap",
        "Generating codemap with",
        "Pattern ",
        "Loaded ",
        "Recursively scanning",
        "Searching for ",
        "Matched pattern",
        "Processing ",
        "Success: Parsed",
        "Codemap generation complete",
        "Successfully built codemap",
        "Codemap generated successfully"
    };
    
    /* Check if this message should be suppressed in non-debug mode */
    if (!debug_mode) {
        for (size_t i = 0; i < sizeof(suppress_msgs) / sizeof(suppress_msgs[0]); i++) {
            if (strstr(fmt, suppress_msgs[i]) != NULL) {
                return; /* Just exit, don't print anything */
            }
        }
    }
    
    /* If we get here, either debug mode is on, or this is a message we always want to show */
    va_list ap;
    va_start(ap, fmt);
    
    if (debug_mode) {
        /* In debug mode, prefix with [DEBUG] */
        fprintf(stderr, "[DEBUG] ");
        vfprintf(stderr, fmt, ap);
        if (fmt[0] != '\0' && fmt[strlen(fmt) - 1] != '\n') {
            fputc('\n', stderr);
        }
    } else {
        /* In normal mode, just print the message */
        vfprintf(stderr, fmt, ap);
        if (fmt[0] != '\0' && fmt[strlen(fmt) - 1] != '\n') {
            fputc('\n', stderr);
        }
    }
    
    va_end(ap);
}

#endif /* DEBUG_H */