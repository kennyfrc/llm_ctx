#ifndef DEBUG_H
#define DEBUG_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

extern bool debug_mode;

static inline void debug_printf(const char* fmt, ...)
{
    const char* always_suppress[] = {
        "[DEBUG] Initializing language pack: ", "[DEBUG] Cleaning up language pack: ",
        "[DEBUG] Parsing file with language pack: "};

    const char* non_debug_suppress[] = {"Successfully extracted",
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
                                        "Codemap generated successfully"};

    for (size_t i = 0; i < sizeof(always_suppress) / sizeof(always_suppress[0]); i++)
    {
        if (strstr(fmt, always_suppress[i]) != NULL)
        {
            return;
        }
    }

    if (!debug_mode)
    {
        for (size_t i = 0; i < sizeof(non_debug_suppress) / sizeof(non_debug_suppress[0]); i++)
        {
            if (strstr(fmt, non_debug_suppress[i]) != NULL)
            {
                return;
            }
        }
    }

    va_list ap;
    va_start(ap, fmt);

    if (debug_mode)
    {
        fprintf(stderr, "[DEBUG] ");
        vfprintf(stderr, fmt, ap);
        if (fmt[0] != '\0' && fmt[strlen(fmt) - 1] != '\n')
        {
            fputc('\n', stderr);
        }
    }
    else
    {
        vfprintf(stderr, fmt, ap);
        if (fmt[0] != '\0' && fmt[strlen(fmt) - 1] != '\n')
        {
            fputc('\n', stderr);
        }
    }

    va_end(ap);
}

#endif /* DEBUG_H */