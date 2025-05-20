# Makefile for Cami language pack
CC = gcc
CFLAGS = -Wall -Wextra -fPIC -shared

# Paths to Tree-sitter grammar libraries
TS_JS_LIB = ../../tree-sitter-javascript/libtree-sitter-javascript.a
TS_TS_LIB = ../../tree-sitter-typescript/libtree-sitter-typescript.a

# Build the language pack shared library
parser.so: cami_pack.c
	$(CC) $(CFLAGS) -o $@ $< $(TS_JS_LIB) $(TS_TS_LIB) -L/opt/homebrew/lib -ltree-sitter

clean:
	rm -f parser.so

test: parser.so
	@echo "Testing Cami language pack..."
	@echo "Testing parser with sample file..."

.PHONY: clean test