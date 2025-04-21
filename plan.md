# Development Plan: `.llm_ctx.conf` Configuration File

This plan outlines the steps to add support for a configuration file (`.llm_ctx.conf`) that allows users to set default behaviors for `llm_ctx`, such as the system prompt, editor comments flag, and automatic clipboard copying. The configuration file will be automatically discovered by searching upwards from the current directory. Command-line arguments will always override settings from the configuration file.

## Data Model

We need structures and variables to manage settings from different sources (defaults, config file, CLI).

```c
// In a suitable header (e.g., config.h or main.c if kept simple)

#include <stdbool.h>

// Structure to hold settings parsed directly from the config file
typedef struct {
    char *system_prompt_source; // NULL, inline string, or "@path" format
    bool editor_comments;
    bool copy_to_clipboard;

    // Flags to track if a setting was explicitly set in the config file
    bool system_prompt_set;
    bool editor_comments_set;
    bool copy_to_clipboard_set;
} ConfigSettings;

// Global variable to store loaded config settings
// Initialized to defaults (all false/NULL)
// static ConfigSettings g_config_settings = {0}; // Initialize in .c file

// Global flags for the final effective settings after merging sources
// static bool g_effective_editor_comments = false; // Already exists as want_editor_comments
// static char *g_effective_system_prompt = NULL; // Already exists as system_instructions
static bool g_effective_copy_to_clipboard = false; // New global flag

// Flags to track if CLI options were used
static bool s_flag_used = false;
static bool e_flag_used = false;
// static bool copy_flag_used = false; // Add if CLI flags for copy are introduced

```

**Data Flow:**

1.  Initialize global effective settings (`want_editor_comments`, `system_instructions`, `g_effective_copy_to_clipboard`) and CLI usage flags (`s_flag_used`, `e_flag_used`) to their defaults (`false`/`NULL`).
2.  Parse command-line arguments using `getopt_long`. Update effective settings and set the corresponding `*_flag_used` to `true` if an option is encountered.
3.  Find the `.llm_ctx.conf` file using upward search (`find_config_file`).
4.  If found, parse it into a temporary `ConfigSettings` struct (`parse_config_file`).
5.  Merge the loaded `ConfigSettings` into the effective settings: Apply a config setting *only if* the corresponding `*_flag_used` is `false` and the setting was present in the config file (`*_set` flag is true).
6.  Use the final effective settings during program execution.

## Algorithm Design

1.  **Config File Discovery (`find_config_file`)**:
    *   Searches upwards from `getcwd()` for `.llm_ctx.conf`.
    *   Returns `char*` path (caller frees) or `NULL`.
    *   Handles reaching root (`/`) and errors gracefully.

2.  **Config File Parsing (`parse_config_file`)**:
    *   Takes `config_path` and `ConfigSettings*`.
    *   Reads line by line, trims whitespace, skips comments (`#`) and empty lines.
    *   Parses `key=value` pairs (case-sensitive key, value trimmed).
    *   Handles `system_prompt`, `editor_comments`, `copy_to_clipboard`.
    *   Parses boolean values leniently ("true", "yes", "1" vs "false", "no", "0", case-insensitive).
    *   Sets `*_set` flags in the `ConfigSettings` struct.
    *   Returns `true` on success, `false` on file open error. Ignores malformed lines/unknown keys (with optional warnings).

3.  **Merging Settings (in `main`)**:
    *   Occurs after `getopt_long` loop.
    *   Calls `find_config_file`.
    *   If found, calls `parse_config_file`.
    *   Applies settings from the parsed config *only if* the corresponding `*_flag_used` is `false` and the config setting's `*_set` flag is `true`.
    *   Handles memory management for `system_prompt_source` and `system_instructions`.

4.  **Clipboard Integration (`copy_to_clipboard`)**:
    *   Takes `const char *buffer` (final output).
    *   Uses platform-specific command (`pbcopy`, `xclip`, `wl-copy`, `clip.exe`) via `#ifdef`.
    *   Uses `popen` to pipe the buffer to the command.
    *   Handles `popen`/`pclose` errors.

## Vertical Slice Plan

1.  **Slice 1: Basic Config Loading & `copy_to_clipboard` Flag**
    *   **Goal:** Load `.llm_ctx.conf` from CWD only. Parse `copy_to_clipboard`. Set `g_effective_copy_to_clipboard`. Print debug message.
    *   **Tasks:** Add struct/globals. Basic `parse_config_file`. Basic merge logic in `main`. Add debug print.
    *   **Test:** Create `.llm_ctx.conf`. Run. Verify message.

2.  **Slice 2: Automatic Config File Discovery**
    *   **Goal:** Implement upward search.
    *   **Tasks:** Implement `find_config_file()`. Update `main` to use it.
    *   **Test:** Place config in CWD/parent. Run from subdir. Verify correct file/setting applied.

3.  **Slice 3: Implement Clipboard Copy**
    *   **Goal:** Actually copy output to clipboard.
    *   **Tasks:** Implement `copy_to_clipboard()`. Modify `main` to read `temp_file` to buffer and call `copy_to_clipboard()` or print to `stdout`. Manage buffer memory.
    *   **Test:** Set `copy_to_clipboard=true`. Run. Paste. Verify.

4.  **Slice 4: Add `editor_comments` Config & Precedence**
    *   **Goal:** Configure `-e` via file, respecting CLI override.
    *   **Tasks:** Add fields to `ConfigSettings`. Enhance parser. Add `e_flag_used`. Implement merge logic using `e_flag_used`.
    *   **Test:** Combinations of config and `-e`. Verify `response_guide`.

5.  **Slice 5: Add `system_prompt` Config & Precedence**
    *   **Goal:** Configure `-s` via file (inline/`@path`), respecting CLI override.
    *   **Tasks:** Add fields to `ConfigSettings`. Enhance parser. Add `s_flag_used`. Implement merge logic using `s_flag_used`. Reuse/adapt `handle_system_arg` logic. Manage memory.
    *   **Test:** Combinations of config (`inline`/`@file`) and `-s` variants. Verify correct prompt.

6.  **Slice 6: Refinements & Documentation**
    *   **Goal:** Add error handling, comments, update help/docs.
    *   **Tasks:** Add warnings/errors in parser. Improve file operation error handling. Add comments. Update `show_help()`. Update `README.md`.
    *   **Test:** Invalid configs. Verify warnings/errors. Check help/docs.


