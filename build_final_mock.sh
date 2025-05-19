#!/bin/bash
# Script to build the final mock JavaScript grammar

set -e  # Exit on error

echo "Building final mock JavaScript grammar..."

# Ensure the packs/javascript directory exists
mkdir -p packs/javascript

# On macOS, we need to use a special linker flag to avoid symbol mangling
# Use -exported_symbols_list with a file listing the symbols to export
echo "_tree_sitter_javascript" > symbols.list

# Compile the mock grammar into a shared library
gcc -shared -fPIC -o packs/javascript/libtree-sitter-javascript.dylib \
    packs/javascript/better_mock.c \
    -Wl,-reexport_library,/opt/homebrew/lib/libtree-sitter.dylib

# Verify the symbol
echo "Checking symbols in the mock library:"
nm -g packs/javascript/libtree-sitter-javascript.dylib | grep tree_sitter_javascript

echo "Making a symlink for our library so it can be found in the standard search locations"
mkdir -p /Users/kennyfrc/Documents/code/fun/llm_ctx/.tree-sitter/lib
ln -sf /Users/kennyfrc/Documents/code/fun/llm_ctx/packs/javascript/libtree-sitter-javascript.dylib \
       /Users/kennyfrc/Documents/code/fun/llm_ctx/.tree-sitter/lib/libtree-sitter-javascript.dylib

echo "Mock grammar built and linked"