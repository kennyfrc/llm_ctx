# Makefile for ruby language pack
CC = gcc
CFLAGS = -Wall -Wextra -fPIC -shared

# Path to Tree-sitter Ruby grammar library
TS_LANG_LIB = ../../tree-sitter-ruby/libtree-sitter-ruby.a

# Build the language pack shared library
parser.so: ruby_pack.c
	$(CC) $(CFLAGS) -o $@ $< $(TS_LANG_LIB) -L/opt/homebrew/lib -ltree-sitter

clean:
	rm -f parser.so

test: parser.so
	@echo "Testing language pack: ruby..."
	@echo "Create a test_ruby_pack tool to validate the ruby language pack."

.PHONY: clean test