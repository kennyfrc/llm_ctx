#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "test_framework.h"

/* Assume these are defined elsewhere (e.g., test_cli.c or a shared helper) */
/* and linked during compilation */
extern char *run_command(const char *cmd);
extern int string_contains(const char *str, const char *substr);

/**
 * Verify that llm_ctx loads .llm_ctx.conf from the directory
 * that contains the *resolved* executable path (after following
 * any symlink on $PATH). This exercises the new fallback added
 * in get_executable_dir()/find_config_file().
 *
 * Why this test matters:
 *   – Regresses the exact feature request.
 *   – Guards against future refactors that might break /proc/self/exe
 *     or _NSGetExecutablePath() handling.
 */

TEST(test_cli_config_discovery_binary_dir) {
    /* ---------- 1.  make throw-away bin dir with symlink ---------- */
    char bin_template[] = "/tmp/llm_ctx_bin_XXXXXX";
    char *bin_dir = mkdtemp(bin_template);          /* race-free */
    ASSERT("Temp bin dir created", bin_dir != NULL);
    if (!bin_dir) return; /* Avoid proceeding if mkdtemp failed */

    char symlink_path[1024];
    char executable_path[1024];
    snprintf(executable_path, sizeof(executable_path), "%s/llm_ctx", getenv("PWD") ? getenv("PWD") : ".");
    snprintf(symlink_path, sizeof(symlink_path), "%s/llm_ctx", bin_dir);
    ASSERT("Symlink created", symlink(executable_path, symlink_path) == 0);

    /* ---------- 2.  drop sibling config that flips clipboard flag ---------- */
    char conf_path[1024];
    snprintf(conf_path, sizeof(conf_path), "%s/.llm_ctx.conf", bin_dir);
    FILE *conf = fopen(conf_path, "w");
    ASSERT("Config file writable", conf != NULL);
    if (!conf) { system("rm -rf bin_dir"); return; } /* Clean up if fopen fails */
    fprintf(conf, "copy_to_clipboard=true\n");
    fclose(conf);

    /* ---------- 3.  invoke via modified $PATH from TEST_DIR ---------- */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "cd %s && PATH=\"%s:$PATH\" llm_ctx -f __regular.txt 2>&1",
             TEST_DIR, bin_dir);
    char *output = run_command(cmd);

    /* ---------- 4.  assertions ---------- */
    ASSERT("stdout suppressed (binary dir config)", !string_contains(output, "Regular file content"));
    ASSERT("clipboard message on stderr (binary dir config)", string_contains(output, "Content copied to clipboard."));

    /* ---------- 5.  cleanup ---------- */
    char rm_cmd[1024];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf %s", bin_dir);
    system(rm_cmd);
}
