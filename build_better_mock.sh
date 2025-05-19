#!/bin/bash
# Script to build the improved mock JavaScript grammar

set -e  # Exit on error

echo "Building improved mock JavaScript grammar..."

# Ensure the packs/javascript directory exists
mkdir -p packs/javascript

# Compile the mock grammar into a shared library with explicit symbol export
cc -shared -fPIC -o packs/javascript/libtree-sitter-javascript.dylib packs/javascript/better_mock.c

# Make it executable
chmod +x packs/javascript/libtree-sitter-javascript.dylib

# Let's verify the symbol was exported correctly
nm -g packs/javascript/libtree-sitter-javascript.dylib | grep tree_sitter_javascript

echo "Mock grammar built as packs/javascript/libtree-sitter-javascript.dylib"