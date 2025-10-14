#include "tokenizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>
#include <unistd.h>
#include <assert.h>

/* Types from tiktoken-c */
typedef void CoreBPE;
typedef uint32_t Rank;

/* Function pointer types matching tiktoken-c API */
typedef CoreBPE* (*get_bpe_from_model_fn)(const char *model);
typedef Rank* (*encode_ordinary_fn)(CoreBPE *ptr, const char *text, size_t *num_tokens);
typedef void (*destroy_corebpe_fn)(CoreBPE *ptr);
typedef const char* (*version_fn)(void);

/* Global state for dynamic loading */
static void *g_tokenizer_lib = NULL;
static get_bpe_from_model_fn g_get_bpe_from_model = NULL;
static encode_ordinary_fn g_encode_ordinary = NULL;
static destroy_corebpe_fn g_destroy_corebpe = NULL;
static version_fn g_version_fn = NULL;
static int g_load_attempted = 0;
static int g_load_failed = 0;
static char *g_executable_dir = NULL;
static char g_load_error[512];

static void remember_dl_error(const char *err) {
    if (err && *err) {
        snprintf(g_load_error, sizeof(g_load_error), "%s", err);
    }
}

/* Platform-specific library name */
#ifdef __APPLE__
    #define TOKENIZER_LIB_NAME "tokenizer/libtiktoken_c.dylib"
#elif defined(__linux__)
    #define TOKENIZER_LIB_NAME "tokenizer/libtiktoken_c.so"
#elif defined(_WIN32)
    #define TOKENIZER_LIB_NAME "tokenizer/libtiktoken_c.dll"
#else
    #define TOKENIZER_LIB_NAME "tokenizer/libtiktoken_c.so"
#endif

/* Try to load the tokenizer library */
static void load_tokenizer_lib(void) {
    if (g_load_attempted) {
        return;
    }
    
    g_load_attempted = 1;
    g_load_error[0] = '\0';
    
    /* Try to load the library */
    g_tokenizer_lib = dlopen(TOKENIZER_LIB_NAME, RTLD_LAZY);
    if (!g_tokenizer_lib) {
        /* Get error for debug */
        const char *err = dlerror();
        if (getenv("LLMCTX_DEBUG") && err) {
            fprintf(stderr, "debug: dlopen(%s) failed: %s\n", TOKENIZER_LIB_NAME, err);
        }
        remember_dl_error(err);

        /* Try with executable directory if set */
        if (!g_tokenizer_lib && g_executable_dir) {
            char exe_path[1024];
            snprintf(exe_path, sizeof(exe_path), "%s/%s", g_executable_dir, TOKENIZER_LIB_NAME);
            g_tokenizer_lib = dlopen(exe_path, RTLD_LAZY);
            if (!g_tokenizer_lib && getenv("LLMCTX_DEBUG")) {
                const char *path_err = dlerror();
                fprintf(stderr, "debug: dlopen(%s) failed: %s\n", exe_path, path_err);
                remember_dl_error(path_err);
            } else if (!g_tokenizer_lib) {
                remember_dl_error(dlerror());
            }
        }

        /* Try with absolute path from current directory */
        if (!g_tokenizer_lib) {
            char abs_path[1024];
            if (getcwd(abs_path, sizeof(abs_path))) {
                strcat(abs_path, "/");
                strcat(abs_path, TOKENIZER_LIB_NAME);
                g_tokenizer_lib = dlopen(abs_path, RTLD_LAZY);
                if (!g_tokenizer_lib) {
                    remember_dl_error(dlerror());
                }
            }
        }

        if (!g_tokenizer_lib) {
            /* Try without path prefix in case it's in LD_LIBRARY_PATH */
            g_tokenizer_lib = dlopen("libtiktoken_c.so", RTLD_LAZY);
            if (!g_tokenizer_lib) {
                g_tokenizer_lib = dlopen("libtiktoken_c.dylib", RTLD_LAZY);
                if (!g_tokenizer_lib) {
                    remember_dl_error(dlerror());
                }
            } else {
                g_load_error[0] = '\0';
            }
        }
    }
    
    if (!g_tokenizer_lib) {
        g_load_failed = 1;
        return;
    }
    
    /* Look up the functions we need */
    g_get_bpe_from_model = (get_bpe_from_model_fn)dlsym(g_tokenizer_lib, "tiktoken_get_bpe_from_model");
    g_encode_ordinary = (encode_ordinary_fn)dlsym(g_tokenizer_lib, "tiktoken_corebpe_encode_ordinary");
    g_destroy_corebpe = (destroy_corebpe_fn)dlsym(g_tokenizer_lib, "tiktoken_destroy_corebpe");
    g_version_fn = (version_fn)dlsym(g_tokenizer_lib, "tiktoken_c_version");
    
    if (!g_get_bpe_from_model || !g_encode_ordinary || !g_destroy_corebpe) {
        dlclose(g_tokenizer_lib);
        g_tokenizer_lib = NULL;
        g_load_failed = 1;
        snprintf(g_load_error, sizeof(g_load_error), "missing required symbols in tokenizer library");
        return;
    }

    g_load_failed = 0;
    g_load_error[0] = '\0';
}

static void ensure_tokenizer_ready(void) {
    load_tokenizer_lib();
    if (g_load_failed || !g_tokenizer_lib || !g_get_bpe_from_model || !g_encode_ordinary || !g_destroy_corebpe) {
        const char *detail = g_load_error[0] ? g_load_error : "tokenizer library not found";
        fprintf(stderr, "fatal: tokenizer unavailable (%s)\n", detail);
        fprintf(stderr, "hint: run `make tokenizer` to build the vendored tokenizer library\n");
        exit(EXIT_FAILURE);
    }
}

size_t llm_count_tokens(const char *text, const char *model) {
    if (!text || !model) {
        return SIZE_MAX;
    }
    
    /* Ensure library is loaded */
    ensure_tokenizer_ready();
    
    /* Validate model string */
    assert(model != NULL);
    assert(strlen(model) > 0);
    assert(strlen(model) < 256); /* Reasonable model name length */
    
    /* Get the BPE encoder for the model */
    CoreBPE *bpe = g_get_bpe_from_model(model);
    if (!bpe) {
        fprintf(stderr, "fatal: tokenizer model '%s' not supported\n", model);
        fprintf(stderr, "hint: choose a supported model or update the vendored tokenizer\n");
        exit(EXIT_FAILURE);
    }
    
    /* Validate BPE encoder */
    assert(bpe != NULL);
    
    /* Encode the text */
    size_t num_tokens = 0;
    Rank *tokens = g_encode_ordinary(bpe, text, &num_tokens);
    
    /* Assert that we got a reasonable token count */
    if (num_tokens > 0 && num_tokens < SIZE_MAX) {
        /* Sanity check: token count should be reasonable compared to text length */
        size_t text_len = strlen(text);
        /* Most text has between 1 token per 6 chars to 1 token per 2 chars */
        if (num_tokens > text_len) {
            fprintf(stderr, "warning: suspicious token count %zu for text length %zu\n", num_tokens, text_len);
        }
    }
    
    /* Clean up */
    if (tokens && tokens != (Rank *)0x4) {
        /* Only free if it's a valid heap pointer */
        if ((uintptr_t)tokens > 0x1000) {
            free(tokens);
        }
    }
    g_destroy_corebpe(bpe);
    
    return num_tokens;
}

int llm_tokenizer_available(void) {
    load_tokenizer_lib();
    return g_tokenizer_lib != NULL && g_get_bpe_from_model != NULL;
}

void llm_set_executable_dir(const char *dir) {
    if (dir && *dir) {
        if (g_executable_dir) {
            free(g_executable_dir);
        }
        g_executable_dir = strdup(dir);
    }
}
