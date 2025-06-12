#!/bin/bash
# Wrapper script to run llm_ctx with proper architecture on macOS

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Check if running on macOS
if [[ "$(uname)" == "Darwin" ]]; then
    # Check architecture of llm_ctx binary
    ARCH=$(file "$SCRIPT_DIR/llm_ctx" | grep -o 'x86_64\|arm64' | head -1)
    
    if [[ "$ARCH" == "x86_64" ]]; then
        # Run under Rosetta 2 if needed
        exec arch -x86_64 "$SCRIPT_DIR/llm_ctx" "$@"
    else
        # Run natively
        exec "$SCRIPT_DIR/llm_ctx" "$@"
    fi
else
    # Not macOS, run directly
    exec "$SCRIPT_DIR/llm_ctx" "$@"
fi