#!/bin/bash
# Script to build the mock JavaScript grammar

set -e  # Exit on error

echo "Building mock JavaScript grammar..."

# Ensure the packs/javascript directory exists
mkdir -p packs/javascript

# Compile the mock grammar into a shared library
cc -shared -fPIC -o packs/javascript/libtree-sitter-javascript.dylib packs/javascript/grammar_mock.c

# Make it executable
chmod +x packs/javascript/libtree-sitter-javascript.dylib

echo "Mock grammar built as packs/javascript/libtree-sitter-javascript.dylib"