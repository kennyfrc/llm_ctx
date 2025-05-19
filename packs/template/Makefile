# Makefile for template language pack
CC = gcc
CFLAGS = -Wall -Wextra -fPIC -shared

# Path to your Tree-sitter grammar library
# Update this to point to your language's Tree-sitter grammar
TS_LANG_LIB = ../../tree-sitter-your-language/libtree-sitter-your-language.a

# Build the language pack shared library
parser.so: template_pack.c
	$(CC) $(CFLAGS) -o $@ $< $(TS_LANG_LIB) -L/opt/homebrew/lib -ltree-sitter

clean:
	rm -f parser.so

test: parser.so
	@echo "Testing template language pack..."
	@echo "No automated tests available. Please modify this target for your language."

.PHONY: clean test