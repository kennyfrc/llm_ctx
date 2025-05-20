#ifndef DEBUG_H
#define DEBUG_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Global debug mode flag */
extern bool debug_mode;

/**
 * Debug print function that only prints if debug mode is enabled.
 * Prefixes all output with "[DEBUG] " in debug mode.
 * In all modes, certain repetitive initialization messages are suppressed.
 */
static inline void debug_printf(const char *fmt, ...) {
    /* Messages to always suppress regardless of debug mode */
    const char *always_suppress[] = {
        "[DEBUG] Initializing language pack: ",
        "[DEBUG] Cleaning up language pack: ",
        "[DEBUG] Parsing file with language pack: "
    };
    
    /* Messages to suppress only in non-debug mode */
    const char *non_debug_suppress[] = {
        /* Don't suppress [PACK] messages - we want to see them in normal mode */
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
    
    /* Check if this message should be always suppressed */
    for (size_t i = 0; i < sizeof(always_suppress) / sizeof(always_suppress[0]); i++) {
        if (strstr(fmt, always_suppress[i]) != NULL) {
            return; /* Just exit, don't print anything */
        }
    }
    
    /* Check if this message should be suppressed in non-debug mode */
    if (!debug_mode) {
        for (size_t i = 0; i < sizeof(non_debug_suppress) / sizeof(non_debug_suppress[0]); i++) {
            if (strstr(fmt, non_debug_suppress[i]) != NULL) {
                return; /* Just exit, don't print anything */
            }
        }
    }
    
    /* If we get here, the message should be printed */
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