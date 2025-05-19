#!/bin/bash
# Script to build JavaScript grammar from npm package

set -e  # Exit on error

echo "Building JavaScript grammar from npm package..."

# Define paths
NPM_PKG_DIR="/Users/kennyfrc/Documents/code/fun/llm_ctx/tmp/node_modules/tree-sitter-javascript"
PACKS_DIR="/Users/kennyfrc/Documents/code/fun/llm_ctx/packs/javascript"
NODE_FILE="$NPM_PKG_DIR/prebuilds/darwin-arm64/tree-sitter-javascript.node"
DYLIB_OUT="$PACKS_DIR/libtree-sitter-javascript.dylib"

# Check if the Node native module exists
if [ ! -f "$NODE_FILE" ]; then
    echo "Error: Could not find tree-sitter-javascript.node at $NODE_FILE"
    exit 1
fi

# Copy the Node native module to our packs directory with the proper name
cp "$NODE_FILE" "$DYLIB_OUT"

# Make it executable
chmod +x "$DYLIB_OUT"

echo "Copied JavaScript grammar to: $DYLIB_OUT"
echo "Checking symbols in the library:"
nm -g "$DYLIB_OUT" | grep tree_sitter_javascript || echo "Symbol not found in library"

# Create a symlink in a standard location
mkdir -p "/Users/kennyfrc/Documents/code/fun/llm_ctx/.tree-sitter/lib"
ln -sf "$DYLIB_OUT" "/Users/kennyfrc/Documents/code/fun/llm_ctx/.tree-sitter/lib/libtree-sitter-javascript.dylib"
echo "Created symlink in standard location"

echo "JavaScript grammar setup complete"