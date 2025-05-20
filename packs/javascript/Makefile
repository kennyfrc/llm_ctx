CC = gcc
CFLAGS = -Wall -Wextra -I. -I../.. -I../../tree-sitter-javascript/src -I../../tree-sitter-javascript/bindings/c
LDFLAGS = -fPIC -shared

# Tree-sitter JavaScript static library path
TS_JS_LIB = ../../tree-sitter-javascript/libtree-sitter-javascript.a

# Object files
OBJS = parser.so test_pack

# Default target
all: $(OBJS)

# Build the javascript language pack with tree-sitter statically linked
parser.so: js_pack.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(TS_JS_LIB) -L/opt/homebrew/lib -ltree-sitter

# Build test program with tree-sitter statically linked
test_pack: test_pack.c js_pack.c
	$(CC) $(CFLAGS) -o $@ $^ $(TS_JS_LIB) -L/opt/homebrew/lib -ltree-sitter

# Clean up
clean:
	rm -f $(OBJS)

# Test the javascript language pack
test: test_pack
	@echo "Running JavaScript pack tests..."
	@./test_pack

.PHONY: all clean test