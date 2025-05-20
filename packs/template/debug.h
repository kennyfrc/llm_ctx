#ifndef DEBUG_H
#define DEBUG_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* For language packs, we'll never print common initialization/cleanup messages */

/**
 * Debug print function for language packs.
 * In language packs, we specifically suppress initialization and cleanup messages.
 */
static inline void debug_printf(const char *fmt, ...) {
    /* These are messages we want to ALWAYS suppress */
    const char *always_suppress[] = {
        "[DEBUG] Initializing language pack: ",
        "[DEBUG] Cleaning up language pack: ",
        "[DEBUG] Parsing file with language pack: "
    };
    
    /* Check if this message should be suppressed completely */
    for (size_t i = 0; i < sizeof(always_suppress) / sizeof(always_suppress[0]); i++) {
        if (strstr(fmt, always_suppress[i]) != NULL) {
            return; /* Don't print anything */
        }
    }
    
    /* Only other messages get through to here */
    va_list ap;
    va_start(ap, fmt);
    
    /* Print message to stderr */
    vfprintf(stderr, fmt, ap);
    
    /* Ensure a newline terminates the message if needed */
    if (fmt[0] != '\0' && fmt[strlen(fmt) - 1] != '\n') {
        fputc('\n', stderr);
    }
    
    va_end(ap);
}

#endif /* DEBUG_H */